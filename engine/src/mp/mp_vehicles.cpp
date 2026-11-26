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
#include <cstring>
#include <cmath>

static uint8 MP_ClassifyVehicleKind(CVehicle *veh)
{
	if (!veh) return MP_VEHKIND_CAR;
	if (veh->IsBike()) return MP_VEHKIND_BIKE;
	if (veh->IsBoat()) return MP_VEHKIND_BOAT;
	return MP_VEHKIND_CAR;
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

static CVehicle *MP_CreateVehicleByModel(int32 modelIndex, const CVector &pos, bool isPlayerSpawn)
{
	if (modelIndex < 130 || modelIndex > 236) return nil;
	CBaseModelInfo *mi = CModelInfo::GetModelInfo(modelIndex);
	if (!mi || mi->GetModelType() != MITYPE_VEHICLE) return nil;
	if (!CStreaming::HasModelLoaded(modelIndex)) {
		CStreaming::RequestModel(modelIndex, STREAMFLAGS_DONT_REMOVE);
		CStreaming::LoadAllRequestedModels(false);
	}
	if (!CStreaming::HasModelLoaded(modelIndex)) return nil;

	CVehicleModelInfo *vmi = (CVehicleModelInfo *)mi;
	CVehicle *veh = nil;
	uint8 createdBy = isPlayerSpawn ? MISSION_VEHICLE : RANDOM_VEHICLE;
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
	veh->m_area = (int8)CGame::currArea;
	CWorld::Add(veh);
	return veh;
}

static int32 MP_TrackLocalVehicle(CVehicle *veh)
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
		if (gMp.vehicles[i].ownerPeerId != gMp.localPeerId && gMp.vehicles[i].lastUpdateMs < oldestMs) {
			oldestMs = gMp.vehicles[i].lastUpdateMs;
			oldestSlot = i;
		}
	}
	int32 slot = freeSlot >= 0 ? freeSlot : oldestSlot;
	if (slot < 0) return -1;

	MpSyncedVehicle &sv = gMp.vehicles[slot];
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

	CVehicle *veh = MP_CreateVehicleByModel(modelIndex, pos, true);
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
			sv.veh = nil;
		}
		if (sv.ownerPeerId == gMp.localPeerId) {
			if (!sv.veh) sv.active = false;
			continue;
		}
		if (now - sv.lastUpdateMs > 20000) {
			if (sv.veh && (!localVeh || localVeh != sv.veh)) {
				CWorld::Remove(sv.veh);
				delete sv.veh;
				sv.veh = nil;
			}
			sv.active = false;
			continue;
		}
		if (!sv.veh) {
			sv.veh = MP_CreateVehicleByModel(sv.modelIndex, sv.targetPos, false);
			if (!sv.veh) continue;
			sv.pos = sv.targetPos;
		}
		CVehicle *veh = sv.veh;
		if (veh == localVeh && localVeh->pDriver == localPed) {
			// Local player took over driver seat
			sv.ownerPeerId = gMp.localPeerId;
			continue;
		}

		float dtExtrap = Min((float)(now - sv.lastUpdateMs) * 0.001f, 0.12f);
		CVector predPos = sv.targetPos + sv.vel * (dtExtrap * 50.0f);
		float blend = Clamp(dt * 14.0f, 0.12f, 0.72f);
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
		veh->m_fHealth = Clamp(sv.health, 0.0f, 1000.0f);
		veh->m_currentColour1 = sv.color1;
		veh->m_currentColour2 = sv.color2;
		veh->bEngineOn = true;
		veh->m_bSirenOrAlarm = sv.siren != 0;
		veh->m_area = (int8)sv.area;
		veh->AutoPilot.m_nCarMission = MISSION_NONE;
		veh->SetStatus(STATUS_PHYSICS);
		MP_ApplyVehicleTireMask(veh, sv.tireMask);

		if (veh->IsCar() && veh->IsRealHeli()) {
			CAutomobile *heli = (CAutomobile *)veh;
			heli->m_fPropellerRotation += sv.rotorSpeed * dt * 60.0f;
		}
		if (veh == localVeh && localPed) {
			localPed->SetPedPositionInCar();
		}
	}
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

	uint32 now = CTimer::GetTimeInMilliseconds();
	if (now - gMp.lastVehSendMs < 30) return;
	gMp.lastVehSendMs = now;

	if (localVeh && localVeh->pDriver == local) {
		int32 slot = MP_TrackLocalVehicle(localVeh);
		if (slot >= 0) {
			gMp.vehicles[slot].ownerPeerId = gMp.localPeerId;
		}
	}
	if (gMp.isHost) {
		CVector focus = local ? local->GetPosition() : CVector(0, 0, 0);
		CPool<CVehicle, CAutomobile> *vPool = CPools::GetVehiclePool();
		if (vPool) {
			for (int32 idx = 0; idx < vPool->GetSize(); idx++) {
				CVehicle *v = vPool->GetSlot(idx);
				if (!v) continue;
				bool trackable = (v->VehicleCreatedBy == MISSION_VEHICLE || v->bHasBeenOwnedByPlayer)
					&& ((v->GetPosition() - focus).MagnitudeSqr2D() <= SQR(95.0f) || MP_IsNearAnyPlayer2D(v->GetPosition(), 95.0f));
				if (trackable && MP_FindVehicleSlotByPtr(v) < 0) {
					MP_TrackLocalVehicle(v);
				}
			}
		}
	}

	int32 sent = 0;
	for (int32 i = 0; i < MP_MAX_SYNCED_VEHICLES && sent < 12; i++) {
		MpSyncedVehicle &sv = gMp.vehicles[i];
		if (!sv.active || sv.ownerPeerId != gMp.localPeerId) continue;
		if (!sv.veh || !MP_IsPoolVehicleValid(sv.veh)) {
			sv.veh = nil;
			sv.active = false;
			continue;
		}
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
		sv.tireMask = MP_ReadVehicleTireMask(v);
		sv.area = (uint8)Clamp((int32)v->m_area, 0, 255);
		sv.health = v->m_fHealth;
		if (v->IsCar() && v->IsRealHeli()) {
			sv.rotorSpeed = ((CAutomobile *)v)->m_fPropellerRotation;
		} else {
			sv.rotorSpeed = 0.0f;
		}

		uint8 pkt[128];
		int32 p = 0;
		pkt[p++] = MP_PKT_VEHICLE_SYNC;
		memcpy(pkt + p, &sv.netVehId, 4); p += 4;
		memcpy(pkt + p, &sv.modelIndex, 2); p += 2;
		pkt[p++] = sv.vehKind;
		pkt[p++] = sv.color1;
		pkt[p++] = sv.color2;
		pkt[p++] = sv.siren;
		pkt[p++] = sv.tireMask;
		pkt[p++] = sv.area;
		memcpy(pkt + p, &sv.pos, 12); p += 12;
		memcpy(pkt + p, &sv.vel, 12); p += 12;
		memcpy(pkt + p, &sv.right, 12); p += 12;
		memcpy(pkt + p, &sv.up, 12); p += 12;
		memcpy(pkt + p, &sv.at, 12); p += 12;
		memcpy(pkt + p, &sv.health, 4); p += 4;
		float rotor = (v->IsCar() && v->IsRealHeli()) ? 0.5f : 0.0f;
		memcpy(pkt + p, &rotor, 4); p += 4;
		MP_EnqueuePacket(-1, 0, pkt, p);
		sent++;
	}
}

void MP_HandleVehicleSyncPacket(int32 fromPeerId, const uint8 *data, int32 len)
{
	if (len < 79) return;
	int32 p = 1;
	int32 netVehId = -1;
	uint16 mi = 0;
	memcpy(&netVehId, data + p, 4); p += 4;
	memcpy(&mi, data + p, 2); p += 2;
	uint8 kind = data[p++];
	uint8 c1 = data[p++];
	uint8 c2 = data[p++];
	uint8 siren = data[p++];
	uint8 tireMask = data[p++];
	uint8 area = data[p++];

	int32 slot = MP_FindVehicleSlotByNetId(netVehId);
	if (slot < 0) {
		for (int32 i = 0; i < MP_MAX_SYNCED_VEHICLES; i++) {
			if (!gMp.vehicles[i].active) { slot = i; break; }
		}
	}
	if (slot < 0) return;

	MpSyncedVehicle &sv = gMp.vehicles[slot];
	bool fresh = !sv.active;
	sv.active = true;
	sv.netVehId = netVehId;
	sv.ownerPeerId = fromPeerId;
	sv.modelIndex = mi;
	sv.vehKind = kind;
	sv.color1 = c1;
	sv.color2 = c2;
	sv.siren = siren;
	sv.tireMask = tireMask;
	sv.area = area;

	memcpy(&sv.targetPos, data + p, 12); p += 12;
	memcpy(&sv.vel, data + p, 12); p += 12;
	memcpy(&sv.right, data + p, 12); p += 12;
	memcpy(&sv.up, data + p, 12); p += 12;
	memcpy(&sv.at, data + p, 12); p += 12;
	memcpy(&sv.health, data + p, 4); p += 4;
	memcpy(&sv.rotorSpeed, data + p, 4); p += 4;

	if (fresh || (sv.pos - sv.targetPos).MagnitudeSqr() > 400.0f) {
		sv.pos = sv.targetPos;
	}
	sv.lastUpdateMs = CTimer::GetTimeInMilliseconds();
}

void MP_HandleVehicleClaimPacket(int32 fromPeerId, const uint8 *data, int32 len)
{
	if (len < 5) return;
	int32 netVehId = -1;
	memcpy(&netVehId, data + 1, 4);
	int32 slot = MP_FindVehicleSlotByNetId(netVehId);
	if (slot >= 0) {
		gMp.vehicles[slot].ownerPeerId = fromPeerId;
		gMp.vehicles[slot].lastUpdateMs = CTimer::GetTimeInMilliseconds();
	}
}

bool MP_OnLocalEnterVehicleAttempt(CPed *ped, CVehicle *veh, bool asPassenger)
{
	if (!gMp.initialized || !ped || !veh) return false;
	if (asPassenger) return false;
	int32 slot = MP_FindVehicleSlotByPtr(veh);
	if (slot < 0) slot = MP_TrackLocalVehicle(veh);
	if (slot >= 0) {
		gMp.vehicles[slot].ownerPeerId = gMp.localPeerId;
		uint8 pkt[5];
		pkt[0] = MP_PKT_VEHICLE_CLAIM;
		memcpy(pkt + 1, &gMp.vehicles[slot].netVehId, 4);
		MP_EnqueuePacket(-1, 1, pkt, 5);
	}
	return false;
}

void MP_OnVehicleDestroyed(CVehicle *veh)
{
	if (!gMp.initialized || !veh) return;
	int32 slot = MP_FindVehicleSlotByPtr(veh);
	if (slot >= 0) {
		gMp.vehicles[slot].health = 0.0f;
	}
}

void MP_CleanupRemoteVehicles(void)
{
	CPlayerPed *local = FindPlayerPed();
	CVehicle *localVeh = local && local->bInVehicle ? local->m_pMyVehicle : nil;
	for (int32 i = 0; i < MP_MAX_SYNCED_VEHICLES; i++) {
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
