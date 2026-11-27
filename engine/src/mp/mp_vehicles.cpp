#include "mp_core.h"

#ifdef MULTIPLAYER_COOP

#include "ModelIndices.h"
#include "ModelInfo.h"
#include "Streaming.h"
#include "World.h"
#include "Pools.h"
#include "Timer.h"
#include "Game.h"
#include "CarCtrl.h"
#include "HandlingMgr.h"
#include "CivilianPed.h"
#include "AnimManager.h"
#include "AnimBlendAssociation.h"
#include "AnimBlendAssocGroup.h"
#include "RpAnimBlend.h"
#include <cstring>
#include <cmath>

static uint8 MP_ClassifyVehicleKind(CVehicle *veh)
{
	if (!veh) return MP_VEHKIND_CAR;
	if (veh->IsBike()) return MP_VEHKIND_BIKE;
	if (veh->IsBoat()) return MP_VEHKIND_BOAT;
	return MP_VEHKIND_CAR;
}

static bool MP_EnsureVehAnimGroupLoaded(AssocGroupId groupId)
{
	if (groupId < 0 || groupId >= NUM_ANIM_ASSOC_GROUPS) return false;
	CAnimBlendAssocGroup *grp = &CAnimManager::GetAnimAssocGroups()[groupId];
	if (grp && grp->animBlock && !grp->animBlock->isLoaded) {
		int32 blkIdx = CAnimManager::GetAnimationBlockIndex(grp->animBlock->name);
		if (blkIdx >= 0) {
			CStreaming::RequestAnim(blkIdx, STREAMFLAGS_DEPENDENCY | STREAMFLAGS_DONT_REMOVE);
			CStreaming::LoadAllRequestedModels(false);
		}
	}
	return !grp || !grp->animBlock || grp->animBlock->isLoaded;
}

static void MP_CleanupNpcDriver(MpSyncedVehicle &sv, CVehicle *veh)
{
	if (!sv.npcDriverPed) return;
	CPed *npc = sv.npcDriverPed;
	sv.npcDriverPed = nil;
	if (!MP_IsPoolPedValid(npc)) return;
	if (npc->bInVehicle && npc->m_pMyVehicle == veh && (!veh || !MP_IsPoolVehicleValid(veh) || veh->pDriver == npc)) {
		if (veh && MP_IsPoolVehicleValid(veh) && veh->pDriver == npc)
			veh->RemoveDriver();
		npc->RemoveInCarAnims();
		npc->bInVehicle = false;
		npc->m_pMyVehicle = nil;
		CWorld::Remove(npc);
		delete npc;
	} else {
		// Pulled out / carjacked locally: hand over to world as temporary ped
		npc->CharCreatedBy = RANDOM_CHAR;
		npc->bStreamingDontDelete = false;
	}
}

uint8 MP_ReadVehicleTireMask(CVehicle *veh)
{
	if (!veh) return 0;
	uint8 mask = 0;
	if (veh->IsCar()) {
		CAutomobile *car = (CAutomobile *)veh;
		for (int w = 0; w < 4; w++) {
			if (car->Damage.GetWheelStatus(w) != 0)
				mask |= (uint8)(1 << w);
		}
	} else if (veh->IsBike()) {
		CBike *bike = (CBike *)veh;
		if (bike->m_wheelStatus[0] != 0) mask |= 1 << 0;
		if (bike->m_wheelStatus[1] != 0) mask |= 1 << 1;
	}
	return mask;
}

void MP_ApplyVehicleTireMask(CVehicle *veh, uint8 mask)
{
	if (!veh) return;
	if (veh->IsCar()) {
		CAutomobile *car = (CAutomobile *)veh;
		for (int w = 0; w < 4; w++) {
			car->Damage.SetWheelStatus(w, (mask & (1 << w)) ? 2 : 0);
		}
	} else if (veh->IsBike()) {
		CBike *bike = (CBike *)veh;
		bike->m_wheelStatus[0] = (mask & (1 << 0)) ? 2 : 0;
		bike->m_wheelStatus[1] = (mask & (1 << 1)) ? 2 : 0;
	}
}

int32 MP_FindVehicleSlotByPtr(CVehicle *veh)
{
	if (!veh) return -1;
	for (int32 i = 0; i < MP_MAX_SYNCED_VEHICLES; i++) {
		if (gMp.vehicles[i].active && gMp.vehicles[i].veh == veh)
			return i;
	}
	return -1;
}

int32 MP_FindVehicleSlotByNetId(int32 netVehId)
{
	if (netVehId < 0) return -1;
	for (int32 i = 0; i < MP_MAX_SYNCED_VEHICLES; i++) {
		if (gMp.vehicles[i].active && gMp.vehicles[i].netVehId == netVehId)
			return i;
	}
	return -1;
}

CVehicle *MP_GetVehiclePtrByNetId(int32 netVehId)
{
	int32 slot = MP_FindVehicleSlotByNetId(netVehId);
	if (slot < 0) return nil;
	CVehicle *veh = gMp.vehicles[slot].veh;
	return (veh && MP_IsPoolVehicleValid(veh)) ? veh : nil;
}

bool MP_IsRemoteControlledVehicle(const CVehicle *veh)
{
	if (!gMp.initialized || !veh) return false;
	CPlayerPed *localPed = FindPlayerPed();
	if (localPed && localPed->bInVehicle && localPed->m_pMyVehicle == veh && veh->pDriver == localPed)
		return false;
	for (int32 i = 0; i < MP_MAX_SYNCED_VEHICLES; i++) {
		const MpSyncedVehicle &sv = gMp.vehicles[i];
		if (sv.active && sv.veh == veh && sv.ownerPeerId != gMp.localPeerId)
			return true;
	}
	return false;
}

static CVehicle *MP_CreateVehicleByModel(int32 modelIndex, const CVector &pos, bool isPlayerSpawn, bool lockFromCull)
{
	if (modelIndex < 130 || modelIndex > 236) return nil;
	CBaseModelInfo *mi = CModelInfo::GetModelInfo(modelIndex);
	if (!mi || mi->GetModelType() != MITYPE_VEHICLE) return nil;
	if (!CStreaming::HasModelLoaded(modelIndex)) {
		CStreaming::RequestModel(modelIndex, STREAMFLAGS_DEPENDENCY | STREAMFLAGS_DONT_REMOVE);
		CStreaming::LoadAllRequestedModels(false);
	}
	if (!CStreaming::HasModelLoaded(modelIndex)) return nil;

	CVehicleModelInfo *vmi = (CVehicleModelInfo *)mi;
	CVehicle *veh = nil;
	uint8 createdBy = (isPlayerSpawn || lockFromCull) ? MISSION_VEHICLE : RANDOM_VEHICLE;
	if (vmi->m_vehicleType == VEHICLE_TYPE_BIKE) {
		veh = new CBike(modelIndex, createdBy);
	} else if (vmi->m_vehicleType == VEHICLE_TYPE_BOAT) {
		veh = new CBoat(modelIndex, createdBy);
	} else if (vmi->m_vehicleType == VEHICLE_TYPE_CAR) {
		veh = new CAutomobile(modelIndex, createdBy);
	}
	if (!veh) return nil;

	veh->SetPosition(pos);
	veh->SetStatus(STATUS_ABANDONED);
	veh->bHasBeenOwnedByPlayer = isPlayerSpawn;
	veh->m_nDoorLock = CARLOCK_UNLOCKED;
	veh->m_area = (int8)CGame::currArea;
	CWorld::Add(veh);
	return veh;
}

int32 MP_TrackLocalVehicle(CVehicle *veh)
{
	if (!veh) return -1;
	int32 existing = MP_FindVehicleSlotByPtr(veh);
	if (existing >= 0) return existing;

	int32 freeSlot = -1;
	uint32 oldestMs = 0xFFFFFFFFu;
	int32 oldestSlot = -1;
	for (int32 i = 0; i < MP_MAX_SYNCED_VEHICLES; i++) {
		if (!gMp.vehicles[i].active) {
			freeSlot = i;
			break;
		}
		if (gMp.vehicles[i].ownerPeerId != gMp.localPeerId &&
		    !MP_IsVehicleOccupiedByPlayer(gMp.vehicles[i].veh) &&
		    gMp.vehicles[i].lastUpdateMs < oldestMs) {
			oldestMs = gMp.vehicles[i].lastUpdateMs;
			oldestSlot = i;
		}
	}
	int32 slot = freeSlot >= 0 ? freeSlot : oldestSlot;
	if (slot < 0) return -1;

	MpSyncedVehicle &sv = gMp.vehicles[slot];
	if (sv.active) MP_CleanupNpcDriver(sv, sv.veh);
	memset(&sv, 0, sizeof(sv));
	sv.active = true;
	sv.netVehId = gMp.nextNetVehId++;
	sv.ownerPeerId = gMp.localPeerId;
	sv.modelIndex = (uint16)veh->GetModelIndex();
	sv.vehKind = MP_ClassifyVehicleKind(veh);
	sv.color1 = veh->m_currentColour1;
	sv.color2 = veh->m_currentColour2;
	sv.area = (uint8)Clamp((int32)veh->m_area, 0, 255);
	sv.veh = veh;
	sv.pos = veh->GetPosition();
	sv.targetPos = sv.pos;
	sv.lastUpdateMs = CTimer::GetTimeInMilliseconds();
	sv.lastOwnedLocalMs = sv.lastUpdateMs;
	return slot;
}

CVehicle *MP_SpawnVehicleLocal(int32 modelIndex, float aheadDist)
{
	CPlayerPed *ped = FindPlayerPed();
	if (!ped) return nil;
	CVector fwd = ped->GetForward();
	fwd.Normalise();
	CVector pos = ped->GetPosition() + fwd * aheadDist + CVector(0.0f, 0.0f, 1.25f);
	bool foundZ = false;
	float gz = CWorld::FindGroundZFor3DCoord(pos.x, pos.y, pos.z + 5.0f, &foundZ);
	if (foundZ) pos.z = gz + 1.0f;

	CVehicle *veh = MP_CreateVehicleByModel(modelIndex, pos, true, true);
	if (!veh) return nil;
	veh->GetMatrix().SetRotateZOnly(ped->m_fRotationCur + HALFPI);
	veh->SetPosition(pos);
	MP_TrackLocalVehicle(veh);
	return veh;
}

void MP_TickVehiclesBefore(float dt)
{
	if (!gMp.initialized) return;
	uint32 now = CTimer::GetTimeInMilliseconds();
	CPlayerPed *localPed = FindPlayerPed();
	CVehicle *localVeh = localPed && localPed->bInVehicle ? localPed->m_pMyVehicle : nil;

	for (int32 i = 0; i < MP_MAX_SYNCED_VEHICLES; i++) {
		MpSyncedVehicle &sv = gMp.vehicles[i];
		if (!sv.active) continue;
		if (sv.veh && !MP_IsPoolVehicleValid(sv.veh)) {
			MP_CleanupNpcDriver(sv, nil);
			sv.veh = nil;
		}
		if (sv.ownerPeerId == gMp.localPeerId) {
			if (!sv.veh) sv.active = false;
			else MP_CleanupNpcDriver(sv, sv.veh);
			continue;
		}
		uint32 staleTimeout = gMp.isHost ? 20000u : 5500u;
		if (now - sv.lastUpdateMs > staleTimeout) {
			if (!MP_IsVehicleOccupiedByPlayer(sv.veh)) {
				MP_CleanupNpcDriver(sv, sv.veh);
				if (sv.veh) {
					CWorld::Remove(sv.veh);
					delete sv.veh;
					sv.veh = nil;
				}
				sv.active = false;
			}
			continue;
		}
		if (!sv.veh) {
			sv.veh = MP_CreateVehicleByModel(sv.modelIndex, sv.targetPos, false, true);
			if (!sv.veh) continue;
			sv.pos = sv.targetPos;
			sv.veh->GetMatrix().GetRight() = sv.right;
			sv.veh->GetMatrix().GetForward() = sv.up;
			sv.veh->GetMatrix().GetUp() = sv.at;
			sv.veh->GetMatrix().UpdateRW();
			sv.veh->SetPosition(sv.pos);
			sv.veh->UpdateRwFrame();
		}
		CVehicle *veh = sv.veh;
		if (veh == localVeh && localVeh->pDriver == localPed) {
			MP_CleanupNpcDriver(sv, veh);
			sv.ownerPeerId = gMp.localPeerId;
			sv.driverPeerId = (uint8)gMp.localPeerId;
			sv.releaseOwnerAtMs = now + 1200;
			continue;
		}

		float dtExtrap = Min((float)(now - sv.lastUpdateMs) * 0.001f, 0.12f);
		CVector predPos = sv.targetPos + sv.vel * (dtExtrap * 50.0f);
		float blend = Clamp(dt * 16.0f, 0.16f, 0.78f);
		sv.pos = sv.pos + (predPos - sv.pos) * blend;
		if ((sv.pos - predPos).MagnitudeSqr() > 49.0f) {
			sv.pos = predPos;
		}
		veh->GetMatrix().GetRight() = sv.right;
		veh->GetMatrix().GetForward() = sv.up;
		veh->GetMatrix().GetUp() = sv.at;
		veh->GetMatrix().UpdateRW();
		veh->SetPosition(sv.pos);
		veh->UpdateRwFrame();
		veh->m_vecMoveSpeed = sv.vel;
		veh->m_vecTurnSpeed = sv.turn;
		veh->m_fSteerAngle = sv.steerAngle;
		veh->m_fGasPedal = sv.gasPedal;
		veh->m_fBrakePedal = sv.brakePedal;
		veh->m_fHealth = Clamp(sv.health, 0.0f, 1000.0f);
		veh->m_currentColour1 = sv.color1;
		veh->m_currentColour2 = sv.color2;
		veh->bEngineOn = (sv.vflags & 1) != 0;
		veh->bLightsOn = (sv.vflags & 2) != 0;
		veh->m_bSirenOrAlarm = (sv.vflags & 4) != 0 || sv.siren != 0;
		veh->bIsHandbrakeOn = (sv.vflags & 32) != 0;
		veh->m_nCarHornTimer = (sv.vflags & 64) ? 1 : 0;
		veh->m_area = (int8)sv.area;
		veh->AutoPilot.m_nCarMission = MISSION_NONE;
		veh->SetStatus((sv.vflags & 8) ? STATUS_WRECKED : STATUS_PHYSICS);
		if (veh->IsCar() && !veh->bIsHandbrakeOn && veh->m_fBrakePedal <= 0.05f) {
			CAutomobile *car = (CAutomobile *)veh;
			for (int w = 0; w < 4; w++) {
				if (car->m_aWheelState[w] == WHEEL_STATE_FIXED || car->m_aWheelState[w] == WHEEL_STATE_SKIDDING)
					car->m_aWheelState[w] = WHEEL_STATE_NORMAL;
			}
		}
		if ((sv.vflags & 8) && veh->GetStatus() != STATUS_WRECKED) {
			veh->BlowUpCar(nil);
		}
		MP_ApplyVehicleTireMask(veh, sv.tireMask);

		if (veh->IsCar() && veh->IsRealHeli()) {
			CAutomobile *heli = (CAutomobile *)veh;
			heli->m_fPropellerRotation += sv.rotorSpeed * dt * 60.0f;
		}

		// Synced NPC driver visual ped (e.g. ambient traffic driven by Host)
		bool localInteractingWithVeh = localPed && localPed->m_pMyVehicle == veh;
		if (sv.npcDriverModel > 0 && sv.driverPeerId == 0 && !localInteractingWithVeh && !(sv.vflags & 8)) {
			if (sv.npcDriverPed && (!MP_IsPoolPedValid(sv.npcDriverPed) || !sv.npcDriverPed->bInVehicle || veh->pDriver != sv.npcDriverPed)) {
				MP_CleanupNpcDriver(sv, veh);
			}
			if (!sv.npcDriverPed && !veh->pDriver) {
				int32 npcMi = (int32)sv.npcDriverModel;
				CBaseModelInfo *pmi = CModelInfo::GetModelInfo(npcMi);
				if (!pmi || pmi->GetModelType() != MITYPE_PED) npcMi = MI_MALE01;
				if (!CStreaming::HasModelLoaded(npcMi)) {
					CStreaming::RequestModel(npcMi, STREAMFLAGS_DEPENDENCY | STREAMFLAGS_DONT_REMOVE);
					CStreaming::LoadAllRequestedModels(false);
				}
				if (CStreaming::HasModelLoaded(npcMi)) {
					CPed *npc = new CCivilianPed(PEDTYPE_CIVMALE, npcMi);
					if (npc) {
						npc->CharCreatedBy = MISSION_CHAR;
						npc->bRespondsToThreats = false;
						npc->bAllowMedicsToReviveMe = false;
						npc->bStreamingDontDelete = true;
						npc->bRenderPedInCar = !veh->bIsBus;
						npc->bUsesCollision = false;
						npc->SetPosition(veh->GetPosition());
						CWorld::Add(npc);
						veh->SetDriver(npc);
						npc->m_pMyVehicle = veh;
						npc->m_pMyVehicle->RegisterReference((CEntity **)&npc->m_pMyVehicle);
						npc->bInVehicle = true;
						npc->bChangedSeat = false;
						npc->SetPedState(PED_DRIVING);
						if (veh->IsBike()) MP_EnsureVehAnimGroupLoaded((AssocGroupId)((CBike *)veh)->m_bikeAnimType);
						npc->AddInCarAnims(veh, true);
						npc->RemoveWeaponWhenEnteringVehicle();
						npc->SetPedPositionInCar();
						npc->UpdateRwFrame();
						sv.npcDriverPed = npc;
					}
				}
			}
			if (sv.npcDriverPed && MP_IsPoolPedValid(sv.npcDriverPed) && sv.npcDriverPed->bInVehicle) {
				sv.npcDriverPed->SetPedPositionInCar();
				sv.npcDriverPed->UpdateRwFrame();
			}
		} else if (sv.npcDriverPed) {
			if (!localInteractingWithVeh || (sv.npcDriverPed->bInVehicle && sv.npcDriverPed->m_pMyVehicle == veh && sv.driverPeerId != 0)) {
				MP_CleanupNpcDriver(sv, veh);
			} else if (!sv.npcDriverPed->bInVehicle) {
				// Local player pulled the NPC out; detach tracking
				sv.npcDriverPed->CharCreatedBy = RANDOM_CHAR;
				sv.npcDriverPed->bStreamingDontDelete = false;
				sv.npcDriverPed = nil;
			}
		}

		if (veh == localVeh && localPed) {
			localPed->SetPedPositionInCar();
		}
	}
}

static void MP_WriteAndSendVehicleSync(MpSyncedVehicle &sv, CPed *localPed)
{
	CVehicle *v = sv.veh;
	sv.pos = v->GetPosition();
	sv.vel = v->m_vecMoveSpeed;
	sv.turn = v->m_vecTurnSpeed;
	sv.right = v->GetRight();
	sv.up = v->GetForward();
	sv.at = v->GetUp();
	sv.color1 = v->m_currentColour1;
	sv.color2 = v->m_currentColour2;
	sv.siren = v->m_bSirenOrAlarm ? 1 : 0;
	sv.vflags = (v->bEngineOn ? 1 : 0)
		| (v->bLightsOn ? 2 : 0)
		| (v->m_bSirenOrAlarm ? 4 : 0)
		| (v->GetStatus() == STATUS_WRECKED ? 8 : 0)
		| ((v->m_nDoorLock != CARLOCK_UNLOCKED && v->m_nDoorLock != CARLOCK_NOT_USED) ? 16 : 0)
		| (v->bIsHandbrakeOn ? 32 : 0)
		| (v->m_nCarHornTimer > 0 ? 64 : 0);
	sv.tireMask = MP_ReadVehicleTireMask(v);
	sv.area = (uint8)Clamp((int32)v->m_area, 0, 255);
	sv.steerAngle = v->m_fSteerAngle;
	sv.gasPedal = v->m_fGasPedal;
	sv.brakePedal = v->m_fBrakePedal;
	sv.health = v->m_fHealth;
	sv.npcDriverModel = (v->pDriver && v->pDriver != localPed && !MP_IsRemotePlayerPed(v->pDriver) && MP_IsPoolPedValid(v->pDriver))
		? (uint16)Clamp((int32)v->pDriver->GetModelIndex(), 1, 65535) : 0;
	if (v->IsCar() && v->IsRealHeli()) {
		sv.rotorSpeed = ((CAutomobile *)v)->m_fPropellerRotation;
	} else {
		sv.rotorSpeed = 0.0f;
	}

	uint8 pkt[144];
	int32 p = 0;
	pkt[p++] = MP_PKT_VEHICLE_SYNC;
	memcpy(pkt + p, &sv.netVehId, 4); p += 4;
	memcpy(pkt + p, &sv.modelIndex, 2); p += 2;
	pkt[p++] = sv.vehKind;
	pkt[p++] = sv.color1;
	pkt[p++] = sv.color2;
	pkt[p++] = sv.vflags;
	pkt[p++] = sv.tireMask;
	pkt[p++] = sv.area;
	pkt[p++] = sv.driverPeerId;
	memcpy(pkt + p, &sv.npcDriverModel, 2); p += 2;
	memcpy(pkt + p, &sv.pos, 12); p += 12;
	memcpy(pkt + p, &sv.vel, 12); p += 12;
	memcpy(pkt + p, &sv.turn, 12); p += 12;
	memcpy(pkt + p, &sv.right, 12); p += 12;
	memcpy(pkt + p, &sv.up, 12); p += 12;
	memcpy(pkt + p, &sv.at, 12); p += 12;
	memcpy(pkt + p, &sv.steerAngle, 4); p += 4;
	memcpy(pkt + p, &sv.gasPedal, 4); p += 4;
	memcpy(pkt + p, &sv.brakePedal, 4); p += 4;
	memcpy(pkt + p, &sv.health, 4); p += 4;
	float rotor = (v->IsCar() && v->IsRealHeli()) ? 0.5f : 0.0f;
	memcpy(pkt + p, &rotor, 4); p += 4;
	MP_EnqueuePacket(-1, 0, pkt, p);
}

void MP_TickVehiclesAfter(float)
{
	if (!gMp.initialized) return;
	CPlayerPed *local = FindPlayerPed();
	CVehicle *localVeh = (local && local->bInVehicle && local->m_pMyVehicle && MP_IsPoolVehicleValid(local->m_pMyVehicle))
		? local->m_pMyVehicle : nil;

	if (localVeh && localVeh->pDriver != local) {
		local->SetPedPositionInCar();
	}

	// Preserve synced vehicle control/light states after ProcessControl & before RenderEffects
	for (int32 i = 0; i < MP_MAX_SYNCED_VEHICLES; i++) {
		MpSyncedVehicle &sv = gMp.vehicles[i];
		if (!sv.active || sv.ownerPeerId == gMp.localPeerId || !sv.veh || !MP_IsPoolVehicleValid(sv.veh)) continue;
		if (sv.veh == localVeh && localVeh->pDriver == local) continue;
		CVehicle *v = sv.veh;
		v->m_fSteerAngle = sv.steerAngle;
		v->m_fGasPedal = sv.gasPedal;
		v->m_fBrakePedal = sv.brakePedal;
		v->bEngineOn = (sv.vflags & 1) != 0;
		v->bLightsOn = (sv.vflags & 2) != 0;
		v->m_bSirenOrAlarm = (sv.vflags & 4) != 0 || sv.siren != 0;
		v->bIsHandbrakeOn = (sv.vflags & 32) != 0;
		v->m_nCarHornTimer = (sv.vflags & 64) ? 1 : 0;
		if (v->IsCar() && !v->bIsHandbrakeOn && v->m_fBrakePedal <= 0.05f) {
			CAutomobile *car = (CAutomobile *)v;
			for (int w = 0; w < 4; w++) {
				if (car->m_aWheelState[w] == WHEEL_STATE_FIXED || car->m_aWheelState[w] == WHEEL_STATE_SKIDDING)
					car->m_aWheelState[w] = WHEEL_STATE_NORMAL;
			}
		}
	}

	uint32 now = CTimer::GetTimeInMilliseconds();
	if (now - gMp.lastVehSendMs < 30) return;
	gMp.lastVehSendMs = now;

	if (localVeh && localVeh->pDriver == local) {
		int32 slot = MP_TrackLocalVehicle(localVeh);
		if (slot >= 0) {
			gMp.vehicles[slot].ownerPeerId = gMp.localPeerId;
			gMp.vehicles[slot].driverPeerId = (uint8)gMp.localPeerId;
			gMp.vehicles[slot].releaseOwnerAtMs = now + 1200;
		}
	}

	CVector focus = local ? local->GetPosition() : CVector(0, 0, 0);
	if (gMp.isHost) {
		CPool<CVehicle, CAutomobile> *vPool = CPools::GetVehiclePool();
		if (vPool) {
			for (int32 idx = 0; idx < vPool->GetSize(); idx++) {
				CVehicle *v = vPool->GetSlot(idx);
				if (!v) continue;
				bool trackable = (v->VehicleCreatedBy == MISSION_VEHICLE || v->bHasBeenOwnedByPlayer)
					|| ((v->GetPosition() - focus).MagnitudeSqr2D() <= SQR(140.0f) || MP_IsNearAnyPlayer2D(v->GetPosition(), 140.0f));
				if (trackable && MP_FindVehicleSlotByPtr(v) < 0) {
					MP_TrackLocalVehicle(v);
				}
			}
		}
	}

	// Detect removed / out-of-range locally owned vehicles and inform peers reliably
	for (int32 i = 0; i < MP_MAX_SYNCED_VEHICLES; i++) {
		MpSyncedVehicle &sv = gMp.vehicles[i];
		if (!sv.active || sv.ownerPeerId != gMp.localPeerId) continue;
		bool valid = sv.veh && MP_IsPoolVehicleValid(sv.veh);
		bool cullDistant = false;
		if (valid && gMp.isHost && sv.veh->VehicleCreatedBy != MISSION_VEHICLE && !sv.veh->bHasBeenOwnedByPlayer && !MP_IsVehicleOccupiedByPlayer(sv.veh)) {
			if ((sv.veh->GetPosition() - focus).MagnitudeSqr2D() > SQR(175.0f) && !MP_IsNearAnyPlayer2D(sv.veh->GetPosition(), 175.0f)) {
				cullDistant = true;
			}
		}
		if (!valid || cullDistant) {
			uint8 rpkt[8];
			rpkt[0] = MP_PKT_VEHICLE_REMOVE;
			memcpy(rpkt + 1, &sv.netVehId, 4);
			MP_EnqueuePacket(-1, 1, rpkt, 5);
			MP_CleanupNpcDriver(sv, sv.veh);
			sv.veh = nil;
			sv.active = false;
		}
	}

	bool sentFlag[MP_MAX_SYNCED_VEHICLES];
	memset(sentFlag, 0, sizeof(sentFlag));
	int32 sent = 0;

	// Pass 1: priority sync for player-driven, moving, or mission vehicles
	for (int32 i = 0; i < MP_MAX_SYNCED_VEHICLES && sent < 22; i++) {
		MpSyncedVehicle &sv = gMp.vehicles[i];
		if (!sv.active || sv.ownerPeerId != gMp.localPeerId || !sv.veh || !MP_IsPoolVehicleValid(sv.veh)) continue;
		CVehicle *v = sv.veh;
		bool localUsing = (localVeh == v && v->pDriver == local)
			|| (local && local->m_pMyVehicle == v && (local->m_nPedState == PED_ENTER_CAR || local->m_nPedState == PED_CARJACK || local->m_nPedState == PED_EXIT_CAR));
		bool priority = localUsing || v->VehicleCreatedBy == MISSION_VEHICLE || v->m_vecMoveSpeed.MagnitudeSqr() > 0.0001f;
		if (!priority) continue;

		if (localUsing) {
			sv.driverPeerId = (uint8)gMp.localPeerId;
			sv.releaseOwnerAtMs = now + 1200;
		} else {
			sv.driverPeerId = 0;
		}
		MP_WriteAndSendVehicleSync(sv, local);
		sentFlag[i] = true;
		sent++;
		if (!gMp.isHost && !localUsing && sv.releaseOwnerAtMs != 0 && now > sv.releaseOwnerAtMs) {
			sv.ownerPeerId = 1;
			sv.releaseOwnerAtMs = 0;
		}
	}

	// Pass 2: round-robin sync for parked/stationary vehicles
	for (int32 step = 0; step < MP_MAX_SYNCED_VEHICLES && sent < 22; step++) {
		int32 i = (int32)((gMp.vehScanCursor + (uint32)step) % MP_MAX_SYNCED_VEHICLES);
		MpSyncedVehicle &sv = gMp.vehicles[i];
		if (sentFlag[i] || !sv.active || sv.ownerPeerId != gMp.localPeerId || !sv.veh || !MP_IsPoolVehicleValid(sv.veh)) continue;
		sv.driverPeerId = 0;
		MP_WriteAndSendVehicleSync(sv, local);
		sentFlag[i] = true;
		sent++;
		if (!gMp.isHost && sv.releaseOwnerAtMs != 0 && now > sv.releaseOwnerAtMs) {
			sv.ownerPeerId = 1;
			sv.releaseOwnerAtMs = 0;
		}
	}
	gMp.vehScanCursor = (gMp.vehScanCursor + 7) % MP_MAX_SYNCED_VEHICLES;
}

void MP_HandleVehicleSyncPacket(int32 fromPeerId, const uint8 *data, int32 len)
{
	if (len < 108) return;
	int32 p = 1;
	int32 netVehId = -1;
	uint16 mi = 0;
	memcpy(&netVehId, data + p, 4); p += 4;
	memcpy(&mi, data + p, 2); p += 2;
	uint8 kind = data[p++];
	uint8 c1 = data[p++];
	uint8 c2 = data[p++];
	uint8 vflags = data[p++];
	uint8 tireMask = data[p++];
	uint8 area = data[p++];
	uint8 driverPeer = data[p++];
	uint16 npcDriverMi = 0;
	memcpy(&npcDriverMi, data + p, 2); p += 2;

	int32 slot = MP_FindVehicleSlotByNetId(netVehId);
	if (slot < 0) {
		for (int32 i = 0; i < MP_MAX_SYNCED_VEHICLES; i++) {
			if (!gMp.vehicles[i].active) { slot = i; break; }
		}
	}
	if (slot < 0) return;

	MpSyncedVehicle &sv = gMp.vehicles[slot];
	CPlayerPed *local = FindPlayerPed();
	if (sv.active && sv.veh && MP_IsPoolVehicleValid(sv.veh) && local && local->bInVehicle && local->m_pMyVehicle == sv.veh && sv.veh->pDriver == local) {
		// Local player actively owns & drives this vehicle
		return;
	}

	bool fresh = !sv.active;
	sv.active = true;
	sv.netVehId = netVehId;
	sv.ownerPeerId = fromPeerId;
	sv.modelIndex = mi;
	sv.vehKind = kind;
	sv.color1 = c1;
	sv.color2 = c2;
	sv.vflags = vflags;
	sv.siren = (vflags & 4) ? 1 : 0;
	sv.tireMask = tireMask;
	sv.area = area;
	sv.driverPeerId = driverPeer;
	sv.npcDriverModel = npcDriverMi;

	memcpy(&sv.targetPos, data + p, 12); p += 12;
	memcpy(&sv.vel, data + p, 12); p += 12;
	memcpy(&sv.turn, data + p, 12); p += 12;
	memcpy(&sv.right, data + p, 12); p += 12;
	memcpy(&sv.up, data + p, 12); p += 12;
	memcpy(&sv.at, data + p, 12); p += 12;
	memcpy(&sv.steerAngle, data + p, 4); p += 4;
	memcpy(&sv.gasPedal, data + p, 4); p += 4;
	memcpy(&sv.brakePedal, data + p, 4); p += 4;
	memcpy(&sv.health, data + p, 4); p += 4;
	memcpy(&sv.rotorSpeed, data + p, 4); p += 4;

	if (fresh || (sv.pos - sv.targetPos).MagnitudeSqr() > 400.0f) {
		sv.pos = sv.targetPos;
	}
	sv.lastUpdateMs = CTimer::GetTimeInMilliseconds();
}

void MP_HandleVehicleRemovePacket(int32, const uint8 *data, int32 len)
{
	if (len < 5) return;
	int32 netVehId = -1;
	memcpy(&netVehId, data + 1, 4);
	int32 slot = MP_FindVehicleSlotByNetId(netVehId);
	if (slot < 0) return;
	MpSyncedVehicle &sv = gMp.vehicles[slot];
	if (MP_IsVehicleOccupiedByPlayer(sv.veh)) {
		sv.ownerPeerId = gMp.localPeerId;
		return;
	}
	MP_CleanupNpcDriver(sv, sv.veh);
	if (sv.veh && MP_IsPoolVehicleValid(sv.veh)) {
		CWorld::Remove(sv.veh);
		delete sv.veh;
	}
	sv.veh = nil;
	sv.active = false;
}

void MP_HandleVehicleClaimPacket(int32 fromPeerId, const uint8 *data, int32 len)
{
	if (len < 5) return;
	int32 netVehId = -1;
	memcpy(&netVehId, data + 1, 4);
	int32 slot = MP_FindVehicleSlotByNetId(netVehId);
	if (slot >= 0) {
		MpSyncedVehicle &sv = gMp.vehicles[slot];
		sv.ownerPeerId = fromPeerId;
		sv.driverPeerId = (uint8)fromPeerId;
		sv.lastUpdateMs = CTimer::GetTimeInMilliseconds();
		if (sv.veh && MP_IsPoolVehicleValid(sv.veh)) {
			sv.veh->AutoPilot.m_nCarMission = MISSION_NONE;
			sv.veh->SetStatus(STATUS_PHYSICS);
			MP_CleanupNpcDriver(sv, sv.veh);
			if (sv.veh->pDriver && sv.veh->pDriver != FindPlayerPed() && !MP_IsRemotePlayerPed(sv.veh->pDriver)) {
				CPed *npc = sv.veh->pDriver;
				sv.veh->RemoveDriver();
				npc->RemoveInCarAnims();
				npc->bInVehicle = false;
				npc->m_pMyVehicle = nil;
				CWorld::Remove(npc);
				delete npc;
			}
		}
	}
}

bool MP_OnLocalEnterVehicleAttempt(CPed *ped, CVehicle *veh, bool asPassenger)
{
	if (!gMp.initialized || !ped || !veh) return false;

	int32 slot = MP_FindVehicleSlotByPtr(veh);
	bool hasRemoteDriver = (veh->pDriver && MP_IsRemotePlayerPed(veh->pDriver));
	if (slot >= 0 && gMp.vehicles[slot].driverPeerId > 0 && gMp.vehicles[slot].driverPeerId != (uint8)gMp.localPeerId) {
		hasRemoteDriver = true;
	}

	if (asPassenger || hasRemoteDriver) {
		if (veh->IsBike()) {
			if (!veh->pPassengers[0]) {
				ped->m_vehDoor = CAR_DOOR_LR;
				ped->SetObjective(OBJECTIVE_ENTER_CAR_AS_PASSENGER, veh);
				return true;
			}
			return hasRemoteDriver;
		}
		static const struct { int seat; uint32 door; } kSeats[3] = {
			{ 0, CAR_DOOR_RF },
			{ 2, CAR_DOOR_RR },
			{ 1, CAR_DOOR_LR }
		};
		int maxPass = Min((int)veh->m_nNumMaxPassengers, (int)ARRAY_SIZE(veh->pPassengers));
		for (int i = 0; i < 3; i++) {
			if (kSeats[i].seat < maxPass && !veh->pPassengers[kSeats[i].seat]) {
				ped->m_vehDoor = (uint16)kSeats[i].door;
				ped->SetObjective(OBJECTIVE_ENTER_CAR_AS_PASSENGER, veh);
				return true;
			}
		}
		return hasRemoteDriver;
	}

	if (slot < 0) slot = MP_TrackLocalVehicle(veh);
	if (slot >= 0) {
		gMp.vehicles[slot].ownerPeerId = gMp.localPeerId;
		gMp.vehicles[slot].driverPeerId = (uint8)gMp.localPeerId;
		gMp.vehicles[slot].releaseOwnerAtMs = CTimer::GetTimeInMilliseconds() + 5000;
		uint8 pkt[6];
		pkt[0] = MP_PKT_VEHICLE_CLAIM;
		memcpy(pkt + 1, &gMp.vehicles[slot].netVehId, 4);
		pkt[5] = 0;
		MP_EnqueuePacket(-1, 1, pkt, 6);
	}
	return false;
}

void MP_OnVehicleDestroyed(CVehicle *veh)
{
	if (!gMp.initialized || !veh) return;
	int32 slot = MP_FindVehicleSlotByPtr(veh);
	if (slot >= 0) {
		gMp.vehicles[slot].health = 0.0f;
		gMp.vehicles[slot].vflags |= 8;
	}
}

void MP_CleanupRemoteVehicles(void)
{
	CPlayerPed *local = FindPlayerPed();
	CVehicle *localVeh = local && local->bInVehicle ? local->m_pMyVehicle : nil;
	for (int32 i = 0; i < MP_MAX_SYNCED_VEHICLES; i++) {
		MP_CleanupNpcDriver(gMp.vehicles[i], gMp.vehicles[i].veh);
		if (gMp.vehicles[i].veh && gMp.vehicles[i].ownerPeerId != gMp.localPeerId && gMp.vehicles[i].veh != localVeh) {
			if (MP_IsPoolVehicleValid(gMp.vehicles[i].veh)) {
				CWorld::Remove(gMp.vehicles[i].veh);
				delete gMp.vehicles[i].veh;
			}
			gMp.vehicles[i].veh = nil;
		}
		gMp.vehicles[i].active = false;
	}
}

#endif // MULTIPLAYER_COOP
