#include "mp_core.h"

#ifdef MULTIPLAYER_COOP

#include "Frontend.h"
#include "Game.h"
#include "Zones.h"
#include "Wanted.h"
#include "Timer.h"
#include "DMAudio.h"
#include "World.h"
#include "Script.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {

void mp_init(int32 localPeerId, int32 isHost)
{
	MP_Init(localPeerId, isHost != 0);
}

void mp_shutdown(void)
{
	MP_Shutdown();
}

void mp_on_peer_disconnected(int32 peerId)
{
	MP_OnPeerDisconnected(peerId);
}

void mp_set_player_info(int32 peerId, const char *name, int32 modelIndex)
{
	MP_SetPlayerInfo(peerId, name, modelIndex);
}

void mp_get_local_player_state(float *outFloats, int32 *outInts)
{
	MP_GetLocalPlayerState(outFloats, outInts);
}

void mp_apply_remote_player(int32 peerId, float x, float y, float z, float heading, int32 modelIndex)
{
	MpRemotePlayer *rp = MP_GetOrCreateRemotePlayer(peerId);
	if (!rp) return;
	rp->targetPos = CVector(x, y, z);
	rp->targetHeading = heading;
	if (modelIndex >= 0) rp->skinModel = modelIndex;
	rp->lastUpdateMs = CTimer::GetTimeInMilliseconds();
}

void mp_push_net_packet(int32 fromPeerId, const uint8 *data, int32 len)
{
	MP_PushNetPacket(fromPeerId, data, len);
}

int32 mp_poll_net_packet(int32 *outChannel, int32 *outTargetPeer, uint8 *outBuf, int32 maxLen)
{
	return MP_PollNetPacket(outChannel, outTargetPeer, outBuf, maxLen);
}

void mp_exec_cheat(int32 cheatId, int32 param1, int32 param2)
{
	MP_ExecCheat(cheatId, param1, param2);
}

void mp_spawn_vehicle(int32 modelIndex, float aheadDist)
{
	MP_SpawnVehicle(modelIndex, aheadDist);
}

void mp_spawn_pickup(int32 pickupKind, int32 weaponOrAmount)
{
	MP_SpawnPickup(pickupKind, weaponOrAmount);
}

void mp_set_open_islands(int32 open)
{
	MP_ApplyOpenIslands(open);
}

int32 mp_export_save_state(uint8 **outPtr)
{
	if (!outPtr) return 0;
	int32 sz = 0;
	uint8 *data = MP_ExportSaveData(&sz);
	*outPtr = data;
	return sz;
}

void mp_free_save_state(uint8 *ptr)
{
	if (ptr) free(ptr);
}

int32 mp_import_save_state(const uint8 *data, int32 len)
{
	return MP_ImportSaveData(data, len) ? 1 : 0;
}

void mp_trigger_restart(int32 newGame)
{
	if (newGame == 2) {
		CTheScripts::ScriptToLoad = 1;
		gMp.freeRoam = true;
		gMp.openIslands = true;
	} else if (newGame == 1) {
		CTheScripts::ScriptToLoad = 0;
		gMp.freeRoam = false;
	}
	FrontEndMenuManager.m_bWantToLoad = false;
	FrontEndMenuManager.m_bFirstTime = false;
	FrontEndMenuManager.m_bWantToRestart = true;
}

void mp_set_ui_pause_block(int32 block)
{
	gMp.uiPauseBlocked = (block != 0);
	CTimer::ms_bDisableUserPause = gMp.uiPauseBlocked;
}

void mp_get_hud_state(char *outBuf, int32 bufLen)
{
	if (!outBuf || bufLen <= 0) return;
	CPlayerPed *ped = FindPlayerPed();
	float hp = ped ? ped->m_fHealth : 100.0f;
	float arm = ped ? ped->m_fArmour : 0.0f;
	int32 cash = CWorld::Players[CWorld::PlayerInFocus].m_nMoney;
	int32 wanted = (ped && ped->m_pWanted) ? (int32)ped->m_pWanted->GetWantedLevel() : 0;
	CVector pos = ped ? ped->GetPosition() : CVector(0, 0, 0);
	const char *zoneName = "Vice City";
	CZone *z = CTheZones::FindSmallestNavigationZoneForPosition(&pos, true, true);
	if (z && z->name[0]) zoneName = z->name;

	int32 off = snprintf(outBuf, (size_t)bufLen,
		"{\"hp\":%.1f,\"armor\":%.1f,\"cash\":%d,\"wanted\":%d,\"area\":%d,\"district\":\"%s\",\"peers\":[",
		hp, arm, cash, wanted, CGame::currArea, zoneName);

	bool first = true;
	for (int32 i = 0; i < MP_MAX_PLAYERS && off < bufLen - 128; i++) {
		const MpRemotePlayer &rp = gMp.players[i];
		if (!rp.active) continue;
		off += snprintf(outBuf + off, (size_t)(bufLen - off),
			"%s{\"peerId\":%d,\"name\":\"%s\",\"hp\":%d,\"area\":%d,\"inVehicle\":%d}",
			first ? "" : ",", rp.peerId, rp.name, (int)rp.health, (int)rp.area,
			(rp.actionFlags & MP_ACT_IN_VEHICLE) ? 1 : 0);
		first = false;
	}
	snprintf(outBuf + off, (size_t)(bufLen - off), "]}");
}

void mp_set_frontend_setting(const char *key, float val)
{
	if (!key) return;
	if (strcmp(key, "music_vol") == 0) {
		FrontEndMenuManager.m_PrefsMusicVolume = (int8)Clamp((int32)val, 0, 127);
		DMAudio.SetMusicMasterVolume(FrontEndMenuManager.m_PrefsMusicVolume);
	} else if (strcmp(key, "sfx_vol") == 0) {
		FrontEndMenuManager.m_PrefsSfxVolume = (int8)Clamp((int32)val, 0, 127);
		DMAudio.SetEffectsMasterVolume(FrontEndMenuManager.m_PrefsSfxVolume);
	} else if (strcmp(key, "radio_station") == 0) {
		FrontEndMenuManager.m_PrefsRadioStation = (int8)Clamp((int32)val, 0, 9);
		DMAudio.SetRadioInCar(FrontEndMenuManager.m_PrefsRadioStation);
	} else if (strcmp(key, "brightness") == 0) {
		FrontEndMenuManager.m_PrefsBrightness = Clamp((int32)val, 0, 384);
	} else if (strcmp(key, "draw_dist") == 0) {
		FrontEndMenuManager.m_PrefsLOD = val;
	} else if (strcmp(key, "subtitles") == 0) {
		FrontEndMenuManager.m_PrefsShowSubtitles = val != 0.0f ? 1 : 0;
	} else if (strcmp(key, "widescreen") == 0) {
		FrontEndMenuManager.m_PrefsUseWideScreen = val != 0.0f ? 1 : 0;
	} else if (strcmp(key, "invert_y") == 0) {
		FrontEndMenuManager.m_PrefsInvertVertically = (val != 0.0f);
	} else if (strcmp(key, "show_hud") == 0) {
		FrontEndMenuManager.m_PrefsShowHud = (val != 0.0f);
	} else if (strcmp(key, "control_method") == 0) {
		FrontEndMenuManager.m_ControlMethod = Clamp((int32)val, 0, 1);
	}
}

void mp_get_frontend_settings(char *outBuf, int32 bufLen)
{
	if (!outBuf || bufLen <= 0) return;
	snprintf(outBuf, (size_t)bufLen,
		"{\"music_vol\":%d,\"sfx_vol\":%d,\"radio_station\":%d,\"brightness\":%d,"
		"\"draw_dist\":%.2f,\"subtitles\":%d,\"widescreen\":%d,\"invert_y\":%d,"
		"\"show_hud\":%d,\"control_method\":%d}",
		(int)FrontEndMenuManager.m_PrefsMusicVolume,
		(int)FrontEndMenuManager.m_PrefsSfxVolume,
		(int)FrontEndMenuManager.m_PrefsRadioStation,
		FrontEndMenuManager.m_PrefsBrightness,
		FrontEndMenuManager.m_PrefsLOD,
		(int)FrontEndMenuManager.m_PrefsShowSubtitles,
		(int)FrontEndMenuManager.m_PrefsUseWideScreen,
		FrontEndMenuManager.m_PrefsInvertVertically ? 1 : 0,
		FrontEndMenuManager.m_PrefsShowHud ? 1 : 0,
		(int)FrontEndMenuManager.m_ControlMethod);
}

} // extern "C"

#endif // MULTIPLAYER_COOP
