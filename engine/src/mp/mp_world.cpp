#include "mp_core.h"

#ifdef MULTIPLAYER_COOP

#include "Bridge.h"
#include "BulletInfo.h"
#include "Clock.h"
#include "Explosion.h"
#include "Frontend.h"
#include "Garages.h"
#include "GenericGameStorage.h"
#include "ModelIndices.h"
#include "Pad.h"
#include "PCSave.h"
#include "Pickups.h"
#include "Remote.h"
#include "Stats.h"
#include "Streaming.h"
#include "Timer.h"
#include "Weather.h"
#include "World.h"
#include "PCSave.h"
#include "SaveBuf.h"
#include "Script.h"
#include "Game.h"
#include "crossplatform.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Cheats implemented in Pad.cpp
extern void WeaponCheat1();
extern void WeaponCheat2();
extern void WeaponCheat3();
extern void HealthCheat();
extern void ArmourCheat();
extern void WantedLevelUpCheat();
extern void WantedLevelDownCheat();
extern void BlowUpCarsCheat();
extern void MayhemCheat();
extern void EverybodyAttacksPlayerCheat();
extern void WeaponsForAllCheat();
extern void FastWeatherCheat();
extern void OnlyRenderWheelsCheat();
extern void ChittyChittyBangBangCheat();
extern void StrongGripCheat();
extern void FannyMagnetCheat();

extern char DefaultPCSaveFileName[260];
extern char LoadFileName[256];

static uint32 gLastResetBroadcastMs = 0;

void MP_ApplyOpenIslands(int32 mode)
{
	if (mode == 2) {
		gMp.freeRoam = true;
		gMp.openIslands = true;
	} else {
		gMp.openIslands = (mode != 0);
	}
	if (gMp.openIslands) {
		CStats::CommercialPassed = 1;
		CStats::IndustrialPassed = 1;
		CStats::SuburbanPassed = 1;
	}
	if (mode == 2 && CTheScripts::ScriptToLoad != 1) {
		CTheScripts::ScriptToLoad = 1;
		if (gGameState == GS_PLAYING_GAME) {
			FrontEndMenuManager.m_bWantToLoad = false;
			FrontEndMenuManager.m_bFirstTime = false;
			FrontEndMenuManager.m_bWantToRestart = true;
		}
	}
}

void MP_TickWorldBefore(float)
{
	if (!gMp.initialized) return;
	if (gMp.openIslands) {
		CStats::CommercialPassed = 1;
		CStats::IndustrialPassed = 1;
		CStats::SuburbanPassed = 1;
	}
}

void MP_TickWorldAfter(float)
{
	if (!gMp.initialized || !gMp.isHost) return;
	uint32 now = CTimer::GetTimeInMilliseconds();
	if (now - gMp.lastWorldSendMs < 1500) return;
	gMp.lastWorldSendMs = now;

	uint8 pkt[12];
	pkt[0] = MP_PKT_WEATHER_TIME;
	pkt[1] = CClock::ms_nGameClockHours;
	pkt[2] = CClock::ms_nGameClockMinutes;
	int16 wOld = CWeather::OldWeatherType;
	int16 wNew = CWeather::NewWeatherType;
	memcpy(pkt + 3, &wOld, 2);
	memcpy(pkt + 5, &wNew, 2);
	pkt[7] = (gMp.freeRoam || CTheScripts::ScriptToLoad == 1) ? 2 : (gMp.openIslands ? 1 : 0);
	MP_EnqueuePacket(-1, 0, pkt, 8);
}

void MP_ExecCheat(int32 cheatId, int32 param1, int32)
{
	switch (cheatId) {
	case 1:
		HealthCheat();
		break;
	case 2:
		ArmourCheat();
		break;
	case 3:
		CWorld::Players[CWorld::PlayerInFocus].m_nMoney += 250000;
		break;
	case 4:
		WeaponCheat1();
		WeaponCheat2();
		WeaponCheat3();
		break;
	case 5:
		WantedLevelUpCheat();
		break;
	case 6:
		WantedLevelDownCheat();
		break;
	case 7:
		MP_SpawnVehicle(MI_RHINO, 7.0f);
		break;
	case 8:
		BlowUpCarsCheat();
		break;
	case 9:
		if (param1 == 0) CWeather::ForceWeatherNow(WEATHER_SUNNY);
		else if (param1 == 1) CWeather::ForceWeatherNow(WEATHER_CLOUDY);
		else if (param1 == 2) CWeather::ForceWeatherNow(WEATHER_RAINY);
		else CWeather::ForceWeatherNow(WEATHER_FOGGY);
		break;
	case 10:
		if (param1 == 1) CTimer::SetTimeScale(2.0f);
		else if (param1 == 2) CTimer::SetTimeScale(0.5f);
		else CTimer::SetTimeScale(1.0f);
		break;
	case 11:
		FastWeatherCheat();
		break;
	case 12:
		MayhemCheat();
		break;
	case 13:
		EverybodyAttacksPlayerCheat();
		break;
	case 14:
		WeaponsForAllCheat();
		break;
	case 15:
		OnlyRenderWheelsCheat();
		break;
	case 16:
		ChittyChittyBangBangCheat();
		break;
	case 17:
		StrongGripCheat();
		break;
	case 18:
		FrontEndMenuManager.m_PrefsAllowNastyGame = !FrontEndMenuManager.m_PrefsAllowNastyGame;
		break;
	case 19: {
		CVehicle *veh = FindPlayerVehicle();
		if (veh) {
			veh->m_fHealth = 1000.0f;
			if (veh->IsCar()) ((CAutomobile *)veh)->Fix();
			else if (veh->IsBike()) ((CBike *)veh)->Fix();
		}
		break;
	}
	case 20: {
		CVehicle *veh = FindPlayerVehicle();
		if (veh) {
			CVector pos = veh->GetPosition();
			float hdg = veh->GetForward().Heading();
			veh->GetMatrix().SetRotateZOnly(hdg);
			veh->SetPosition(pos + CVector(0, 0, 1.0f));
			veh->m_vecMoveSpeed += veh->GetForward() * 0.45f;
		}
		break;
	}
	case 21: {
		CPlayerPed *ped = FindPlayerPed();
		if (ped) {
			CVector pos = ped->GetPosition() + ped->GetForward() * 3.5f;
			CRemote::GivePlayerRemoteControlledCar(pos.x, pos.y, pos.z, ped->m_fRotationCur, MI_RCBANDIT);
		}
		break;
	}
	case 22:
		MP_SetPlayerInfo(gMp.localPeerId, nil, param1);
		break;
	default:
		break;
	}
}

void MP_SpawnVehicle(int32 modelIndex, float aheadDist)
{
	if (!gMp.initialized || gMp.isHost) {
		MP_SpawnVehicleLocal(modelIndex, aheadDist);
		return;
	}
	uint8 pkt[12];
	pkt[0] = MP_PKT_SPAWN_REQ;
	pkt[1] = MP_SPAWN_REQ_VEHICLE;
	memcpy(pkt + 2, &modelIndex, 4);
	memcpy(pkt + 6, &aheadDist, 4);
	MP_EnqueuePacket(-1, 1, pkt, 10);
}

static void MP_SpawnPickupLocal(int32 pickupKind, int32 weaponOrAmount, const CVector *optPos)
{
	CPlayerPed *ped = FindPlayerPed();
	if (!ped) return;
	CVector pos = optPos ? *optPos : (ped->GetPosition() + ped->GetForward() * 3.0f);
	uint32 mi = MI_PICKUP_HEALTH;
	uint8 ptype = PICKUP_ONCE_TIMEOUT;
	uint32 qty = 0;

	switch (pickupKind) {
	case 1: mi = MI_PICKUP_HEALTH; break;
	case 2: mi = MI_PICKUP_BODYARMOUR; break;
	case 3: mi = MI_MONEY; ptype = PICKUP_MONEY; qty = weaponOrAmount > 0 ? (uint32)weaponOrAmount : 5000; break;
	case 4: mi = MI_PICKUP_BRIBE; break;
	case 5: mi = MI_PICKUP_ADRENALINE; break;
	case 6: {
		eWeaponType wt = (eWeaponType)Clamp(weaponOrAmount, 1, WEAPONTYPE_TOTALWEAPONS - 1);
		mi = (uint32)CPickups::ModelForWeapon(wt);
		qty = AmmoForWeapon[wt];
		break;
	}
	default: break;
	}

	if (mi > 0) {
		CStreaming::RequestModel(mi, STREAMFLAGS_DONT_REMOVE);
		CStreaming::LoadAllRequestedModels(false);
		CPickups::GenerateNewOne(pos, mi, ptype, qty);
	}
}

void MP_SpawnPickup(int32 pickupKind, int32 weaponOrAmount)
{
	if (!gMp.initialized || gMp.isHost) {
		MP_SpawnPickupLocal(pickupKind, weaponOrAmount, nil);
		return;
	}
	CPlayerPed *ped = FindPlayerPed();
	CVector pos = ped ? (ped->GetPosition() + ped->GetForward() * 3.0f) : CVector(0, 0, 0);
	uint8 pkt[24];
	pkt[0] = MP_PKT_SPAWN_REQ;
	pkt[1] = MP_SPAWN_REQ_PICKUP;
	memcpy(pkt + 2, &pickupKind, 4);
	memcpy(pkt + 6, &weaponOrAmount, 4);
	memcpy(pkt + 10, &pos, 12);
	MP_EnqueuePacket(-1, 1, pkt, 22);
}

void MP_HandleSpawnReqPacket(int32 fromPeerId, const uint8 *data, int32 len)
{
	if (!gMp.isHost || len < 10) return;
	uint8 kind = data[1];
	if (kind == MP_SPAWN_REQ_VEHICLE) {
		int32 modelIndex = 0;
		float dist = 6.0f;
		memcpy(&modelIndex, data + 2, 4);
		memcpy(&dist, data + 6, 4);
		for (int32 i = 0; i < MP_MAX_PLAYERS; i++) {
			if (gMp.players[i].active && gMp.players[i].peerId == fromPeerId) {
				CVector pos = gMp.players[i].pos + CVector(Cos(gMp.players[i].heading + HALFPI), Sin(gMp.players[i].heading + HALFPI), 0.0f) * dist + CVector(0, 0, 1.0f);
				// Temporarily spawn near requesting player
				CVehicle *v = MP_SpawnVehicleLocal(modelIndex, dist);
				if (v) v->SetPosition(pos);
				break;
			}
		}
	} else if (kind == MP_SPAWN_REQ_PICKUP && len >= 22) {
		int32 pKind = 0, arg = 0;
		CVector pos;
		memcpy(&pKind, data + 2, 4);
		memcpy(&arg, data + 6, 4);
		memcpy(&pos, data + 10, 12);
		MP_SpawnPickupLocal(pKind, arg, &pos);
	}
}

void MP_OnShotFired(int32 weaponType, const CVector &src, const CVector &dir, CEntity *, float dmg, bool isDriveBy)
{
	if (!gMp.initialized) return;
	uint8 pkt[36];
	int32 p = 0;
	pkt[p++] = MP_PKT_SHOT_FIRED;
	pkt[p++] = (uint8)Clamp(weaponType, 0, 255);
	pkt[p++] = isDriveBy ? 1 : 0;
	memcpy(pkt + p, &src, 12); p += 12;
	memcpy(pkt + p, &dir, 12); p += 12;
	memcpy(pkt + p, &dmg, 4); p += 4;
	MP_EnqueuePacket(-1, 0, pkt, p);
}

void MP_HandleShotFiredPacket(int32 fromPeerId, const uint8 *data, int32 len)
{
	if (len < 31) return;
	MpRemotePlayer *rp = MP_GetOrCreateRemotePlayer(fromPeerId);
	if (!rp || !rp->ped) return;
	uint8 wt = data[1];
	CVector src, dir;
	float dmg = 10.0f;
	int32 p = 3;
	memcpy(&src, data + p, 12); p += 12;
	memcpy(&dir, data + p, 12); p += 12;
	memcpy(&dmg, data + p, 4); p += 4;

	eWeaponType ewt = (eWeaponType)Clamp((int32)wt, 0, WEAPONTYPE_TOTALWEAPONS - 1);
	CWeaponInfo *info = CWeaponInfo::GetWeaponInfo(ewt);
	if (info && info->m_eWeaponFire == WEAPON_FIRE_INSTANT_HIT) {
		CVector endPos = src + dir * info->m_fRange;
		CBulletInfo::AddBullet(rp->ped, ewt, src, dir * 1.5f);
		(void)endPos;
	}
}

void MP_OnPickupEvent(int32 slot, uint8 action, int32 modelIndex, uint8 type, int32 quantity, const CVector &pos)
{
	if (!gMp.initialized) return;
	uint8 pkt[32];
	int32 p = 0;
	pkt[p++] = MP_PKT_PICKUP_EVENT;
	pkt[p++] = action;
	pkt[p++] = type;
	memcpy(pkt + p, &slot, 4); p += 4;
	memcpy(pkt + p, &modelIndex, 4); p += 4;
	memcpy(pkt + p, &quantity, 4); p += 4;
	memcpy(pkt + p, &pos, 12); p += 12;
	MP_EnqueuePacket(-1, 1, pkt, p);
}

void MP_HandlePickupEventPacket(int32, const uint8 *data, int32 len)
{
	if (len < 27) return;
	uint8 action = data[1];
	uint8 type = data[2];
	int32 slot = -1, mi = 0, qty = 0;
	CVector pos;
	int32 p = 3;
	memcpy(&slot, data + p, 4); p += 4;
	memcpy(&mi, data + p, 4); p += 4;
	memcpy(&qty, data + p, 4); p += 4;
	memcpy(&pos, data + p, 12); p += 12;

	if (action == 1 && slot >= 0 && slot < NUMPICKUPS) {
		CPickup &pk = CPickups::aPickUps[slot];
		if (!pk.m_bRemoved) {
			pk.GetRidOfObjects();
			pk.m_bRemoved = true;
		}
	} else if (action == 0 && !gMp.isHost && mi > 0) {
		CStreaming::RequestModel(mi, STREAMFLAGS_DONT_REMOVE);
		CStreaming::LoadAllRequestedModels(false);
		CPickups::GenerateNewOne(pos, (uint32)mi, type, (uint32)qty);
	}
}

void MP_OnGarageAction(int32 garageIdx, uint8 action, CVehicle *veh, uint8 col1, uint8 col2, uint8 extra)
{
	if (!gMp.initialized) return;
	int32 netVehId = veh ? (MP_FindVehicleSlotByPtr(veh) >= 0 ? gMp.vehicles[MP_FindVehicleSlotByPtr(veh)].netVehId : -1) : -1;
	uint8 pkt[16];
	int32 p = 0;
	pkt[p++] = MP_PKT_GARAGE_EVENT;
	pkt[p++] = action;
	pkt[p++] = col1;
	pkt[p++] = col2;
	pkt[p++] = extra;
	memcpy(pkt + p, &garageIdx, 4); p += 4;
	memcpy(pkt + p, &netVehId, 4); p += 4;
	MP_EnqueuePacket(-1, 1, pkt, p);
}

void MP_HandleGarageEventPacket(int32, const uint8 *data, int32 len)
{
	if (len < 13) return;
	uint8 action = data[1];
	uint8 col1 = data[2];
	uint8 col2 = data[3];
	uint8 extra = data[4];
	int32 gIdx = -1, netVehId = -1;
	memcpy(&gIdx, data + 5, 4);
	memcpy(&netVehId, data + 9, 4);
	CVehicle *veh = MP_GetVehiclePtrByNetId(netVehId);
	if (!veh) return;
	if (action == MP_GARAGE_RESPRAY) {
		veh->m_currentColour1 = col1;
		veh->m_currentColour2 = col2;
		veh->m_fHealth = 1000.0f;
	} else if (action >= MP_GARAGE_BOMB1 && action <= MP_GARAGE_BOMB3) {
		veh->m_bombType = extra;
	} else if (action == MP_GARAGE_DETONATE) {
		veh->m_bombType = CARBOMB_NONE;
		veh->m_nBombTimer = 500;
	}
}

void MP_HandleWeatherTimePacket(int32, const uint8 *data, int32 len)
{
	if (gMp.isHost || len < 8) return;
	uint8 h = data[1];
	uint8 m = data[2];
	int16 wOld = 0, wNew = 0;
	memcpy(&wOld, data + 3, 2);
	memcpy(&wNew, data + 5, 2);
	int32 mode = (int32)data[7];
	CClock::SetGameClock(h, m);
	CWeather::OldWeatherType = wOld;
	CWeather::NewWeatherType = wNew;
	MP_ApplyOpenIslands(mode);
}

void MP_OnRemoteCarEvent(uint8 action, const CVector &pos, float heading, CVehicle *)
{
	if (!gMp.initialized) return;
	uint8 pkt[20];
	pkt[0] = MP_PKT_REMOTE_CAR;
	pkt[1] = action;
	memcpy(pkt + 2, &pos, 12);
	memcpy(pkt + 14, &heading, 4);
	MP_EnqueuePacket(-1, 1, pkt, 18);
}

void MP_HandleRemoteCarPacket(int32, const uint8 *data, int32 len)
{
	if (len < 18) return;
	uint8 action = data[1];
	if (action == 1) {
		CVector pos;
		memcpy(&pos, data + 2, 12);
		CExplosion::AddExplosion(nil, nil, EXPLOSION_CAR_QUICK, pos, 0);
	}
}

void MP_OnWorldReset(void)
{
	MP_CleanupRemotePlayers();
	MP_CleanupRemoteVehicles();
	MP_CleanupRemotePeds();
	uint32 now = CTimer::GetTimeInMilliseconds();
	if (gMp.initialized && gMp.isHost && now - gLastResetBroadcastMs > 1000) {
		gLastResetBroadcastMs = now;
		uint8 pkt[2] = { MP_PKT_WORLD_RESET, 1 };
		MP_EnqueuePacket(-1, 1, pkt, 2);
	}
}

void MP_HandleWorldResetPacket(int32, const uint8 *, int32)
{
	MP_CleanupRemotePlayers();
	MP_CleanupRemoteVehicles();
	MP_CleanupRemotePeds();
}

uint8 *MP_ExportSaveData(int32 *outSize)
{
	if (outSize) *outSize = 0;
	PcSaveHelper.SaveSlot(6);
	char filename[300];
	snprintf(filename, sizeof(filename), "%s7.b", DefaultPCSaveFileName);
	FILE *f = fopen(filename, "rb");
	if (!f) return nil;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	rewind(f);
	if (sz <= 0 || sz > 2 * 1024 * 1024) {
		fclose(f);
		return nil;
	}
	uint8 *buf = (uint8 *)malloc((size_t)sz);
	if (!buf) {
		fclose(f);
		return nil;
	}
	size_t rd = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	if ((long)rd != sz) {
		free(buf);
		return nil;
	}
	if (outSize) *outSize = (int32)sz;
	return buf;
}

bool MP_ImportSaveData(const uint8 *data, int32 size)
{
	if (!data || size <= 0 || size > 2 * 1024 * 1024) return false;
	char filename[300];
	snprintf(filename, sizeof(filename), "%s7.b", DefaultPCSaveFileName);
	FILE *f = fopen(filename, "wb");
	if (!f) return false;
	size_t wr = fwrite(data, 1, (size_t)size, f);
	fclose(f);
	if ((int32)wr != size) return false;

	snprintf(LoadFileName, sizeof(LoadFileName), "%s7", DefaultPCSaveFileName);
	FrontEndMenuManager.m_bWantToLoad = true;
	FrontEndMenuManager.m_bWantToRestart = true;
	return true;
}

#endif // MULTIPLAYER_COOP
