#pragma once

#include "common.h"

#ifdef MULTIPLAYER_COOP

#include "Ped.h"
#include "PlayerPed.h"
#include "Vehicle.h"
#include "Automobile.h"
#include "Bike.h"
#include "Boat.h"

#define MP_MAX_PLAYERS          4
#define MP_MAX_SYNCED_VEHICLES  128
#define MP_MAX_SYNCED_PEDS      64
#define MP_MAX_SYNCED_PICKUPS   64
#define MP_MAX_OUT_QUEUE        512
#define MP_MAX_PACKET_SIZE      512
#define MP_MAX_PLAYER_NAME      24

enum MpPacketType {
	MP_PKT_PLAYER_STATE   = 1,
	MP_PKT_VEHICLE_SYNC   = 2,
	MP_PKT_VEHICLE_CLAIM  = 3,
	MP_PKT_PED_SYNC       = 4,
	MP_PKT_SHOT_FIRED     = 5,
	MP_PKT_PICKUP_EVENT   = 6,
	MP_PKT_GARAGE_EVENT   = 7,
	MP_PKT_WEATHER_TIME   = 8,
	MP_PKT_PLAYER_META    = 9,
	MP_PKT_SPAWN_REQ      = 10,
	MP_PKT_REMOTE_CAR     = 11,
	MP_PKT_WORLD_RESET    = 12,
	MP_PKT_VEHICLE_REMOVE = 13,
	MP_PKT_PED_REMOVE     = 14
};

enum MpActionFlags {
	MP_ACT_IN_VEHICLE     = 1 << 0,
	MP_ACT_PASSENGER      = 1 << 1,
	MP_ACT_FIRING         = 1 << 2,
	MP_ACT_SPRINTING      = 1 << 3,
	MP_ACT_DUCKING        = 1 << 4,
	MP_ACT_JUMPING        = 1 << 5,
	MP_ACT_DEAD           = 1 << 6,
	MP_ACT_AIMING         = 1 << 7,
	MP_ACT_ENTERING_VEH   = 1 << 8,
	MP_ACT_EXITING_VEH    = 1 << 9
};

enum MpSpawnReqKind {
	MP_SPAWN_REQ_VEHICLE  = 0,
	MP_SPAWN_REQ_PICKUP   = 1
};

enum MpGarageAction {
	MP_GARAGE_RESPRAY     = 0,
	MP_GARAGE_BOMB1       = 1,
	MP_GARAGE_BOMB2       = 2,
	MP_GARAGE_BOMB3       = 3,
	MP_GARAGE_CRUSH       = 4,
	MP_GARAGE_DETONATE    = 5
};

enum MpVehicleKind {
	MP_VEHKIND_CAR  = 0,
	MP_VEHKIND_BIKE = 1,
	MP_VEHKIND_BOAT = 2
};

struct MpOutPacket {
	int32 targetPeer; // -1 = broadcast
	uint8 channel;    // 0 = unreliable, 1 = reliable
	uint16 size;
	uint8 data[MP_MAX_PACKET_SIZE];
};

struct MpRemotePlayer {
	bool active;
	bool hasState;
	int32 peerId;
	char name[MP_MAX_PLAYER_NAME];
	int32 skinModel;
	uint32 lastUpdateMs;

	CVector pos;
	CVector targetPos;
	CVector vel;
	float heading;
	float targetHeading;
	float aimPitch;

	uint8 health;
	uint8 armor;
	uint8 weapon;
	uint16 actionFlags;
	int32 netVehId;
	int8 seatIdx;
	uint8 area;
	uint8 pedState;
	uint8 moveState;
	uint8 vehDoorCode;
	uint8 animGroup;
	int16 animId;
	float animTime;
	int32 lastDoorVehId;
	uint8 lastDoorCode;

	CPed *ped;
	int32 blipHandle;
	uint32 lastShotFxMs;
	uint8 lastWeaponGiven;
};

struct MpSyncedVehicle {
	bool active;
	int32 netVehId;
	int32 ownerPeerId;
	uint16 modelIndex;
	uint8 vehKind;
	uint8 color1;
	uint8 color2;
	uint8 siren;
	uint8 bombType;
	uint8 tireMask;
	uint8 area;
	uint8 driverPeerId;
	uint8 vflags;
	uint16 npcDriverModel;
	float health;
	float rotorSpeed;
	float steerAngle;
	float gasPedal;
	float brakePedal;

	CVector pos;
	CVector targetPos;
	CVector vel;
	CVector turn;
	CVector right;
	CVector up;
	CVector at;

	uint32 lastUpdateMs;
	uint32 lastOwnedLocalMs;
	uint32 releaseOwnerAtMs;
	CVehicle *veh;
	CPed *npcDriverPed;
};

struct MpSyncedPed {
	bool active;
	int32 netPedId;
	int32 ownerPeerId;
	uint16 modelIndex;
	uint8 pedType;
	uint8 weapon;
	uint8 health;
	uint8 pedState;
	uint8 moveState;
	uint8 flags;
	uint8 area;

	CVector pos;
	CVector targetPos;
	CVector vel;
	float heading;
	float targetHeading;

	uint32 lastUpdateMs;
	CPed *ped;
};

struct MpState {
	bool initialized;
	bool isHost;
	int32 localPeerId;
	int32 localSkinModel;
	char localName[MP_MAX_PLAYER_NAME];
	bool openIslands;
	bool freeRoam;
	bool uiPauseBlocked;

	uint32 seqCounter;
	uint32 lastPlayerSendMs;
	uint32 lastVehSendMs;
	uint32 lastPedSendMs;
	uint32 lastWorldSendMs;
	uint32 lastPurgeMs;
	uint32 vehScanCursor;
	uint32 pedScanCursor;
	int32 nextNetVehId;
	int32 nextNetPedId;

	MpRemotePlayer players[MP_MAX_PLAYERS];
	MpSyncedVehicle vehicles[MP_MAX_SYNCED_VEHICLES];
	MpSyncedPed peds[MP_MAX_SYNCED_PEDS];

	MpOutPacket outQueue[MP_MAX_OUT_QUEUE];
	int32 outHead;
	int32 outTail;
	int32 outCount;
};

extern MpState gMp;

// Core lifecycle & packet queue
void MP_Init(int32 localPeerId, bool isHost);
void MP_Shutdown(void);
void MP_TickBeforeGame(float dt);
void MP_TickAfterGame(float dt);
void MP_RenderNameTags(void);
void MP_OnPeerDisconnected(int32 peerId);
void MP_SetPlayerInfo(int32 peerId, const char *name, int32 modelIndex);
void MP_PushNetPacket(int32 fromPeerId, const uint8 *data, int32 len);
int32 MP_PollNetPacket(int32 *outChannel, int32 *outTargetPeer, uint8 *outBuf, int32 maxLen);
void MP_EnqueuePacket(int32 targetPeer, uint8 channel, const uint8 *data, int32 len);
MpRemotePlayer *MP_GetOrCreateRemotePlayer(int32 peerId);
void MP_GetLocalPlayerState(float *outFloats, int32 *outInts);

// Utility helpers
uint16 MP_EncodeAngle16(float rad);
float MP_DecodeAngle16(uint16 v);
float MP_LerpAngle(float a, float b, float t);
int32 MP_ResolvePedModelIndex(int32 skinId);
void MP_ApplyLocalSkin(int32 skinModel);

bool MP_IsPoolPedValid(const CPed *ped);
bool MP_IsPoolVehicleValid(const CVehicle *veh);

// Peds & players
void MP_UpdateRemotePlayersBefore(float dt);
void MP_UpdateRemotePlayersAfter(float dt);
void MP_HandlePlayerStatePacket(int32 fromPeerId, const uint8 *data, int32 len);
void MP_HandlePlayerMetaPacket(int32 fromPeerId, const uint8 *data, int32 len);
void MP_HandlePedSyncPacket(int32 fromPeerId, const uint8 *data, int32 len);
void MP_HandlePedRemovePacket(int32 fromPeerId, const uint8 *data, int32 len);
void MP_SendLocalPlayerState(void);
void MP_TickAmbientPedsBefore(float dt);
void MP_TickAmbientPedsAfter(float dt);
void MP_CleanupRemotePlayers(void);
void MP_CleanupRemotePeds(void);
void MP_ProcessPassengerEntry(void);
void MP_ProcessPassengerDriveBy(CVehicle *veh);

// Vehicles
void MP_TickVehiclesBefore(float dt);
void MP_TickVehiclesAfter(float dt);
void MP_HandleVehicleSyncPacket(int32 fromPeerId, const uint8 *data, int32 len);
void MP_HandleVehicleClaimPacket(int32 fromPeerId, const uint8 *data, int32 len);
void MP_HandleVehicleRemovePacket(int32 fromPeerId, const uint8 *data, int32 len);
void MP_CleanupRemoteVehicles(void);
int32 MP_FindVehicleSlotByPtr(CVehicle *veh);
int32 MP_FindVehicleSlotByNetId(int32 netVehId);
int32 MP_TrackLocalVehicle(CVehicle *veh);
CVehicle *MP_GetVehiclePtrByNetId(int32 netVehId);
uint8 MP_ReadVehicleTireMask(CVehicle *veh);
void MP_ApplyVehicleTireMask(CVehicle *veh, uint8 mask);
CVehicle *MP_SpawnVehicleLocal(int32 modelIndex, float aheadDist);
bool MP_OnLocalEnterVehicleAttempt(CPed *ped, CVehicle *veh, bool asPassenger);
void MP_OnVehicleDestroyed(CVehicle *veh);

// World, pickups, garages, cheats & saves
void MP_TickWorldBefore(float dt);
void MP_TickWorldAfter(float dt);
void MP_ApplyOpenIslands(int32 mode);
void MP_ExecCheat(int32 cheatId, int32 param1, int32 param2);
void MP_SpawnVehicle(int32 modelIndex, float aheadDist);
void MP_SpawnPickup(int32 pickupKind, int32 weaponOrAmount);
void MP_HandleShotFiredPacket(int32 fromPeerId, const uint8 *data, int32 len);
void MP_HandlePickupEventPacket(int32 fromPeerId, const uint8 *data, int32 len);
void MP_HandleGarageEventPacket(int32 fromPeerId, const uint8 *data, int32 len);
void MP_HandleWeatherTimePacket(int32 fromPeerId, const uint8 *data, int32 len);
void MP_HandleSpawnReqPacket(int32 fromPeerId, const uint8 *data, int32 len);
void MP_HandleRemoteCarPacket(int32 fromPeerId, const uint8 *data, int32 len);
void MP_HandleWorldResetPacket(int32 fromPeerId, const uint8 *data, int32 len);

void MP_OnShotFired(int32 weaponType, const CVector &src, const CVector &dir, CEntity *target, float dmg, bool isDriveBy);
void MP_OnPickupEvent(int32 slot, uint8 action, int32 modelIndex, uint8 type, int32 quantity, const CVector &pos);
void MP_OnGarageAction(int32 garageIdx, uint8 action, CVehicle *veh, uint8 col1, uint8 col2, uint8 extra);
void MP_OnRemoteCarEvent(uint8 action, const CVector &pos, float heading, CVehicle *veh);
void MP_OnWorldReset(void);

uint8 *MP_ExportSaveData(int32 *outSize);
bool MP_ImportSaveData(const uint8 *data, int32 size);

// Engine query hooks
bool MP_IsClientSession(void);
bool MP_IsRemotePlayerPed(const CPed *ped);
bool MP_IsSyncedPed(const CPed *ped);
bool MP_IsSyncedVehicle(const CVehicle *veh);
bool MP_IsVehicleOccupiedByPlayer(const CVehicle *veh);
void MP_PurgeUnsyncedClientEntities(void);
bool MP_IsNearAnyPlayer2D(const CVector &pos, float maxDist);
void MP_GetSpawnFocusCoors(CVector *inOutPos);

#endif // MULTIPLAYER_COOP
