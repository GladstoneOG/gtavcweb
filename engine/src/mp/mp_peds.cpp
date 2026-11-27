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
#include "Particle.h"
#include "DMAudio.h"
#include "soundlist.h"
#include "RpAnimBlend.h"
#include "AnimManager.h"
#include "AnimBlendAssociation.h"
#include "AnimBlendAssocGroup.h"
#include "Radar.h"
#include <cstring>
#include <cmath>

static bool MP_IsLocomotionAnimId(int32 animId)
{
	switch (animId) {
	case ANIM_STD_WALK:
	case ANIM_STD_RUN:
	case ANIM_STD_RUNFAST:
	case ANIM_STD_IDLE:
	case ANIM_STD_IDLE_BIGGUN:
	case ANIM_STD_IDLE_HBHB:
	case ANIM_STD_IDLE_TIRED:
	case ANIM_STD_RUNSTOP1:
	case ANIM_STD_RUNSTOP2:
	case ANIM_STD_JUMP_LAUNCH:
	case ANIM_STD_JUMP_GLIDE:
	case ANIM_STD_JUMP_LAND:
	case ANIM_STD_FALL:
	case ANIM_STD_FALL_GLIDE:
	case ANIM_STD_FALL_LAND:
		return true;
	default:
		return false;
	}
}

int32 MP_FindRemotePlayerPeerByPed(const CPed *ped)
{
	if (!ped) return -1;
	for (int32 i = 0; i < MP_MAX_PLAYERS; i++) {
		if (gMp.players[i].active && gMp.players[i].ped == ped)
			return gMp.players[i].peerId;
	}
	return -1;
}

int32 MP_FindSyncedPedNetIdByPtr(const CPed *ped)
{
	if (!ped) return -1;
	for (int32 i = 0; i < MP_MAX_SYNCED_PEDS; i++) {
		if (gMp.peds[i].active && gMp.peds[i].ped == ped)
			return gMp.peds[i].netPedId;
	}
	return -1;
}

static uint8 MP_EncodeDoorCode(uint32 doorNode)
{
	switch (doorNode) {
	case CAR_DOOR_LF:   return 1;
	case CAR_DOOR_RF:   return 2;
	case CAR_DOOR_LR:   return 3;
	case CAR_DOOR_RR:   return 4;
	case CAR_WINDSCREEN:return 5;
	case CAR_BOOT:      return 6;
	case CAR_BUMP_REAR: return 7;
	default:            return 0;
	}
}

static uint32 MP_DecodeDoorCode(uint8 code)
{
	switch (code) {
	case 1: return CAR_DOOR_LF;
	case 2: return CAR_DOOR_RF;
	case 3: return CAR_DOOR_LR;
	case 4: return CAR_DOOR_RR;
	case 5: return CAR_WINDSCREEN;
	case 6: return CAR_BOOT;
	case 7: return CAR_BUMP_REAR;
	default: return 0;
	}
}

static bool MP_IsCarSwingDoorNode(uint32 doorNode)
{
	return doorNode == CAR_DOOR_LF || doorNode == CAR_DOOR_RF ||
	       doorNode == CAR_DOOR_LR || doorNode == CAR_DOOR_RR;
}

static bool MP_IsDoorCloseAnim(int32 animId)
{
	return animId == ANIM_STD_CAR_CLOSE_DOOR_LHS ||
	       animId == ANIM_STD_CAR_CLOSE_DOOR_LO_LHS ||
	       animId == ANIM_STD_CAR_CLOSE_DOOR_RHS ||
	       animId == ANIM_STD_CAR_CLOSE_DOOR_LO_RHS ||
	       animId == ANIM_STD_VAN_CLOSE_DOOR_REAR_LHS ||
	       animId == ANIM_STD_VAN_CLOSE_DOOR_REAR_RHS;
}

static CAnimBlendAssociation *MP_FindLiveVehAnim(CPed *ped)
{
	if (!ped || !ped->m_pVehicleAnim || !ped->GetClump()) return nil;
	for (CAnimBlendAssociation *a = RpAnimBlendClumpGetFirstAssociation(ped->GetClump()); a; a = RpAnimBlendGetNextAssociation(a)) {
		if (a == ped->m_pVehicleAnim) return a;
	}
	return nil;
}

static bool MP_EnsureAnimGroupLoaded(AssocGroupId groupId)
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

static void MP_ExpectedSeatedAnim(CVehicle *veh, int8 seatIdx, AssocGroupId &outGrp, AnimationId &outAnim)
{
	outGrp = ASSOCGRP_STD;
	if (!veh) { outAnim = ANIM_STD_CAR_SIT; return; }
	if (veh->IsBoat()) {
		outAnim = (veh->pHandling && (veh->pHandling->Flags & HANDLING_SIT_IN_BOAT)) ? ANIM_STD_CAR_SIT : ANIM_STD_BOAT_DRIVE;
	} else if (veh->IsBike()) {
		outGrp = (AssocGroupId)((CBike *)veh)->m_bikeAnimType;
		outAnim = (seatIdx == 0) ? ANIM_BIKE_RIDE : ANIM_BIKE_RIDE_P;
	} else if (seatIdx == 0) {
		outAnim = veh->bLowVehicle ? ANIM_STD_CAR_SIT_LO : ANIM_STD_CAR_SIT;
	} else {
		outAnim = veh->bLowVehicle ? ANIM_STD_CAR_SIT_P_LO : ANIM_STD_CAR_SIT_P;
	}
}

static void MP_CloseRemoteLastDoor(MpRemotePlayer &rp)
{
	if (rp.lastDoorVehId < 0 || rp.lastDoorCode == 0) return;
	uint32 doorComp = MP_DecodeDoorCode(rp.lastDoorCode);
	if (MP_IsCarSwingDoorNode(doorComp)) {
		CVehicle *lv = MP_GetVehiclePtrByNetId(rp.lastDoorVehId);
		if (lv && lv->IsCar() && MP_IsPoolVehicleValid(lv)) {
			lv->ProcessOpenDoor(doorComp, ANIM_STD_NUM, 0.0f);
		}
	}
	rp.lastDoorVehId = -1;
	rp.lastDoorCode = 0;
}

static void MP_DetachPedFromVehicle(CPed *ped)
{
	if (!ped) return;
	if (ped->bInVehicle && ped->m_pMyVehicle && MP_IsPoolVehicleValid(ped->m_pMyVehicle)) {
		if (ped->m_pMyVehicle->pDriver == ped) ped->m_pMyVehicle->RemoveDriver();
		else ped->m_pMyVehicle->RemovePassenger(ped);
	}
	ped->RemoveInCarAnims();
	ped->bInVehicle = false;
	ped->m_pMyVehicle = nil;
	ped->m_pVehicleAnim = nil;
}

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

static CPed *MP_SpawnManagedPed(int32 modelIndex, const CVector &pos, bool isPlayerClone)
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

	ePedType ptype = isPlayerClone ? PEDTYPE_CIVMALE : PEDTYPE_CIVMALE;
	CPed *ped = new CCivilianPed(ptype, mi);
	if (!ped) return nil;
	ped->CharCreatedBy = MISSION_CHAR;
	ped->bRespondsToThreats = false;
	ped->bAllowMedicsToReviveMe = false;
	ped->bIsStaticWaitingForCollision = false;
	ped->bStreamingDontDelete = true;
	ped->bRenderPedInCar = true;
	if (mi == MI_PLAYER || isPlayerClone) {
		ped->m_animGroup = ASSOCGRP_PLAYER;
	} else {
		CPedModelInfo *pmi = (CPedModelInfo *)CModelInfo::GetModelInfo(mi);
		if (pmi && pmi->GetModelType() == MITYPE_PED)
			ped->m_animGroup = (AssocGroupId)pmi->m_animGroup;
	}
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

static void MP_EnsureSeatedAnim(CPed *ped, CVehicle *veh, int8 seatIdx)
{
	if (!ped || !veh || !ped->GetClump()) return;
	AssocGroupId expGrp = ASSOCGRP_STD;
	AnimationId expAnim = ANIM_STD_CAR_SIT;
	MP_ExpectedSeatedAnim(veh, seatIdx, expGrp, expAnim);
	MP_EnsureAnimGroupLoaded(expGrp);

	CAnimBlendAssociation *sit = RpAnimBlendClumpGetAssociation(ped->GetClump(), expAnim);
	if (!sit || sit->blendDelta < 0.0f) {
		for (CAnimBlendAssociation *a = RpAnimBlendClumpGetFirstAssociation(ped->GetClump()); a; a = RpAnimBlendGetNextAssociation(a)) {
			if (a->animId != expAnim) {
				a->blendDelta = -1000.0f;
				a->flags |= ASSOC_DELETEFADEDOUT;
			}
		}
		ped->AddInCarAnims(veh, seatIdx == 0);
		ped->StopNonPartialAnims();
	}
	ped->bChangedSeat = false;
	ped->bRenderPedInCar = !veh->bIsBus;
}

static void MP_SeatPedInVehicle(CPed *ped, CVehicle *veh, int8 seatIdx)
{
	if (!ped || !veh || !MP_IsPoolVehicleValid(veh)) return;
	bool alreadySeatedHere = false;
	if (ped->bInVehicle && ped->m_pMyVehicle == veh) {
		if (seatIdx == 0 && veh->pDriver == ped) alreadySeatedHere = true;
		else if (seatIdx >= 1 && seatIdx <= (int8)ARRAY_SIZE(veh->pPassengers) && veh->pPassengers[seatIdx - 1] == ped) alreadySeatedHere = true;
	}
	if (!alreadySeatedHere) {
		if (ped->bInVehicle && ped->m_pMyVehicle && MP_IsPoolVehicleValid(ped->m_pMyVehicle)) {
			if (ped->m_pMyVehicle->pDriver == ped) ped->m_pMyVehicle->RemoveDriver();
			else ped->m_pMyVehicle->RemovePassenger(ped);
			ped->RemoveInCarAnims();
			ped->bInVehicle = false;
			ped->m_pMyVehicle = nil;
		}
		if (seatIdx == 0) {
			if (veh->pDriver && veh->pDriver != ped && !MP_IsRemotePlayerPed(veh->pDriver) && veh->pDriver != FindPlayerPed()) {
				CPed *oldDrv = veh->pDriver;
				veh->RemoveDriver();
				if (MP_IsPoolPedValid(oldDrv) && oldDrv->CharCreatedBy == MISSION_CHAR) {
					oldDrv->RemoveInCarAnims();
					oldDrv->bInVehicle = false;
					oldDrv->m_pMyVehicle = nil;
					CWorld::Remove(oldDrv);
					delete oldDrv;
				}
			}
			if (!veh->pDriver) {
				veh->pDriver = ped;
				veh->pDriver->RegisterReference((CEntity **)&veh->pDriver);
				ped->m_pMyVehicle = veh;
				ped->m_pMyVehicle->RegisterReference((CEntity **)&ped->m_pMyVehicle);
				ped->bInVehicle = true;
				ped->bUsesCollision = false;
				ped->bChangedSeat = false;
				ped->SetPedState(PED_DRIVING);
				ped->RemoveWeaponWhenEnteringVehicle();
			}
		} else {
			int pIdx = seatIdx - 1;
			int maxPass = veh->IsBike() ? 1 : (int)veh->m_nNumMaxPassengers;
			if (pIdx >= 0 && pIdx < maxPass && pIdx < (int)ARRAY_SIZE(veh->pPassengers)) {
				if (veh->pPassengers[pIdx] && veh->pPassengers[pIdx] != ped && !MP_IsRemotePlayerPed(veh->pPassengers[pIdx]) && veh->pPassengers[pIdx] != FindPlayerPed()) {
					veh->RemovePassenger(veh->pPassengers[pIdx]);
				}
				if (!veh->pPassengers[pIdx]) {
					veh->pPassengers[pIdx] = ped;
					veh->pPassengers[pIdx]->RegisterReference((CEntity **)&veh->pPassengers[pIdx]);
					ped->m_pMyVehicle = veh;
					ped->m_pMyVehicle->RegisterReference((CEntity **)&ped->m_pMyVehicle);
					ped->bInVehicle = true;
					ped->bUsesCollision = false;
					ped->bChangedSeat = false;
					ped->SetPedState(PED_DRIVING);
					ped->RemoveWeaponWhenEnteringVehicle();
				}
			}
		}
	}
	if (ped->bInVehicle && ped->m_pMyVehicle == veh) {
		MP_EnsureSeatedAnim(ped, veh, seatIdx);
		ped->SetPedPositionInCar();
		ped->UpdateRwFrame();
	}
}

static void MP_FadeOutVehicleDoorAnims(CPed *ped, int32 keepAnimId)
{
	if (!ped || !ped->GetClump()) return;
	for (CAnimBlendAssociation *a = RpAnimBlendClumpGetFirstAssociation(ped->GetClump()); a; a = RpAnimBlendGetNextAssociation(a)) {
		bool isSeatedAnim = (keepAnimId < 0) && (a->animId >= ANIM_STD_CAR_SIT && a->animId <= ANIM_STD_BOAT_DRIVE);
		if (isSeatedAnim ||
		    (a->animId >= ANIM_STD_QUICKJACK && a->animId <= ANIM_STD_CAR_CLOSE_DOOR_LO_RHS) ||
		    a->animId == ANIM_STD_CAR_CLOSE_RHS ||
		    (a->animId >= ANIM_STD_VAN_OPEN_DOOR_REAR_LHS && a->animId <= ANIM_STD_BIKE_PULLUP_RHS) ||
		    (a->groupId >= ASSOCGRP_BIKE_STANDARD && a->groupId <= ASSOCGRP_BIKE_DIRT)) {
			if (a->animId != keepAnimId) {
				a->blendDelta = -1000.0f;
				a->flags |= ASSOC_DELETEFADEDOUT;
			}
		}
	}
}

void MP_UpdateRemotePlayersBefore(float dt)
{
	uint32 now = CTimer::GetTimeInMilliseconds();
	for (int32 i = 0; i < MP_MAX_PLAYERS; i++) {
		MpRemotePlayer &rp = gMp.players[i];
		if (!rp.active || !rp.hasState) continue;
		if (now - rp.lastUpdateMs > 15000) {
			MP_CloseRemoteLastDoor(rp);
			MP_OnPeerDisconnected(rp.peerId);
			continue;
		}
		if (rp.ped && !MP_IsPoolPedValid(rp.ped)) {
			rp.ped = nil;
			rp.blipHandle = -1;
		}
		if (!rp.ped) {
			rp.ped = MP_SpawnManagedPed(rp.skinModel, rp.targetPos, true);
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
		ped->m_fHealth = Clamp((float)rp.health, 1.0f, 100.0f);
		ped->m_fArmour = (float)rp.armor;

		bool isTransition = (rp.actionFlags & (MP_ACT_ENTERING_VEH | MP_ACT_EXITING_VEH)) != 0;
		bool isSeated = (rp.actionFlags & MP_ACT_IN_VEHICLE) != 0 && !isTransition;

		// Case 1: Remote player is entering, carjacking, or exiting a vehicle (door + anim sync!)
		if (isTransition && rp.netVehId >= 0) {
			if (ped->bInVehicle) {
				MP_DetachPedFromVehicle(ped);
				ped->SetPedState(PED_IDLE);
			}
			ped->bUsesCollision = false;
			ped->bIsInTheAir = false;
			ped->bRenderPedInCar = true;

			float blend = Clamp(dt * 24.0f, 0.30f, 0.85f);
			if ((rp.pos - rp.targetPos).MagnitudeSqr() > 16.0f) {
				rp.pos = rp.targetPos;
			} else {
				rp.pos = rp.pos + (rp.targetPos - rp.pos) * blend;
			}
			rp.heading = MP_LerpAngle(rp.heading, rp.targetHeading, blend);

			ped->GetMatrix().SetRotateZ(rp.heading);
			ped->SetPosition(rp.pos);
			ped->m_vecMoveSpeed = rp.vel;
			ped->m_fRotationCur = rp.heading;
			ped->m_fRotationDest = rp.heading;
			ped->UpdateRwFrame();

			if (rp.animId >= 0 && rp.animGroup < NUM_ANIM_ASSOC_GROUPS && MP_EnsureAnimGroupLoaded((AssocGroupId)rp.animGroup)) {
				ped->StopNonPartialAnims();
				MP_FadeOutVehicleDoorAnims(ped, rp.animId);
				CAnimBlendAssociation *cur = RpAnimBlendClumpGetAssociation(ped->GetClump(), rp.animId);
				if (!cur) {
					cur = CAnimManager::AddAnimation(ped->GetClump(), (AssocGroupId)rp.animGroup, (AnimationId)rp.animId);
				}
				if (cur) {
					cur->callbackType = CAnimBlendAssociation::CB_NONE;
					cur->SetBlend(1.0f, 8.0f);
					cur->SetRun();
					if (fabsf(cur->currentTime - rp.animTime) > 0.08f) {
						cur->SetCurrentTime(rp.animTime);
					}
					ped->m_pVehicleAnim = cur;
				}
			}

			CVehicle *v = MP_GetVehiclePtrByNetId(rp.netVehId);
			uint32 doorComp = MP_DecodeDoorCode(rp.vehDoorCode);
			if (v && v->IsCar() && MP_IsCarSwingDoorNode(doorComp) && rp.animId >= 0) {
				v->ProcessOpenDoor(doorComp, (uint32)rp.animId, rp.animTime);
				rp.lastDoorVehId = rp.netVehId;
				rp.lastDoorCode = rp.vehDoorCode;
			}
			continue;
		}

		// Case 2: Remote player is seated inside a vehicle (driver or passenger)
		if (isSeated && rp.netVehId >= 0) {
			CVehicle *v = MP_GetVehiclePtrByNetId(rp.netVehId);
			if (v && MP_IsPoolVehicleValid(v)) {
				int8 seat = rp.seatIdx >= 0 ? rp.seatIdx : 0;
				MP_SeatPedInVehicle(ped, v, seat);
				uint32 doorComp = MP_DecodeDoorCode(rp.vehDoorCode);
				if (v->IsCar() && MP_IsCarSwingDoorNode(doorComp) && MP_IsDoorCloseAnim(rp.animId)) {
					v->ProcessOpenDoor(doorComp, (uint32)rp.animId, rp.animTime);
					rp.lastDoorVehId = rp.netVehId;
					rp.lastDoorCode = rp.vehDoorCode;
				} else {
					MP_CloseRemoteLastDoor(rp);
				}
				rp.pos = ped->GetPosition();
				rp.heading = ped->m_fRotationCur;
				continue;
			}
		}

		// Case 3: Remote player is on foot
		if (ped->bInVehicle || ped->m_pVehicleAnim != nil) {
			MP_DetachPedFromVehicle(ped);
			ped->bUsesCollision = true;
			ped->SetPedState(PED_IDLE);
			ped->RestartNonPartialAnims();
			ped->ReplaceWeaponWhenExitingVehicle();
			CAnimManager::BlendAnimation(ped->GetClump(), ped->m_animGroup, ANIM_STD_IDLE, 100.0f);
		}
		MP_CloseRemoteLastDoor(rp);
		MP_FadeOutVehicleDoorAnims(ped, -1);
		MP_EnsurePedWeapon(ped, rp.weapon, rp.lastWeaponGiven);

		float dtExtrap = Min((float)(now - rp.lastUpdateMs) * 0.001f, 0.1f);
		CVector predPos = rp.targetPos + rp.vel * (dtExtrap * 50.0f);
		float blend = Clamp(dt * 14.0f, 0.15f, 0.78f);
		rp.pos = rp.pos + (predPos - rp.pos) * blend;
		rp.heading = MP_LerpAngle(rp.heading, rp.targetHeading, blend);
		if ((rp.pos - predPos).MagnitudeSqr() > 25.0f) {
			rp.pos = predPos;
		}

		ped->bUsesCollision = true;
		ped->GetMatrix().SetRotateZ(rp.heading);
		ped->SetPosition(rp.pos);
		ped->m_vecMoveSpeed = rp.vel;
		ped->m_fRotationCur = rp.heading;
		ped->m_fRotationDest = rp.heading;
		ped->UpdateRwFrame();

		if (rp.actionFlags & MP_ACT_DEAD) {
			if (ped->m_nPedState != PED_DEAD) ped->SetDead();
			rp.lastCombatAnimId = -1;
		} else {
			if (ped->m_nPedState == PED_DEAD) ped->m_nPedState = PED_IDLE;
			ped->RestartNonPartialAnims();
			ped->bIsDucking = (rp.actionFlags & MP_ACT_DUCKING) != 0;
			float speed2d = rp.vel.Magnitude2D();
			eMoveState ms = (eMoveState)rp.moveState;
			if (rp.actionFlags & MP_ACT_SPRINTING) ms = PEDMOVE_SPRINT;
			else if (ms == PEDMOVE_NONE || ms == PEDMOVE_STILL) {
				if (speed2d > 0.015f) ms = PEDMOVE_RUN;
				else if (speed2d > 0.004f) ms = PEDMOVE_WALK;
				else ms = PEDMOVE_STILL;
			}
			ped->SetMoveState(ms);
			ped->SetMoveAnim();
			if ((rp.actionFlags & MP_ACT_JUMPING) && !RpAnimBlendClumpGetAssociation(ped->GetClump(), ANIM_STD_JUMP_LAUNCH)) {
				CAnimManager::BlendAnimation(ped->GetClump(), ASSOCGRP_STD, ANIM_STD_JUMP_LAUNCH, 8.0f);
			}
			if (rp.animId >= 0 && rp.animGroup < NUM_ANIM_ASSOC_GROUPS && MP_EnsureAnimGroupLoaded((AssocGroupId)rp.animGroup)) {
				if (rp.lastCombatAnimId >= 0 && rp.lastCombatAnimId != rp.animId) {
					CAnimBlendAssociation *prev = RpAnimBlendClumpGetAssociation(ped->GetClump(), rp.lastCombatAnimId);
					if (prev) {
						prev->blendDelta = -8.0f;
						prev->flags |= ASSOC_DELETEFADEDOUT;
					}
				}
				CAnimBlendAssociation *ca = RpAnimBlendClumpGetAssociation(ped->GetClump(), rp.animId);
				if (!ca || ca->blendDelta < 0.0f) {
					ca = CAnimManager::BlendAnimation(ped->GetClump(), (AssocGroupId)rp.animGroup, (AnimationId)rp.animId, 8.0f);
				}
				if (ca) {
					ca->callbackType = CAnimBlendAssociation::CB_NONE;
					ca->SetRun();
					if (fabsf(ca->currentTime - rp.animTime) > 0.10f && rp.animTime >= 0.0f && ca->hierarchy && rp.animTime <= ca->hierarchy->totalLength) {
						ca->SetCurrentTime(rp.animTime);
					}
				}
				rp.lastCombatAnimId = rp.animId;
			} else if (rp.lastCombatAnimId >= 0) {
				CAnimBlendAssociation *prev = RpAnimBlendClumpGetAssociation(ped->GetClump(), rp.lastCombatAnimId);
				if (prev) {
					prev->blendDelta = -8.0f;
					prev->flags |= ASSOC_DELETEFADEDOUT;
				}
				rp.lastCombatAnimId = -1;
			}
		}
	}
}

void MP_UpdateRemotePlayersAfter(float)
{
	for (int32 i = 0; i < MP_MAX_PLAYERS; i++) {
		MpRemotePlayer &rp = gMp.players[i];
		if (!rp.active || !rp.hasState || !rp.ped || !MP_IsPoolPedValid(rp.ped)) continue;
		bool isTransition = (rp.actionFlags & (MP_ACT_ENTERING_VEH | MP_ACT_EXITING_VEH)) != 0;
		if (!isTransition && (rp.actionFlags & MP_ACT_IN_VEHICLE) && rp.ped->bInVehicle && rp.ped->m_pMyVehicle && MP_IsPoolVehicleValid(rp.ped->m_pMyVehicle)) {
			MP_EnsureSeatedAnim(rp.ped, rp.ped->m_pMyVehicle, rp.seatIdx >= 0 ? rp.seatIdx : 0);
			rp.ped->SetPedPositionInCar();
			rp.ped->UpdateRwFrame();
		} else if (!(rp.actionFlags & MP_ACT_DEAD)) {
			rp.ped->SetPosition(rp.pos);
			rp.ped->m_fRotationCur = rp.heading;
			rp.ped->m_fRotationDest = rp.heading;
			rp.ped->SetHeading(rp.heading);
			rp.ped->UpdateRwFrame();
		}
	}
	if (gMp.initialized && !gMp.isHost) {
		for (int32 i = 0; i < MP_MAX_SYNCED_PEDS; i++) {
			MpSyncedPed &sp = gMp.peds[i];
			if (!sp.active || !sp.ped || !MP_IsPoolPedValid(sp.ped)) continue;
			if (!(sp.flags & 1)) {
				sp.ped->SetPosition(sp.pos);
				sp.ped->m_fRotationCur = sp.heading;
				sp.ped->m_fRotationDest = sp.heading;
				sp.ped->SetHeading(sp.heading);
				sp.ped->UpdateRwFrame();
			}
		}
	}
}

void MP_SendLocalPlayerState(void)
{
	uint32 now = CTimer::GetTimeInMilliseconds();
	if (now - gMp.lastPlayerSendMs < 30) return;
	gMp.lastPlayerSendMs = now;

	CPlayerPed *ped = FindPlayerPed();
	if (!ped) return;

	uint8 pkt[96];
	int32 p = 0;
	pkt[p++] = MP_PKT_PLAYER_STATE;

	bool hasMyVeh = (ped->m_pMyVehicle != nil && MP_IsPoolVehicleValid(ped->m_pMyVehicle));
	bool isEntering = hasMyVeh && (ped->m_nPedState == PED_ENTER_CAR || ped->m_nPedState == PED_CARJACK || ped->m_nPedState == PED_SEEK_IN_BOAT);
	bool isExiting = hasMyVeh && (ped->m_nPedState == PED_EXIT_CAR || ped->m_nPedState == PED_DRAG_FROM_CAR);
	bool isSeated = ped->bInVehicle && hasMyVeh && !isExiting;

	CVector pos = ped->GetPosition();
	CVector vel = isSeated ? ped->m_pMyVehicle->m_vecMoveSpeed : ped->m_vecMoveSpeed;
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
	if (isSeated) flags |= MP_ACT_IN_VEHICLE;
	if (isEntering) flags |= MP_ACT_ENTERING_VEH;
	if (isExiting) flags |= MP_ACT_EXITING_VEH;
	if (ped->bIsDucking) flags |= MP_ACT_DUCKING;
	if (ped->m_nPedState == PED_DEAD || ped->m_nPedState == PED_DIE) flags |= MP_ACT_DEAD;
	if (ped->m_nPedState == PED_JUMP || ped->bIsInTheAir) flags |= MP_ACT_JUMPING;
	if (ped->bIsShooting) flags |= MP_ACT_FIRING;
	if (ped->bIsAimingGun || ped->m_nPedState == PED_AIM_GUN) flags |= MP_ACT_AIMING;
	if (ped->m_nPedState == PED_FIGHT || ped->m_nPedState == PED_ATTACK || ped->bIsAttacking || ped->bIsShooting) flags |= MP_ACT_ATTACKING;
	if (ped->m_nMoveState == PEDMOVE_SPRINT) flags |= MP_ACT_SPRINTING;

	int32 netVehId = -1;
	int8 seatIdx = -1;
	if (hasMyVeh) {
		int32 slot = MP_FindVehicleSlotByPtr(ped->m_pMyVehicle);
		if (slot < 0 && (isSeated || isEntering)) {
			slot = MP_TrackLocalVehicle(ped->m_pMyVehicle);
		}
		if (slot >= 0) {
			netVehId = gMp.vehicles[slot].netVehId;
			if (isEntering) {
				if (ped->m_objective == OBJECTIVE_ENTER_CAR_AS_DRIVER || ped->m_nPedState == PED_CARJACK || ped->m_pMyVehicle->pDriver == ped) {
					seatIdx = 0;
				} else if (ped->m_vehDoor == CAR_DOOR_RF) seatIdx = 1;
				else if (ped->m_vehDoor == CAR_DOOR_RR) seatIdx = 3;
				else if (ped->m_vehDoor == CAR_DOOR_LR) seatIdx = 2;
				else seatIdx = 1;
			} else if (ped->m_pMyVehicle->pDriver == ped) {
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

	CAnimBlendAssociation *va = MP_FindLiveVehAnim(ped);
	uint8 animGroup = 0;
	int16 animId = -1;
	float animTime = 0.0f;
	if (va && (isEntering || isExiting || (isSeated && MP_IsDoorCloseAnim(va->animId)))) {
		animGroup = (uint8)Clamp((int32)va->groupId, 0, 255);
		animId = va->animId;
		animTime = va->currentTime;
	} else if (!isSeated && !isEntering && !isExiting && (flags & (MP_ACT_ATTACKING | MP_ACT_AIMING | MP_ACT_FIRING)) && ped->GetClump()) {
		CAnimBlendAssociation *best = nil;
		float bestWeight = 0.05f;
		for (CAnimBlendAssociation *a = RpAnimBlendClumpGetFirstAssociation(ped->GetClump()); a; a = RpAnimBlendGetNextAssociation(a)) {
			if (MP_IsLocomotionAnimId(a->animId)) continue;
			if (a->blendDelta >= 0.0f && a->blendAmount > bestWeight) {
				bestWeight = a->blendAmount;
				best = a;
			}
		}
		if (best) {
			animGroup = (uint8)Clamp((int32)best->groupId, 0, 255);
			animId = (int16)best->animId;
			animTime = best->currentTime;
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
	pkt[p++] = (uint8)ped->m_nPedState;
	pkt[p++] = (uint8)ped->m_nMoveState;
	pkt[p++] = MP_EncodeDoorCode(ped->m_vehDoor);
	pkt[p++] = animGroup;
	memcpy(pkt + p, &animId, 2); p += 2;
	memcpy(pkt + p, &animTime, 4); p += 4;

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

	if (len >= 46) {
		rp->pedState = data[p++];
		rp->moveState = data[p++];
		rp->vehDoorCode = data[p++];
		rp->animGroup = data[p++];
		memcpy(&rp->animId, data + p, 2); p += 2;
		memcpy(&rp->animTime, data + p, 4); p += 4;
	} else {
		rp->pedState = 0;
		rp->moveState = 0;
		rp->vehDoorCode = 0;
		rp->animGroup = 0;
		rp->animId = -1;
		rp->animTime = 0.0f;
	}

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

	bool gJustDown = (CPad::NewKeyState.VK_KEYS['G'] && !CPad::OldKeyState.VK_KEYS['G']);
	if (!gJustDown) return;

	CVector lpos = local->GetPosition();
	CVehicle *bestVeh = nil;
	float bestDistSq = 64.0f; // 8m radius
	for (int32 i = 0; i < MP_MAX_SYNCED_VEHICLES; i++) {
		MpSyncedVehicle &sv = gMp.vehicles[i];
		if (!sv.active || !sv.veh || !MP_IsPoolVehicleValid(sv.veh)) continue;
		float dSq = (sv.veh->GetPosition() - lpos).MagnitudeSqr();
		if (dSq < bestDistSq) {
			bestDistSq = dSq;
			bestVeh = sv.veh;
		}
	}
	if (!bestVeh) return;
	MP_OnLocalEnterVehicleAttempt(local, bestVeh, true);
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
				w->Update(pass->m_audioEntityId, nil);
			}
		}
	}
}

void MP_TickAmbientPedsBefore(float dt)
{
	if (!gMp.initialized || gMp.isHost) return;
	uint32 now = CTimer::GetTimeInMilliseconds();
	for (int32 i = 0; i < MP_MAX_SYNCED_PEDS; i++) {
		MpSyncedPed &sp = gMp.peds[i];
		if (!sp.active) continue;
		if (sp.ped && !MP_IsPoolPedValid(sp.ped)) {
			sp.ped = nil;
		}
		if (now - sp.lastUpdateMs > 3200) {
			if (sp.ped) {
				if (sp.ped->bInVehicle && sp.ped->m_pMyVehicle && MP_IsPoolVehicleValid(sp.ped->m_pMyVehicle)) {
					if (sp.ped->m_pMyVehicle->pDriver == sp.ped) sp.ped->m_pMyVehicle->RemoveDriver();
					else sp.ped->m_pMyVehicle->RemovePassenger(sp.ped);
					sp.ped->bInVehicle = false;
					sp.ped->m_pMyVehicle = nil;
				}
				CWorld::Remove(sp.ped);
				delete sp.ped;
				sp.ped = nil;
			}
			sp.active = false;
			continue;
		}
		if (!sp.ped) {
			sp.ped = MP_SpawnManagedPed(sp.modelIndex, sp.targetPos, false);
			if (!sp.ped) continue;
			sp.pos = sp.targetPos;
			sp.heading = sp.targetHeading;
		}
		CPed *p = sp.ped;
		p->m_area = (int8)sp.area;

		if (sp.flags & 1) {
			if (!(sp.prevFlags & 1) && p->m_nPedState != PED_DEAD) {
				p->m_fHealth = 0.0f;
				p->SetDie(ANIM_STD_KO_FRONT, 4.0f, 0.0f);
			} else if (p->m_nPedState != PED_DEAD) {
				p->m_fHealth = 0.0f;
				p->SetDead();
			}
			sp.lastHealth = 0;
			sp.prevFlags = sp.flags;
			continue;
		}

		if (sp.health < sp.lastHealth && sp.lastHealth > 0) {
			p->StartFightDefend(0, HITLEVEL_HIGH, 10);
			CParticle::AddParticle(PARTICLE_BLOOD, p->GetPosition() + CVector(0.0f, 0.0f, 0.35f), CVector(0.0f, 0.0f, 0.04f));
		}
		sp.lastHealth = sp.health;
		sp.prevFlags = sp.flags;

		float dtExtrap = Min((float)(now - sp.lastUpdateMs) * 0.001f, 0.12f);
		CVector predPos = sp.targetPos + sp.vel * (dtExtrap * 50.0f);
		float blend = Clamp(dt * 14.0f, 0.18f, 0.72f);
		if ((sp.pos - predPos).MagnitudeSqr() > 25.0f) {
			sp.pos = predPos;
		} else {
			sp.pos = sp.pos + (predPos - sp.pos) * blend;
		}
		sp.heading = MP_LerpAngle(sp.heading, sp.targetHeading, blend);

		p->bUsesCollision = true;
		p->SetPosition(sp.pos);
		p->m_vecMoveSpeed = sp.vel;
		p->m_fRotationCur = sp.heading;
		p->m_fRotationDest = sp.heading;
		p->SetHeading(sp.heading);
		p->UpdateRwFrame();
		p->m_fHealth = Clamp((float)sp.health, 1.0f, 100.0f);
		p->bIsDucking = (sp.flags & 2) != 0;

		if (sp.weapon < WEAPONTYPE_TOTALWEAPONS && p->GetWeapon()->m_eWeaponType != (eWeaponType)sp.weapon) {
			if (sp.weapon == WEAPONTYPE_UNARMED) p->SetCurrentWeapon(WEAPONTYPE_UNARMED);
			else {
				p->GiveWeapon((eWeaponType)sp.weapon, 999, true);
				p->SetCurrentWeapon((eWeaponType)sp.weapon);
			}
		}
		eMoveState ms = (eMoveState)sp.moveState;
		if (ms == PEDMOVE_NONE) ms = (sp.vel.Magnitude2D() > 0.005f) ? PEDMOVE_WALK : PEDMOVE_STILL;
		p->SetMoveState(ms);
		if (p->m_nPedState != PED_FALL && p->m_nPedState != PED_DIE) {
			p->SetMoveAnim();
		}
	}
}

void MP_TickAmbientPedsAfter(float)
{
	if (!gMp.initialized || !gMp.isHost) return;
	uint32 now = CTimer::GetTimeInMilliseconds();
	if (now - gMp.lastPedSendMs < 50) return;
	gMp.lastPedSendMs = now;

	CPed *local = FindPlayerPed();
	CVector focus = local ? local->GetPosition() : CVector(0, 0, 0);
	CPool<CPed, CPlayerPed> *pool = CPools::GetPedPool();
	if (!pool) return;
	int32 sz = pool->GetSize();
	if (sz <= 0) return;

	// Batched packet: [type(1), count(1), (netPedId(4), mi(2), ptype(1), flags(1), pos(12), vx16(2), vy16(2), h16(2), hp(1), wpn(1), moveState(1), area(1))]
	// = 2 + 30 * 15 = 452 bytes <= 512
	uint8 pkt[MP_MAX_PACKET_SIZE];
	int32 p = 2;
	uint8 count = 0;

	for (int32 step = 0; step < sz && count < 15; step++) {
		int32 idx = (int32)((gMp.pedScanCursor + (uint32)step) % (uint32)sz);
		CPed *ped = pool->GetSlot(idx);
		if (!ped || ped == local || MP_IsRemotePlayerPed(ped) || ped->bInVehicle) continue;
		if ((ped->GetPosition() - focus).Magnitude2D() > 70.0f && !MP_IsNearAnyPlayer2D(ped->GetPosition(), 65.0f))
			continue;

		int32 netId = CPools::GetPedRef(ped);
		uint16 mi = (uint16)ped->GetModelIndex();
		uint8 ptype = (uint8)ped->m_nPedType;
		uint8 pflags = (ped->DyingOrDead() ? 1 : 0) | (ped->bIsDucking ? 2 : 0) | ((ped->CharCreatedBy == MISSION_CHAR) ? 4 : 0);
		CVector pos = ped->GetPosition();
		int16 vx16 = (int16)Clamp((int32)(ped->m_vecMoveSpeed.x * 1000.0f), -32000, 32000);
		int16 vy16 = (int16)Clamp((int32)(ped->m_vecMoveSpeed.y * 1000.0f), -32000, 32000);
		uint16 h16 = MP_EncodeAngle16(ped->m_fRotationCur);
		uint8 hp = (uint8)Clamp((int32)ped->m_fHealth, 0, 255);
		uint8 wpn = (uint8)ped->GetWeapon()->m_eWeaponType;
		uint8 ms = (uint8)ped->m_nMoveState;
		uint8 area = (uint8)Clamp((int32)ped->m_area, 0, 255);

		memcpy(pkt + p, &netId, 4); p += 4;
		memcpy(pkt + p, &mi, 2); p += 2;
		pkt[p++] = ptype;
		pkt[p++] = pflags;
		memcpy(pkt + p, &pos, 12); p += 12;
		memcpy(pkt + p, &vx16, 2); p += 2;
		memcpy(pkt + p, &vy16, 2); p += 2;
		memcpy(pkt + p, &h16, 2); p += 2;
		pkt[p++] = hp;
		pkt[p++] = wpn;
		pkt[p++] = ms;
		pkt[p++] = area;
		count++;
	}
	gMp.pedScanCursor = (gMp.pedScanCursor + 9) % (uint32)sz;

	if (count > 0) {
		pkt[0] = MP_PKT_PED_SYNC;
		pkt[1] = count;
		MP_EnqueuePacket(-1, 0, pkt, p);
	}
}

void MP_HandlePedSyncPacket(int32 fromPeerId, const uint8 *data, int32 len)
{
	if (gMp.isHost || len < 32) return;
	uint8 count = data[1];
	int32 p = 2;
	if (count == 0 || count > 16 || len < 2 + (int32)count * 30) return;

	uint32 now = CTimer::GetTimeInMilliseconds();
	for (uint8 n = 0; n < count; n++) {
		int32 netId = 0;
		uint16 mi = MI_MALE01;
		CVector pos;
		int16 vx16 = 0, vy16 = 0;
		uint16 h16 = 0;
		memcpy(&netId, data + p, 4); p += 4;
		memcpy(&mi, data + p, 2); p += 2;
		uint8 ptype = data[p++];
		uint8 pflags = data[p++];
		memcpy(&pos, data + p, 12); p += 12;
		memcpy(&vx16, data + p, 2); p += 2;
		memcpy(&vy16, data + p, 2); p += 2;
		memcpy(&h16, data + p, 2); p += 2;
		uint8 hp = data[p++];
		uint8 wpn = data[p++];
		uint8 ms = data[p++];
		uint8 area = data[p++];

		int32 slot = -1, freeSlot = -1, deadSlot = -1;
		for (int32 i = 0; i < MP_MAX_SYNCED_PEDS; i++) {
			if (gMp.peds[i].active && gMp.peds[i].netPedId == netId) {
				slot = i;
				break;
			}
			if (!gMp.peds[i].active && freeSlot < 0) freeSlot = i;
			if (gMp.peds[i].active && (gMp.peds[i].flags & 1) && deadSlot < 0) deadSlot = i;
		}
		if (slot < 0) slot = (freeSlot >= 0) ? freeSlot : deadSlot;
		if (slot < 0) continue;

		MpSyncedPed &sp = gMp.peds[slot];
		bool fresh = !sp.active || sp.netPedId != netId;
		if (fresh && sp.ped && MP_IsPoolPedValid(sp.ped)) {
			CWorld::Remove(sp.ped);
			delete sp.ped;
			sp.ped = nil;
		}
		sp.active = true;
		sp.netPedId = netId;
		sp.ownerPeerId = fromPeerId;
		sp.modelIndex = mi;
		sp.pedType = ptype;
		sp.flags = pflags;
		sp.targetPos = pos;
		sp.vel = CVector((float)vx16 * 0.001f, (float)vy16 * 0.001f, 0.0f);
		sp.targetHeading = MP_DecodeAngle16(h16);
		sp.health = hp;
		sp.weapon = wpn;
		sp.moveState = ms;
		sp.area = area;
		if (fresh) {
			sp.lastHealth = hp;
			sp.prevFlags = pflags;
		}
		if (fresh || (sp.pos - sp.targetPos).MagnitudeSqr() > 400.0f) {
			sp.pos = sp.targetPos;
			sp.heading = sp.targetHeading;
		}
		sp.lastUpdateMs = now;
	}
}

void MP_HandlePedRemovePacket(int32, const uint8 *data, int32 len)
{
	if (gMp.isHost || len < 5) return;
	int32 netId = 0;
	memcpy(&netId, data + 1, 4);
	for (int32 i = 0; i < MP_MAX_SYNCED_PEDS; i++) {
		MpSyncedPed &sp = gMp.peds[i];
		if (sp.active && sp.netPedId == netId) {
			if (sp.ped && MP_IsPoolPedValid(sp.ped)) {
				CWorld::Remove(sp.ped);
				delete sp.ped;
			}
			sp.ped = nil;
			sp.active = false;
			break;
		}
	}
}

void MP_RenderNameTags(void)
{
	if (!gMp.initialized) return;
	for (int32 i = 0; i < MP_MAX_PLAYERS; i++) {
		const MpRemotePlayer &rp = gMp.players[i];
		if (!rp.active || !rp.hasState || !rp.ped || !MP_IsPoolPedValid(rp.ped)) continue;
		if (rp.area != (uint8)CGame::currArea) continue;

		CVector headPos = rp.ped->GetPosition() + CVector(0.0f, 0.0f, rp.ped->bInVehicle ? 1.35f : 1.25f);
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
			MP_DetachPedFromVehicle(gMp.players[i].ped);
			CWorld::Remove(gMp.players[i].ped);
			delete gMp.players[i].ped;
		}
		gMp.players[i].ped = nil;
		gMp.players[i].hasState = false;
		gMp.players[i].active = false;
		gMp.players[i].lastDoorVehId = -1;
		gMp.players[i].lastDoorCode = 0;
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
