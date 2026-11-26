#include "mp_core.h"

#ifdef MULTIPLAYER_COOP

#include "CivilianPed.h"
#include "EmergencyPed.h"
#include "CopPed.h"
#include "ModelIndices.h"
#include "ModelInfo.h"
#include "Population.h"
#include "Streaming.h"
#include "WeaponInfo.h"
#include "Weapon.h"
#include "World.h"
#include "Pools.h"
#include "Timer.h"
#include "Game.h"
#include "Pad.h"
#include "Sprite.h"
#include "Font.h"
#include "Text.h"
#include "Camera.h"
#include "SpecialFX.h"
#include "RpAnimBlend.h"
#include "AnimManager.h"
#include "Radar.h"
#include <cstring>
#include <cmath>

int32 MP_ResolvePedModelIndex(int32 skinId)
{
	if (skinId <= 0) return MI_PLAYER;
	if (skinId <= 129 && CModelInfo::GetModelInfo(skinId) && CModelInfo::GetModelInfo(skinId)->GetModelType() == MITYPE_PED)
		return skinId;
	return MI_MALE01;
}

void MP_ApplyLocalSkin(int32 skinModel)
{
	CPlayerPed *ped = FindPlayerPed();
	if (!ped) return;
	// Leave MI_PLAYER intact for base cutscenes/gameplay unless special ped skin chosen
	if (skinModel > 0 && skinModel <= 129) {
		int32 mi = MP_ResolvePedModelIndex(skinModel);
		if (mi != ped->GetModelIndex()) {
			CStreaming::RequestModel(mi, STREAMFLAGS_DONT_REMOVE);
			CStreaming::LoadAllRequestedModels(false);
			if (CStreaming::HasModelLoaded(mi)) {
				ped->DeleteRwObject();
				ped->SetModelIndex(mi);
			}
		}
	}
}

static CPed *MP_SpawnManagedPed(int32 modelIndex, const CVector &pos)
{
	int32 mi = MP_ResolvePedModelIndex(modelIndex);
	if (mi != MI_PLAYER && !CStreaming::HasModelLoaded(mi)) {
		CStreaming::RequestModel(mi, STREAMFLAGS_DEPENDENCY | STREAMFLAGS_DONT_REMOVE);
		CStreaming::LoadAllRequestedModels(false);
	}
	if (!CStreaming::HasModelLoaded(mi)) {
		mi = CStreaming::HasModelLoaded(MI_PLAYER) ? MI_PLAYER : MI_MALE01;
		if (!CStreaming::HasModelLoaded(mi)) {
			CStreaming::RequestModel(mi, STREAMFLAGS_DEPENDENCY | STREAMFLAGS_DONT_REMOVE);
			CStreaming::LoadAllRequestedModels(false);
		}
	}
	if (!CStreaming::HasModelLoaded(mi)) return nil;

	CPed *ped = new CCivilianPed(PEDTYPE_CIVMALE, mi);
	if (!ped) return nil;
	ped->CharCreatedBy = MISSION_CHAR;
	ped->bRespondsToThreats = false;
	ped->bAllowMedicsToReviveMe = false;
	ped->bIsStaticWaitingForCollision = false;
	ped->bStreamingDontDelete = true;
	ped->bRenderPedInCar = true;
	ped->SetPosition(pos);
	ped->m_fHealth = 100.0f;
	ped->bUsesCollision = true;
	CWorld::Add(ped);
	return ped;
}

static void MP_EnsurePedWeapon(CPed *ped, uint8 weaponId, uint8 &cachedWeapon)
{
	if (!ped || weaponId >= WEAPONTYPE_TOTALWEAPONS) return;
	if (cachedWeapon == weaponId) return;
	eWeaponType wt = (eWeaponType)weaponId;
	CWeaponInfo *info = CWeaponInfo::GetWeaponInfo(wt);
	if (info) {
		if (info->m_nModelId > 0 && !CStreaming::HasModelLoaded(info->m_nModelId)) {
			CStreaming::RequestModel(info->m_nModelId, STREAMFLAGS_DONT_REMOVE);
			CStreaming::LoadAllRequestedModels(false);
		}
		if (info->m_nModel2Id > 0 && !CStreaming::HasModelLoaded(info->m_nModel2Id)) {
			CStreaming::RequestModel(info->m_nModel2Id, STREAMFLAGS_DONT_REMOVE);
			CStreaming::LoadAllRequestedModels(false);
		}
	}
	if (wt != WEAPONTYPE_UNARMED) {
		ped->GiveWeapon(wt, 9999, true);
		ped->SetCurrentWeapon(wt);
	} else {
		ped->SetCurrentWeapon(WEAPONTYPE_UNARMED);
	}
	cachedWeapon = weaponId;
}

static void MP_SeatPedInVehicle(CPed *ped, CVehicle *veh, int8 seatIdx)
{
	if (!ped || !veh || !MP_IsPoolVehicleValid(veh)) return;
	if (ped->bInVehicle && ped->m_pMyVehicle == veh) {
		if (seatIdx == 0 && veh->pDriver == ped) return;
		if (seatIdx >= 1 && seatIdx <= (int8)ARRAY_SIZE(veh->pPassengers) && veh->pPassengers[seatIdx - 1] == ped) return;
	}
	if (ped->bInVehicle && ped->m_pMyVehicle && MP_IsPoolVehicleValid(ped->m_pMyVehicle)) {
		if (ped->m_pMyVehicle->pDriver == ped) ped->m_pMyVehicle->RemoveDriver();
		else ped->m_pMyVehicle->RemovePassenger(ped);
		ped->bInVehicle = false;
		ped->m_pMyVehicle = nil;
	}
	if (seatIdx == 0) {
		if (veh->pDriver && veh->pDriver != ped) {
			if (!MP_IsRemotePlayerPed(veh->pDriver) && veh->pDriver != FindPlayerPed())
				veh->RemoveDriver();
		}
		if (!veh->pDriver) {
			veh->pDriver = ped;
			veh->pDriver->RegisterReference((CEntity **)&veh->pDriver);
			ped->m_pMyVehicle = veh;
			ped->m_pMyVehicle->RegisterReference((CEntity **)&ped->m_pMyVehicle);
			ped->bInVehicle = true;
			ped->bUsesCollision = false;
			ped->SetPedState(PED_DRIVING);
			ped->SetPedPositionInCar();
		}
	} else {
		int pIdx = seatIdx - 1;
		int maxPass = veh->IsBike() ? 1 : (int)veh->m_nNumMaxPassengers;
		if (pIdx >= 0 && pIdx < maxPass && pIdx < (int)ARRAY_SIZE(veh->pPassengers)) {
			if (!veh->pPassengers[pIdx]) {
				veh->pPassengers[pIdx] = ped;
				veh->pPassengers[pIdx]->RegisterReference((CEntity **)&veh->pPassengers[pIdx]);
				ped->m_pMyVehicle = veh;
				ped->m_pMyVehicle->RegisterReference((CEntity **)&ped->m_pMyVehicle);
				ped->bInVehicle = true;
				ped->bUsesCollision = false;
				ped->SetPedState(PED_DRIVING);
				ped->SetPedPositionInCar();
			}
		}
	}
}

void MP_UpdateRemotePlayersBefore(float dt)
{
	uint32 now = CTimer::GetTimeInMilliseconds();
	for (int32 i = 0; i < MP_MAX_PLAYERS; i++) {
		MpRemotePlayer &rp = gMp.players[i];
		if (!rp.active) continue;
		if (!rp.hasState) continue;
		if (now - rp.lastUpdateMs > 15000) {
			// Timeout disconnected ghost
			MP_OnPeerDisconnected(rp.peerId);
			continue;
		}
		if (rp.ped && !MP_IsPoolPedValid(rp.ped)) {
			rp.ped = nil;
			rp.blipHandle = -1;
		}
		if (!rp.ped) {
			rp.ped = MP_SpawnManagedPed(rp.skinModel, rp.targetPos);
			if (!rp.ped) continue;
			rp.pos = rp.targetPos;
			rp.heading = rp.targetHeading;
			if (rp.blipHandle < 0) {
				int32 pedRef = CPools::GetPedRef(rp.ped);
				rp.blipHandle = CRadar::SetEntityBlip(BLIP_CHAR, pedRef, 0, BLIP_DISPLAY_BOTH);
				if (rp.blipHandle >= 0) {
					int col = (rp.peerId == 2) ? RADAR_TRACE_GREEN : (rp.peerId == 3) ? RADAR_TRACE_LIGHT_BLUE : RADAR_TRACE_MAGENTA;
					CRadar::ChangeBlipColour(rp.blipHandle, col);
					CRadar::ChangeBlipScale(rp.blipHandle, 2);
				}
			}
		}
		CPed *ped = rp.ped;
		ped->m_area = (int8)rp.area;
		MP_EnsurePedWeapon(ped, rp.weapon, rp.lastWeaponGiven);

		if ((rp.actionFlags & MP_ACT_IN_VEHICLE) && rp.netVehId >= 0) {
			CVehicle *v = MP_GetVehiclePtrByNetId(rp.netVehId);
			if (v && MP_IsPoolVehicleValid(v)) {
				MP_SeatPedInVehicle(ped, v, rp.seatIdx >= 0 ? rp.seatIdx : 0);
				ped->SetPedPositionInCar();
				rp.pos = ped->GetPosition();
				continue;
			}
		}
		if (ped->bInVehicle && ped->m_pMyVehicle && MP_IsPoolVehicleValid(ped->m_pMyVehicle)) {
			CVehicle *oldVeh = ped->m_pMyVehicle;
			if (oldVeh->pDriver == ped) oldVeh->RemoveDriver();
			else oldVeh->RemovePassenger(ped);
			ped->bInVehicle = false;
			ped->m_pMyVehicle = nil;
			ped->bUsesCollision = true;
			ped->SetPedState(PED_IDLE);
		}

		float dtExtrap = Min((float)(now - rp.lastUpdateMs) * 0.001f, 0.1f);
		CVector predPos = rp.targetPos + rp.vel * (dtExtrap * 50.0f);
		float blend = Clamp(dt * 14.0f, 0.12f, 0.75f);
		rp.pos = rp.pos + (predPos - rp.pos) * blend;
		rp.heading = MP_LerpAngle(rp.heading, rp.targetHeading, blend);

		if ((rp.pos - predPos).MagnitudeSqr() > 25.0f) {
			rp.pos = predPos;
		}

		ped->bUsesCollision = true;
		ped->SetPosition(rp.pos);
		ped->m_vecMoveSpeed = rp.vel;
		ped->m_fRotationCur = rp.heading;
		ped->m_fRotationDest = rp.heading;
		ped->SetHeading(rp.heading);
		ped->UpdateRwFrame();
		ped->m_fHealth = Clamp((float)rp.health, 1.0f, 100.0f);
		ped->m_fArmour = (float)rp.armor;

		if (rp.actionFlags & MP_ACT_DEAD) {
			if (ped->m_nPedState != PED_DEAD) ped->SetDead();
		} else {
			if (ped->m_nPedState == PED_DEAD) ped->m_nPedState = PED_IDLE;
			float speed2d = rp.vel.Magnitude2D();
			ped->bIsDucking = (rp.actionFlags & MP_ACT_DUCKING) != 0;
			if (speed2d > 0.008f) {
				ped->SetMoveState((rp.actionFlags & MP_ACT_SPRINTING) ? PEDMOVE_SPRINT : PEDMOVE_RUN);
			} else {
				ped->SetMoveState(PEDMOVE_STILL);
			}
			ped->SetMoveAnim();
		}
	}
}

void MP_UpdateRemotePlayersAfter(float)
{
	for (int32 i = 0; i < MP_MAX_PLAYERS; i++) {
		MpRemotePlayer &rp = gMp.players[i];
		if (!rp.active || !rp.hasState || !rp.ped || !MP_IsPoolPedValid(rp.ped)) continue;
		if (rp.ped->bInVehicle && rp.ped->m_pMyVehicle && MP_IsPoolVehicleValid(rp.ped->m_pMyVehicle)) {
			rp.ped->SetPedPositionInCar();
		} else if (!(rp.actionFlags & MP_ACT_DEAD)) {
			rp.ped->SetPosition(rp.pos);
			rp.ped->m_fRotationCur = rp.heading;
			rp.ped->m_fRotationDest = rp.heading;
			rp.ped->SetHeading(rp.heading);
			rp.ped->UpdateRwFrame();
		}
	}
}

void MP_SendLocalPlayerState(void)
{
	uint32 now = CTimer::GetTimeInMilliseconds();
	if (now - gMp.lastPlayerSendMs < 40) return;
	gMp.lastPlayerSendMs = now;

	CPlayerPed *ped = FindPlayerPed();
	if (!ped) return;

	uint8 pkt[96];
	int32 p = 0;
	pkt[p++] = MP_PKT_PLAYER_STATE;

	CVector pos = ped->GetPosition();
	CVector vel = ped->bInVehicle && ped->m_pMyVehicle ? ped->m_pMyVehicle->m_vecMoveSpeed : ped->m_vecMoveSpeed;
	memcpy(pkt + p, &pos.x, 4); p += 4;
	memcpy(pkt + p, &pos.y, 4); p += 4;
	memcpy(pkt + p, &pos.z, 4); p += 4;
	memcpy(pkt + p, &vel.x, 4); p += 4;
	memcpy(pkt + p, &vel.y, 4); p += 4;
	memcpy(pkt + p, &vel.z, 4); p += 4;

	uint16 hdg16 = MP_EncodeAngle16(ped->m_fRotationCur);
	memcpy(pkt + p, &hdg16, 2); p += 2;
	uint8 hp = (uint8)Clamp((int32)ped->m_fHealth, 0, 255);
	uint8 arm = (uint8)Clamp((int32)ped->m_fArmour, 0, 255);
	uint8 wpn = (uint8)ped->GetWeapon()->m_eWeaponType;
	uint16 flags = 0;
	if (ped->bInVehicle && ped->m_pMyVehicle) flags |= MP_ACT_IN_VEHICLE;
	if (ped->bIsDucking) flags |= MP_ACT_DUCKING;
	if (ped->m_nPedState == PED_DEAD || ped->m_nPedState == PED_DIE) flags |= MP_ACT_DEAD;
	if (ped->bIsShooting) flags |= MP_ACT_FIRING;
	if (ped->m_nMoveState == PEDMOVE_SPRINT) flags |= MP_ACT_SPRINTING;

	int32 netVehId = -1;
	int8 seatIdx = -1;
	if (ped->bInVehicle && ped->m_pMyVehicle) {
		int32 slot = MP_FindVehicleSlotByPtr(ped->m_pMyVehicle);
		if (slot >= 0) {
			netVehId = gMp.vehicles[slot].netVehId;
			if (ped->m_pMyVehicle->pDriver == ped) {
				seatIdx = 0;
			} else {
				for (int s = 0; s < (int)ARRAY_SIZE(ped->m_pMyVehicle->pPassengers); s++) {
					if (ped->m_pMyVehicle->pPassengers[s] == ped) {
						seatIdx = (int8)(s + 1);
						break;
					}
				}
			}
			if (seatIdx > 0) flags |= MP_ACT_PASSENGER;
		}
	}

	pkt[p++] = hp;
	pkt[p++] = arm;
	pkt[p++] = wpn;
	memcpy(pkt + p, &flags, 2); p += 2;
	memcpy(pkt + p, &netVehId, 4); p += 4;
	pkt[p++] = (uint8)seatIdx;
	pkt[p++] = (uint8)Clamp(CGame::currArea, 0, 255);
	uint16 skin16 = (uint16)Clamp(gMp.localSkinModel, 0, 65535);
	memcpy(pkt + p, &skin16, 2); p += 2;

	MP_EnqueuePacket(-1, 0, pkt, p);
}

void MP_HandlePlayerStatePacket(int32 fromPeerId, const uint8 *data, int32 len)
{
	if (len < 36) return;
	MpRemotePlayer *rp = MP_GetOrCreateRemotePlayer(fromPeerId);
	if (!rp) return;

	int32 p = 1;
	memcpy(&rp->targetPos.x, data + p, 4); p += 4;
	memcpy(&rp->targetPos.y, data + p, 4); p += 4;
	memcpy(&rp->targetPos.z, data + p, 4); p += 4;
	memcpy(&rp->vel.x, data + p, 4); p += 4;
	memcpy(&rp->vel.y, data + p, 4); p += 4;
	memcpy(&rp->vel.z, data + p, 4); p += 4;

	uint16 hdg16 = 0;
	memcpy(&hdg16, data + p, 2); p += 2;
	rp->targetHeading = MP_DecodeAngle16(hdg16);

	rp->health = data[p++];
	rp->armor = data[p++];
	rp->weapon = data[p++];
	memcpy(&rp->actionFlags, data + p, 2); p += 2;
	memcpy(&rp->netVehId, data + p, 4); p += 4;
	rp->seatIdx = (int8)data[p++];
	rp->area = data[p++];
	uint16 skin16 = 0;
	memcpy(&skin16, data + p, 2); p += 2;
	if (skin16 != (uint16)rp->skinModel && rp->ped && MP_IsPoolPedValid(rp->ped) && !rp->ped->bInVehicle) {
		int32 nextMi = MP_ResolvePedModelIndex(skin16);
		rp->skinModel = skin16;
		if (nextMi != rp->ped->GetModelIndex()) {
			if (nextMi != MI_PLAYER) {
				CStreaming::RequestModel(nextMi, STREAMFLAGS_DEPENDENCY | STREAMFLAGS_DONT_REMOVE);
				CStreaming::LoadAllRequestedModels(false);
			}
			if (CStreaming::HasModelLoaded(nextMi)) {
				rp->ped->DeleteRwObject();
				rp->ped->SetModelIndex(nextMi);
			}
		}
	} else {
		rp->skinModel = skin16;
	}
	if (!rp->hasState || (rp->pos - rp->targetPos).MagnitudeSqr() > 400.0f) {
		rp->pos = rp->targetPos;
		rp->heading = rp->targetHeading;
	}
	rp->hasState = true;
	rp->lastUpdateMs = CTimer::GetTimeInMilliseconds();
}

void MP_HandlePlayerMetaPacket(int32 fromPeerId, const uint8 *data, int32 len)
{
	if (len < 1 + 4 + MP_MAX_PLAYER_NAME) return;
	MpRemotePlayer *rp = MP_GetOrCreateRemotePlayer(fromPeerId);
	if (!rp) return;
	int32 skin = 0;
	memcpy(&skin, data + 1, 4);
	char nameBuf[MP_MAX_PLAYER_NAME];
	memcpy(nameBuf, data + 5, MP_MAX_PLAYER_NAME);
	nameBuf[MP_MAX_PLAYER_NAME - 1] = '\0';
	MP_SetPlayerInfo(fromPeerId, nameBuf, skin);
}

void MP_ProcessPassengerEntry(void)
{
	CPlayerPed *local = FindPlayerPed();
	if (!local || local->bInVehicle || local->m_nPedState == PED_DEAD) return;

	// Key 'G' for entering remote peer's vehicle (or bike pillion) as passenger
	bool gJustDown = (CPad::NewKeyState.VK_KEYS['G'] && !CPad::OldKeyState.VK_KEYS['G']);
	if (!gJustDown) return;

	CVector lpos = local->GetPosition();
	CVehicle *bestVeh = nil;
	float bestDistSq = 36.0f; // 6m radius
	for (int32 i = 0; i < MP_MAX_SYNCED_VEHICLES; i++) {
		MpSyncedVehicle &sv = gMp.vehicles[i];
		if (!sv.active || !sv.veh) continue;
		float dSq = (sv.veh->GetPosition() - lpos).MagnitudeSqr();
		if (dSq < bestDistSq) {
			bestDistSq = dSq;
			bestVeh = sv.veh;
		}
	}
	if (!bestVeh) return;

	int maxPass = bestVeh->IsBike() ? 1 : (int)bestVeh->m_nNumMaxPassengers;
	for (int s = 0; s < maxPass && s < (int)ARRAY_SIZE(bestVeh->pPassengers); s++) {
		if (!bestVeh->pPassengers[s]) {
			local->SetObjective(OBJECTIVE_ENTER_CAR_AS_PASSENGER, bestVeh);
			local->m_vehDoor = s + 1;
			break;
		}
	}
}

void MP_ProcessPassengerDriveBy(CVehicle *veh)
{
	if (!veh) return;
	for (int i = 0; i < (int)ARRAY_SIZE(veh->pPassengers); i++) {
		CPed *pass = veh->pPassengers[i];
		if (!pass) continue;
		if (pass == FindPlayerPed() && CPad::GetPad(0)->GetWeapon()) {
			CWeapon *w = pass->GetWeapon();
			if (w && w->m_eWeaponType != WEAPONTYPE_UNARMED && w->m_nAmmoTotal > 0) {
				CVector src = pass->GetPosition() + CVector(0, 0, 0.5f);
				CVector dir = TheCamera.GetForward();
				dir.Normalise();
				w->Update(pass->m_audioEntityId, nil);
			}
		}
	}
}

void MP_TickAmbientPedsBefore(float)
{
	if (!gMp.initialized || gMp.isHost) return;
	uint32 now = CTimer::GetTimeInMilliseconds();
	for (int32 i = 0; i < MP_MAX_SYNCED_PEDS; i++) {
		MpSyncedPed &sp = gMp.peds[i];
		if (!sp.active) continue;
		if (sp.ped && !MP_IsPoolPedValid(sp.ped)) {
			sp.ped = nil;
		}
		if (now - sp.lastUpdateMs > 8000) {
			if (sp.ped) {
				CWorld::Remove(sp.ped);
				delete sp.ped;
				sp.ped = nil;
			}
			sp.active = false;
			continue;
		}
		if (!sp.ped) {
			sp.ped = MP_SpawnManagedPed(sp.modelIndex, sp.targetPos);
			if (!sp.ped) continue;
		}
		sp.ped->m_area = (int8)sp.area;
		sp.ped->SetPosition(sp.targetPos);
		sp.ped->m_vecMoveSpeed = sp.vel;
		sp.ped->SetHeading(sp.targetHeading);
		sp.ped->m_fHealth = (float)sp.health;
	}
}

void MP_TickAmbientPedsAfter(float)
{
	if (!gMp.initialized || !gMp.isHost) return;
	uint32 now = CTimer::GetTimeInMilliseconds();
	if (now - gMp.lastPedSendMs < 150) return;
	gMp.lastPedSendMs = now;

	CPed *local = FindPlayerPed();
	CVector focus = local ? local->GetPosition() : CVector(0, 0, 0);
	CPool<CPed, CPlayerPed> *pool = CPools::GetPedPool();
	int32 sent = 0;
	for (int32 idx = 0; idx < pool->GetSize() && sent < 8; idx++) {
		CPed *ped = pool->GetSlot(idx);
		if (!ped || ped == local || MP_IsRemotePlayerPed(ped) || ped->bInVehicle) continue;
		if ((ped->GetPosition() - focus).Magnitude2D() > 55.0f && !MP_IsNearAnyPlayer2D(ped->GetPosition(), 45.0f))
			continue;
		uint8 pkt[40];
		int32 p = 0;
		pkt[p++] = MP_PKT_PED_SYNC;
		memcpy(pkt + p, &idx, 4); p += 4;
		uint16 mi = (uint16)ped->GetModelIndex();
		memcpy(pkt + p, &mi, 2); p += 2;
		CVector pos = ped->GetPosition();
		memcpy(pkt + p, &pos.x, 4); p += 4;
		memcpy(pkt + p, &pos.y, 4); p += 4;
		memcpy(pkt + p, &pos.z, 4); p += 4;
		uint16 h16 = MP_EncodeAngle16(ped->m_fRotationCur);
		memcpy(pkt + p, &h16, 2); p += 2;
		pkt[p++] = (uint8)Clamp((int32)ped->m_fHealth, 0, 255);
		pkt[p++] = (uint8)ped->GetWeapon()->m_eWeaponType;
		pkt[p++] = (uint8)Clamp((int32)ped->m_area, 0, 255);
		MP_EnqueuePacket(-1, 0, pkt, p);
		sent++;
	}
}

void MP_HandlePedSyncPacket(int32 fromPeerId, const uint8 *data, int32 len)
{
	if (gMp.isHost || len < 24) return;
	int32 p = 1;
	int32 netId = 0;
	uint16 mi = MI_MALE01;
	CVector pos;
	uint16 h16 = 0;
	memcpy(&netId, data + p, 4); p += 4;
	memcpy(&mi, data + p, 2); p += 2;
	memcpy(&pos.x, data + p, 4); p += 4;
	memcpy(&pos.y, data + p, 4); p += 4;
	memcpy(&pos.z, data + p, 4); p += 4;
	memcpy(&h16, data + p, 2); p += 2;
	uint8 hp = data[p++];
	uint8 wpn = data[p++];
	uint8 area = data[p++];

	int32 slot = -1, freeSlot = -1;
	for (int32 i = 0; i < MP_MAX_SYNCED_PEDS; i++) {
		if (gMp.peds[i].active && gMp.peds[i].netPedId == netId) {
			slot = i;
			break;
		}
		if (!gMp.peds[i].active && freeSlot < 0) freeSlot = i;
	}
	if (slot < 0) slot = freeSlot;
	if (slot < 0) return;
	MpSyncedPed &sp = gMp.peds[slot];
	sp.active = true;
	sp.netPedId = netId;
	sp.ownerPeerId = fromPeerId;
	sp.modelIndex = mi;
	sp.targetPos = pos;
	sp.targetHeading = MP_DecodeAngle16(h16);
	sp.health = hp;
	sp.weapon = wpn;
	sp.area = area;
	sp.lastUpdateMs = CTimer::GetTimeInMilliseconds();
}

void MP_RenderNameTags(void)
{
	if (!gMp.initialized) return;
	for (int32 i = 0; i < MP_MAX_PLAYERS; i++) {
		const MpRemotePlayer &rp = gMp.players[i];
		if (!rp.active || !rp.hasState || !rp.ped || !MP_IsPoolPedValid(rp.ped)) continue;
		if (rp.area != (uint8)CGame::currArea) continue;

		CVector headPos = rp.ped->GetPosition() + CVector(0.0f, 0.0f, 1.25f);
		RwV3d inPt = { headPos.x, headPos.y, headPos.z };
		RwV3d outPt;
		float w = 0.0f, h = 0.0f;
		if (!CSprite::CalcScreenCoors(inPt, &outPt, &w, &h, true)) continue;
		if (outPt.z <= 0.1f || outPt.z > 80.0f) continue;

		char tag[64];
		snprintf(tag, sizeof(tag), "%s [%d HP]", rp.name, (int)rp.health);
		wchar wtag[64];
		AsciiToUnicode(tag, wtag);

		CFont::SetScale(0.45f, 0.85f);
		CFont::SetJustifyOff();
		CFont::SetCentreOn();
		CFont::SetPropOn();
		CFont::SetFontStyle(FONT_HEADING);
		CFont::SetColor(CRGBA(0, 229, 255, 255));
		CFont::SetDropShadowPosition(1);
		CFont::SetDropColor(CRGBA(255, 42, 133, 220));
		CFont::PrintString(outPt.x, outPt.y, wtag);
	}
}

void MP_CleanupRemotePlayers(void)
{
	for (int32 i = 0; i < MP_MAX_PLAYERS; i++) {
		if (gMp.players[i].blipHandle >= 0) {
			CRadar::ClearBlip(gMp.players[i].blipHandle);
			gMp.players[i].blipHandle = -1;
		}
		if (gMp.players[i].ped && MP_IsPoolPedValid(gMp.players[i].ped)) {
			if (gMp.players[i].ped->bInVehicle && gMp.players[i].ped->m_pMyVehicle && MP_IsPoolVehicleValid(gMp.players[i].ped->m_pMyVehicle)) {
				if (gMp.players[i].ped->m_pMyVehicle->pDriver == gMp.players[i].ped) gMp.players[i].ped->m_pMyVehicle->RemoveDriver();
				else gMp.players[i].ped->m_pMyVehicle->RemovePassenger(gMp.players[i].ped);
			}
			CWorld::Remove(gMp.players[i].ped);
			delete gMp.players[i].ped;
		}
		gMp.players[i].ped = nil;
		gMp.players[i].hasState = false;
		gMp.players[i].active = false;
	}
}

void MP_CleanupRemotePeds(void)
{
	for (int32 i = 0; i < MP_MAX_SYNCED_PEDS; i++) {
		if (gMp.peds[i].ped && MP_IsPoolPedValid(gMp.peds[i].ped)) {
			CWorld::Remove(gMp.peds[i].ped);
			delete gMp.peds[i].ped;
		}
		gMp.peds[i].ped = nil;
		gMp.peds[i].active = false;
	}
}

#endif // MULTIPLAYER_COOP
