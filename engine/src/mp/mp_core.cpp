#include "mp_core.h"

#ifdef MULTIPLAYER_COOP

#include "Timer.h"
#include "World.h"
#include "Pools.h"
#include "Game.h"
#include "Pad.h"
#include "General.h"
#include "Weapon.h"
#include "Streaming.h"
#include "Wanted.h"
#include "Radar.h"
#include <cstring>
#include <cmath>

MpState gMp;

bool MP_IsPoolPedValid(const CPed *ped)
{
	if (!ped) return false;
	CPool<CPed, CPlayerPed> *pool = CPools::GetPedPool();
	if (!pool) return false;
	int32 sz = pool->GetSize();
	for (int32 i = 0; i < sz; i++) {
		if (pool->GetSlot(i) == ped) return true;
	}
	return false;
}

bool MP_IsPoolVehicleValid(const CVehicle *veh)
{
	if (!veh) return false;
	CPool<CVehicle, CAutomobile> *pool = CPools::GetVehiclePool();
	if (!pool) return false;
	int32 sz = pool->GetSize();
	for (int32 i = 0; i < sz; i++) {
		if (pool->GetSlot(i) == veh) return true;
	}
	return false;
}

static void MP_ClearPacketQueue(void)
{
	gMp.outHead = 0;
	gMp.outTail = 0;
	gMp.outCount = 0;
}

void MP_Init(int32 localPeerId, bool isHost)
{
	bool prevOpen = gMp.openIslands;
	bool prevFreeRoam = gMp.freeRoam;
	MP_Shutdown();
	memset(&gMp, 0, sizeof(gMp));
	gMp.initialized = true;
	gMp.isHost = isHost;
	gMp.openIslands = prevOpen;
	gMp.freeRoam = prevFreeRoam;
	gMp.localPeerId = localPeerId > 0 ? localPeerId : 1;
	gMp.localSkinModel = 0;
	strncpy(gMp.localName, "Tommy", MP_MAX_PLAYER_NAME - 1);
	gMp.nextNetVehId = (gMp.localPeerId * 1000) + 1;
	gMp.nextNetPedId = (gMp.localPeerId * 1000) + 1;
	for (int32 i = 0; i < MP_MAX_PLAYERS; i++) {
		gMp.players[i].peerId = -1;
		gMp.players[i].netVehId = -1;
		gMp.players[i].seatIdx = -1;
		gMp.players[i].blipHandle = -1;
	}
	for (int32 i = 0; i < MP_MAX_SYNCED_VEHICLES; i++) {
		gMp.vehicles[i].netVehId = -1;
		gMp.vehicles[i].ownerPeerId = -1;
	}
	for (int32 i = 0; i < MP_MAX_SYNCED_PEDS; i++) {
		gMp.peds[i].netPedId = -1;
		gMp.peds[i].ownerPeerId = -1;
	}
}

void MP_Shutdown(void)
{
	if (!gMp.initialized) return;
	MP_CleanupRemotePlayers();
	MP_CleanupRemoteVehicles();
	MP_CleanupRemotePeds();
	MP_ClearPacketQueue();
	CTimer::ms_bDisableUserPause = false;
	gMp.initialized = false;
}

void MP_EnqueuePacket(int32 targetPeer, uint8 channel, const uint8 *data, int32 len)
{
	if (!gMp.initialized || !data || len <= 0 || len > MP_MAX_PACKET_SIZE) return;
	if (gMp.outCount >= MP_MAX_OUT_QUEUE) {
		// Drop oldest unreliable packet if queue is full
		gMp.outHead = (gMp.outHead + 1) % MP_MAX_OUT_QUEUE;
		gMp.outCount--;
	}
	MpOutPacket &slot = gMp.outQueue[gMp.outTail];
	slot.targetPeer = targetPeer;
	slot.channel = channel;
	slot.size = (uint16)len;
	memcpy(slot.data, data, (size_t)len);
	gMp.outTail = (gMp.outTail + 1) % MP_MAX_OUT_QUEUE;
	gMp.outCount++;
}

int32 MP_PollNetPacket(int32 *outChannel, int32 *outTargetPeer, uint8 *outBuf, int32 maxLen)
{
	if (!gMp.initialized || gMp.outCount <= 0 || !outBuf) return 0;
	const MpOutPacket &slot = gMp.outQueue[gMp.outHead];
	if ((int32)slot.size > maxLen) return 0;
	int32 len = (int32)slot.size;
	if (outChannel) *outChannel = (int32)slot.channel;
	if (outTargetPeer) *outTargetPeer = slot.targetPeer;
	memcpy(outBuf, slot.data, (size_t)len);
	gMp.outHead = (gMp.outHead + 1) % MP_MAX_OUT_QUEUE;
	gMp.outCount--;
	return len;
}

MpRemotePlayer *MP_GetOrCreateRemotePlayer(int32 peerId)
{
	if (peerId <= 0 || peerId == gMp.localPeerId) return nil;
	int32 emptyIdx = -1;
	for (int32 i = 0; i < MP_MAX_PLAYERS; i++) {
		if (gMp.players[i].active && gMp.players[i].peerId == peerId)
			return &gMp.players[i];
		if (!gMp.players[i].active && emptyIdx < 0)
			emptyIdx = i;
	}
	if (emptyIdx < 0) return nil;
	MpRemotePlayer &rp = gMp.players[emptyIdx];
	memset(&rp, 0, sizeof(rp));
	rp.active = true;
	rp.hasState = false;
	rp.peerId = peerId;
	rp.netVehId = -1;
	rp.seatIdx = -1;
	rp.blipHandle = -1;
	rp.health = 100;
	rp.lastUpdateMs = CTimer::GetTimeInMilliseconds();
	snprintf(rp.name, MP_MAX_PLAYER_NAME, "Player %d", peerId);
	return &rp;
}

void MP_OnPeerDisconnected(int32 peerId)
{
	if (!gMp.initialized) return;
	for (int32 i = 0; i < MP_MAX_PLAYERS; i++) {
		MpRemotePlayer &rp = gMp.players[i];
		if (rp.active && rp.peerId == peerId) {
			if (rp.blipHandle >= 0) {
				CRadar::ClearBlip(rp.blipHandle);
				rp.blipHandle = -1;
			}
			if (rp.ped && MP_IsPoolPedValid(rp.ped)) {
				if (rp.ped->bInVehicle && rp.ped->m_pMyVehicle && MP_IsPoolVehicleValid(rp.ped->m_pMyVehicle)) {
					if (rp.ped->m_pMyVehicle->pDriver == rp.ped) rp.ped->m_pMyVehicle->RemoveDriver();
					else rp.ped->m_pMyVehicle->RemovePassenger(rp.ped);
				}
				CWorld::Remove(rp.ped);
				delete rp.ped;
			}
			rp.ped = nil;
			rp.hasState = false;
			rp.active = false;
			rp.peerId = -1;
		}
	}
	for (int32 i = 0; i < MP_MAX_SYNCED_VEHICLES; i++) {
		if (gMp.vehicles[i].active && gMp.vehicles[i].ownerPeerId == peerId) {
			gMp.vehicles[i].ownerPeerId = gMp.isHost ? gMp.localPeerId : -1;
			if (gMp.isHost && gMp.vehicles[i].veh && MP_IsPoolVehicleValid(gMp.vehicles[i].veh)) {
				gMp.vehicles[i].veh->SetStatus(STATUS_ABANDONED);
			}
		}
	}
	for (int32 i = 0; i < MP_MAX_SYNCED_PEDS; i++) {
		if (gMp.peds[i].active && gMp.peds[i].ownerPeerId == peerId) {
			if (gMp.peds[i].ped && MP_IsPoolPedValid(gMp.peds[i].ped)) {
				CWorld::Remove(gMp.peds[i].ped);
				delete gMp.peds[i].ped;
			}
			gMp.peds[i].ped = nil;
			gMp.peds[i].active = false;
		}
	}
}

void MP_SetPlayerInfo(int32 peerId, const char *name, int32 modelIndex)
{
	if (!gMp.initialized) MP_Init(peerId > 0 ? peerId : 1, true);
	if (peerId <= 0 || peerId == gMp.localPeerId) {
		if (name && name[0]) {
			strncpy(gMp.localName, name, MP_MAX_PLAYER_NAME - 1);
			gMp.localName[MP_MAX_PLAYER_NAME - 1] = '\0';
		}
		gMp.localSkinModel = Clamp(modelIndex, 0, 129);
		MP_ApplyLocalSkin(gMp.localSkinModel);

		// Broadcast reliable player metadata
		uint8 pkt[64];
		memset(pkt, 0, sizeof(pkt));
		pkt[0] = MP_PKT_PLAYER_META;
		int32 p = 1;
		memcpy(pkt + p, &gMp.localSkinModel, 4); p += 4;
		strncpy((char *)(pkt + p), gMp.localName, MP_MAX_PLAYER_NAME - 1);
		p += MP_MAX_PLAYER_NAME;
		MP_EnqueuePacket(-1, 1, pkt, p);
	} else {
		MpRemotePlayer *rp = MP_GetOrCreateRemotePlayer(peerId);
		if (rp) {
			if (name && name[0]) {
				strncpy(rp->name, name, MP_MAX_PLAYER_NAME - 1);
				rp->name[MP_MAX_PLAYER_NAME - 1] = '\0';
			}
			rp->skinModel = Clamp(modelIndex, 0, 129);
		}
	}
}

void MP_PushNetPacket(int32 fromPeerId, const uint8 *data, int32 len)
{
	if (!gMp.initialized || !data || len < 1) return;
	uint8 type = data[0];
	switch (type) {
	case MP_PKT_PLAYER_STATE:  MP_HandlePlayerStatePacket(fromPeerId, data, len); break;
	case MP_PKT_VEHICLE_SYNC:  MP_HandleVehicleSyncPacket(fromPeerId, data, len); break;
	case MP_PKT_VEHICLE_CLAIM: MP_HandleVehicleClaimPacket(fromPeerId, data, len); break;
	case MP_PKT_PED_SYNC:      MP_HandlePedSyncPacket(fromPeerId, data, len); break;
	case MP_PKT_SHOT_FIRED:    MP_HandleShotFiredPacket(fromPeerId, data, len); break;
	case MP_PKT_PICKUP_EVENT:  MP_HandlePickupEventPacket(fromPeerId, data, len); break;
	case MP_PKT_GARAGE_EVENT:  MP_HandleGarageEventPacket(fromPeerId, data, len); break;
	case MP_PKT_WEATHER_TIME:  MP_HandleWeatherTimePacket(fromPeerId, data, len); break;
	case MP_PKT_PLAYER_META:   MP_HandlePlayerMetaPacket(fromPeerId, data, len); break;
	case MP_PKT_SPAWN_REQ:     MP_HandleSpawnReqPacket(fromPeerId, data, len); break;
	case MP_PKT_REMOTE_CAR:    MP_HandleRemoteCarPacket(fromPeerId, data, len); break;
	case MP_PKT_WORLD_RESET:   MP_HandleWorldResetPacket(fromPeerId, data, len); break;
	default: break;
	}
}

void MP_TickBeforeGame(float dt)
{
	if (!gMp.initialized) return;
	// Keep multiplayer tabs unpaused when active in a room or pause-blocked
	int activePeers = 0;
	for (int32 i = 0; i < MP_MAX_PLAYERS; i++) {
		if (gMp.players[i].active) activePeers++;
	}
	CTimer::ms_bDisableUserPause = (activePeers > 0 || gMp.uiPauseBlocked);

	MP_UpdateRemotePlayersBefore(dt);
	MP_TickVehiclesBefore(dt);
	MP_TickAmbientPedsBefore(dt);
	MP_TickWorldBefore(dt);
}

void MP_TickAfterGame(float dt)
{
	if (!gMp.initialized) return;
	MP_ProcessPassengerEntry();
	MP_UpdateRemotePlayersAfter(dt);
	MP_SendLocalPlayerState();
	MP_TickVehiclesAfter(dt);
	MP_TickAmbientPedsAfter(dt);
	MP_TickWorldAfter(dt);
}

void MP_GetLocalPlayerState(float *outFloats, int32 *outInts)
{
	if (!outFloats || !outInts) return;
	CPlayerPed *ped = FindPlayerPed();
	if (!ped) return;
	CVector pos = ped->GetPosition();
	outFloats[0] = pos.x;
	outFloats[1] = pos.y;
	outFloats[2] = pos.z;
	outFloats[3] = ped->m_fRotationCur;
	outFloats[4] = ped->m_fHealth;
	outFloats[5] = ped->m_fArmour;
	outInts[0] = gMp.localPeerId;
	outInts[1] = (int32)ped->GetWeapon()->m_eWeaponType;
	outInts[2] = ped->bInVehicle ? 1 : 0;
	outInts[3] = CGame::currArea;
	outInts[4] = CWorld::Players[CWorld::PlayerInFocus].m_nMoney;
	outInts[5] = ped->m_pWanted ? (int32)ped->m_pWanted->GetWantedLevel() : 0;
}

uint16 MP_EncodeAngle16(float rad)
{
	while (rad < 0.0f) rad += TWOPI;
	while (rad >= TWOPI) rad -= TWOPI;
	return (uint16)((rad / TWOPI) * 65535.0f);
}

float MP_DecodeAngle16(uint16 v)
{
	return ((float)v / 65535.0f) * TWOPI;
}

float MP_LerpAngle(float a, float b, float t)
{
	float diff = b - a;
	while (diff > PI) diff -= TWOPI;
	while (diff < -PI) diff += TWOPI;
	return a + diff * t;
}

bool MP_IsRemotePlayerPed(const CPed *ped)
{
	if (!gMp.initialized || !ped) return false;
	for (int32 i = 0; i < MP_MAX_PLAYERS; i++) {
		if (gMp.players[i].active && gMp.players[i].ped == ped)
			return true;
	}
	return false;
}

bool MP_IsSyncedVehicle(const CVehicle *veh)
{
	if (!gMp.initialized || !veh) return false;
	for (int32 i = 0; i < MP_MAX_SYNCED_VEHICLES; i++) {
		if (gMp.vehicles[i].active && gMp.vehicles[i].veh == veh)
			return true;
	}
	return false;
}

bool MP_IsNearAnyPlayer2D(const CVector &pos, float maxDist)
{
	if (!gMp.initialized) return false;
	float maxSq = maxDist * maxDist;
	for (int32 i = 0; i < MP_MAX_PLAYERS; i++) {
		const MpRemotePlayer &rp = gMp.players[i];
		if (!rp.active) continue;
		float dx = pos.x - rp.pos.x;
		float dy = pos.y - rp.pos.y;
		if (dx * dx + dy * dy < maxSq)
			return true;
	}
	return false;
}

void MP_GetSpawnFocusCoors(CVector *inOutPos)
{
	if (!gMp.initialized || !inOutPos) return;
	// Round-robin traffic/ped focus between local & active peers
	int32 count = 1;
	int32 choices[MP_MAX_PLAYERS];
	choices[0] = -1;
	for (int32 i = 0; i < MP_MAX_PLAYERS; i++) {
		if (gMp.players[i].active && gMp.players[i].area == (uint8)CGame::currArea && count < MP_MAX_PLAYERS)
			choices[count++] = i;
	}
	if (count <= 1) return;
	uint32 slot = (gMp.seqCounter++) % (uint32)count;
	int32 idx = choices[slot];
	if (idx >= 0) {
		*inOutPos = gMp.players[idx].pos;
	}
}

#endif // MULTIPLAYER_COOP
