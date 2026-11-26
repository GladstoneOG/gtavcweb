#ifndef __GTA_OAL_UTILS_H__
#define __GTA_OAL_UTILS_H__

#ifdef AUDIO_OAL
#include "eax.h"
#ifdef __EMSCRIPTEN__
#include <AL/al.h>
#include <AL/alc.h>
#ifndef AL_EFFECT_NULL
#define AL_EFFECT_NULL 0
#define AL_EFFECT_TYPE 0x8001
#define AL_EFFECTSLOT_NULL 0
#define AL_EFFECTSLOT_EFFECT 0x0001
#define AL_FILTER_NULL 0
#define AL_AUXILIARY_SEND_FILTER 0x20006
#define AL_DIRECT_FILTER 0x20005
#define ALC_EXT_EFX_NAME "ALC_EXT_EFX"
#define ALC_ALL_DEVICES_SPECIFIER 0x1013
#define ALC_DEFAULT_ALL_DEVICES_SPECIFIER 0x1012
#define ALC_CONNECTED 0x313
#define ALC_HRTF_SOFT 0x1992
#define ALC_DONT_CARE_SOFT 0x0002
#endif
#else
#include "AL/efx.h"
#endif

void EFXInit();
void EAX3_Set(ALuint effect, const EAXLISTENERPROPERTIES *props);
void EFX_Set(ALuint effect, const EAXLISTENERPROPERTIES *props);
void EAX3_SetReverbMix(ALuint filter, float mix);
void SetEffectsLevel(ALuint uiFilter, float level);

#ifdef __EMSCRIPTEN__
namespace re3_openal {
inline void alGenEffects(ALsizei, ALuint *) {}
inline void alDeleteEffects(ALsizei, const ALuint *) {}
inline ALboolean alIsEffect(ALuint) { return AL_FALSE; }
inline void alEffecti(ALuint, ALenum, ALint) {}
inline void alEffectf(ALuint, ALenum, ALfloat) {}
inline void alGenAuxiliaryEffectSlots(ALsizei, ALuint *) {}
inline void alDeleteAuxiliaryEffectSlots(ALsizei, const ALuint *) {}
inline ALboolean alIsAuxiliaryEffectSlot(ALuint) { return AL_FALSE; }
inline void alAuxiliaryEffectSloti(ALuint, ALenum, ALint) {}
inline void alGenFilters(ALsizei, ALuint *) {}
inline void alDeleteFilters(ALsizei, const ALuint *) {}
inline void alFilteri(ALuint, ALenum, ALint) {}
inline void alFilterf(ALuint, ALenum, ALfloat) {}
}
#else
namespace re3_openal {

extern LPALGENEFFECTS alGenEffects;
extern LPALDELETEEFFECTS alDeleteEffects;
extern LPALISEFFECT alIsEffect;
extern LPALEFFECTI alEffecti;
extern LPALEFFECTIV alEffectiv;
extern LPALEFFECTF alEffectf;
extern LPALEFFECTFV alEffectfv;
extern LPALGETEFFECTI alGetEffecti;
extern LPALGETEFFECTIV alGetEffectiv;
extern LPALGETEFFECTF alGetEffectf;
extern LPALGETEFFECTFV alGetEffectfv;
extern LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots;
extern LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots;
extern LPALISAUXILIARYEFFECTSLOT alIsAuxiliaryEffectSlot;
extern LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti;
extern LPALAUXILIARYEFFECTSLOTIV alAuxiliaryEffectSlotiv;
extern LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf;
extern LPALAUXILIARYEFFECTSLOTFV alAuxiliaryEffectSlotfv;
extern LPALGETAUXILIARYEFFECTSLOTI alGetAuxiliaryEffectSloti;
extern LPALGETAUXILIARYEFFECTSLOTIV alGetAuxiliaryEffectSlotiv;
extern LPALGETAUXILIARYEFFECTSLOTF alGetAuxiliaryEffectSlotf;
extern LPALGETAUXILIARYEFFECTSLOTFV alGetAuxiliaryEffectSlotfv;
extern LPALGENFILTERS alGenFilters;
extern LPALDELETEFILTERS alDeleteFilters;
extern LPALISFILTER alIsFilter;
extern LPALFILTERI alFilteri;
extern LPALFILTERIV alFilteriv;
extern LPALFILTERF alFilterf;
extern LPALFILTERFV alFilterfv;
extern LPALGETFILTERI alGetFilteri;
extern LPALGETFILTERIV alGetFilteriv;
extern LPALGETFILTERF alGetFilterf;
extern LPALGETFILTERFV alGetFilterfv;

}
#endif

using namespace re3_openal;

#endif

#endif // __GTA_OAL_UTILS_H__
