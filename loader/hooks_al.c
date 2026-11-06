/* hooks_al.c -- the OpenAL Soft built into libMaxPayne.so (with an OpenSL ES
 * backend) is bypassed: its entry points jump to the OpenAL Soft in the
 * folder's lib/ (ALSA, settings in alsoft.conf). Calls that pass or
 * return floats go through SOFTFP wrappers.
 * After max_vita's hooks/openal.c (fgsfds, Andy Nguyen, MIT). */
#define AL_ALEXT_PROTOTYPES
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>
#include <string.h>

#include "so_util.h"
#include "util.h"

/* The game stops a source and deletes its buffers right away, without
 * unqueueing them first; do that for it. A stream may already hold the
 * first buffer of its next track by then: the whole queue comes off and the
 * buffers that stay are queued again in order, or the next track would be
 * played from an empty queue and stay silent (found in GTA SA's intro). */
static ALuint last_stopped_src;

static void unqueue_deleted(ALuint src, ALsizei n, const ALuint *bufs)
{
	ALuint q[64];
	ALint queued = 0;
	alGetSourcei(src, AL_BUFFERS_QUEUED, &queued);
	if (queued <= 0 || queued > 64)
		return;
	alGetError();
	alSourceUnqueueBuffers(src, queued, q);
	if (alGetError() != AL_NO_ERROR)
		return; /* not all processed (a rewound source): leave it as it is */
	for (ALint i = 0; i < queued; i++) {
		ALsizei j = 0;
		while (j < n && bufs[j] != q[i])
			j++;
		if (j == n)
			alSourceQueueBuffers(src, 1, &q[i]);
	}
}

static void alSourceStopHook(ALuint src)
{
	last_stopped_src = src;
	alSourceStop(src);
}

static void alDeleteBuffersHook(ALsizei n, const ALuint *bufs)
{
	if (last_stopped_src) {
		ALint type = 0;
		alGetSourcei(last_stopped_src, AL_SOURCE_TYPE, &type);
		if (type == AL_STREAMING)
			unqueue_deleted(last_stopped_src, n, bufs);
		else
			alSourcei(last_stopped_src, AL_BUFFER, 0);
		last_stopped_src = 0;
	}
	alDeleteBuffers(n, bufs);
}

static SOFTFP void w_alAuxiliaryEffectSlotf(ALuint s, ALenum p, ALfloat v) { alAuxiliaryEffectSlotf(s, p, v); }
static SOFTFP void w_alBuffer3f(ALuint b, ALenum p, ALfloat x, ALfloat y, ALfloat z) { alBuffer3f(b, p, x, y, z); }
static SOFTFP void w_alBufferf(ALuint b, ALenum p, ALfloat v) { alBufferf(b, p, v); }
static SOFTFP void w_alEffectf(ALuint e, ALenum p, ALfloat v) { alEffectf(e, p, v); }
static SOFTFP void w_alFilterf(ALuint f, ALenum p, ALfloat v) { alFilterf(f, p, v); }
static SOFTFP void w_alListener3f(ALenum p, ALfloat x, ALfloat y, ALfloat z) { alListener3f(p, x, y, z); }
static SOFTFP void w_alListenerf(ALenum p, ALfloat v) { alListenerf(p, v); }
static SOFTFP void w_alSource3f(ALuint s, ALenum p, ALfloat x, ALfloat y, ALfloat z) { alSource3f(s, p, x, y, z); }
static SOFTFP void w_alSourcef(ALuint s, ALenum p, ALfloat v) { alSourcef(s, p, v); }
static SOFTFP void w_alDopplerFactor(ALfloat v) { alDopplerFactor(v); }
static SOFTFP void w_alDopplerVelocity(ALfloat v) { alDopplerVelocity(v); }
static SOFTFP void w_alSpeedOfSound(ALfloat v) { alSpeedOfSound(v); }
static SOFTFP ALfloat w_alGetFloat(ALenum p) { return alGetFloat(p); }
static SOFTFP ALdouble w_alGetDouble(ALenum p) { return alGetDouble(p); }

/* The game asks for a context at its own rate; mix at MP_AUDIO_RATE. */
static ALCcontext *alcCreateContextHook(ALCdevice *dev, const ALCint *attrs)
{
	ALCint a[32];
	int n = 0;
	int rate = env_int("MP_AUDIO_RATE", 44100);
	for (int i = 0; attrs && attrs[i] && n < 28; i += 2) {
		log_printf("al: context attribute 0x%x = %d\n", attrs[i], attrs[i + 1]);
		if (attrs[i] == ALC_FREQUENCY && rate > 0)
			continue;
		a[n++] = attrs[i];
		a[n++] = attrs[i + 1];
	}
	if (rate > 0) {
		a[n++] = ALC_FREQUENCY;
		a[n++] = rate;
	}
	a[n] = 0;
	return alcCreateContext(dev, a);
}

static void *alGetProcAddressHook(const ALchar *name);
static void *alcGetProcAddressHook(ALCdevice *dev, const ALCchar *name);

static const DynLibFunction al_funcs[] = {
	{ "alAuxiliaryEffectSlotf", (uintptr_t)w_alAuxiliaryEffectSlotf },
	{ "alAuxiliaryEffectSlotfv", (uintptr_t)alAuxiliaryEffectSlotfv },
	{ "alAuxiliaryEffectSloti", (uintptr_t)alAuxiliaryEffectSloti },
	{ "alAuxiliaryEffectSlotiv", (uintptr_t)alAuxiliaryEffectSlotiv },
	{ "alBuffer3f", (uintptr_t)w_alBuffer3f },
	{ "alBuffer3i", (uintptr_t)alBuffer3i },
	{ "alBufferData", (uintptr_t)alBufferData },
	{ "alBufferf", (uintptr_t)w_alBufferf },
	{ "alBufferfv", (uintptr_t)alBufferfv },
	{ "alBufferi", (uintptr_t)alBufferi },
	{ "alBufferiv", (uintptr_t)alBufferiv },
	{ "alDeleteAuxiliaryEffectSlots", (uintptr_t)alDeleteAuxiliaryEffectSlots },
	{ "alDeleteBuffers", (uintptr_t)alDeleteBuffersHook },
	{ "alDeleteEffects", (uintptr_t)alDeleteEffects },
	{ "alDeleteFilters", (uintptr_t)alDeleteFilters },
	{ "alDeleteSources", (uintptr_t)alDeleteSources },
	{ "alDisable", (uintptr_t)alDisable },
	{ "alDistanceModel", (uintptr_t)alDistanceModel },
	{ "alDopplerFactor", (uintptr_t)w_alDopplerFactor },
	{ "alDopplerVelocity", (uintptr_t)w_alDopplerVelocity },
	{ "alEffectf", (uintptr_t)w_alEffectf },
	{ "alEffectfv", (uintptr_t)alEffectfv },
	{ "alEffecti", (uintptr_t)alEffecti },
	{ "alEffectiv", (uintptr_t)alEffectiv },
	{ "alEnable", (uintptr_t)alEnable },
	{ "alFilterf", (uintptr_t)w_alFilterf },
	{ "alFilterfv", (uintptr_t)alFilterfv },
	{ "alFilteri", (uintptr_t)alFilteri },
	{ "alFilteriv", (uintptr_t)alFilteriv },
	{ "alGenAuxiliaryEffectSlots", (uintptr_t)alGenAuxiliaryEffectSlots },
	{ "alGenBuffers", (uintptr_t)alGenBuffers },
	{ "alGenEffects", (uintptr_t)alGenEffects },
	{ "alGenFilters", (uintptr_t)alGenFilters },
	{ "alGenSources", (uintptr_t)alGenSources },
	{ "alGetAuxiliaryEffectSlotf", (uintptr_t)alGetAuxiliaryEffectSlotf },
	{ "alGetAuxiliaryEffectSlotfv", (uintptr_t)alGetAuxiliaryEffectSlotfv },
	{ "alGetAuxiliaryEffectSloti", (uintptr_t)alGetAuxiliaryEffectSloti },
	{ "alGetAuxiliaryEffectSlotiv", (uintptr_t)alGetAuxiliaryEffectSlotiv },
	{ "alGetBoolean", (uintptr_t)alGetBoolean },
	{ "alGetBooleanv", (uintptr_t)alGetBooleanv },
	{ "alGetBuffer3f", (uintptr_t)alGetBuffer3f },
	{ "alGetBuffer3i", (uintptr_t)alGetBuffer3i },
	{ "alGetBufferf", (uintptr_t)alGetBufferf },
	{ "alGetBufferfv", (uintptr_t)alGetBufferfv },
	{ "alGetBufferi", (uintptr_t)alGetBufferi },
	{ "alGetBufferiv", (uintptr_t)alGetBufferiv },
	{ "alGetDouble", (uintptr_t)w_alGetDouble },
	{ "alGetDoublev", (uintptr_t)alGetDoublev },
	{ "alGetEffectf", (uintptr_t)alGetEffectf },
	{ "alGetEffectfv", (uintptr_t)alGetEffectfv },
	{ "alGetEffecti", (uintptr_t)alGetEffecti },
	{ "alGetEffectiv", (uintptr_t)alGetEffectiv },
	{ "alGetEnumValue", (uintptr_t)alGetEnumValue },
	{ "alGetError", (uintptr_t)alGetError },
	{ "alGetFilterf", (uintptr_t)alGetFilterf },
	{ "alGetFilterfv", (uintptr_t)alGetFilterfv },
	{ "alGetFilteri", (uintptr_t)alGetFilteri },
	{ "alGetFilteriv", (uintptr_t)alGetFilteriv },
	{ "alGetFloat", (uintptr_t)w_alGetFloat },
	{ "alGetFloatv", (uintptr_t)alGetFloatv },
	{ "alGetInteger", (uintptr_t)alGetInteger },
	{ "alGetIntegerv", (uintptr_t)alGetIntegerv },
	{ "alGetListener3f", (uintptr_t)alGetListener3f },
	{ "alGetListener3i", (uintptr_t)alGetListener3i },
	{ "alGetListenerf", (uintptr_t)alGetListenerf },
	{ "alGetListenerfv", (uintptr_t)alGetListenerfv },
	{ "alGetListeneri", (uintptr_t)alGetListeneri },
	{ "alGetListeneriv", (uintptr_t)alGetListeneriv },
	{ "alGetProcAddress", (uintptr_t)alGetProcAddressHook },
	{ "alGetSource3f", (uintptr_t)alGetSource3f },
	{ "alGetSource3i", (uintptr_t)alGetSource3i },
	{ "alGetSourcef", (uintptr_t)alGetSourcef },
	{ "alGetSourcefv", (uintptr_t)alGetSourcefv },
	{ "alGetSourcei", (uintptr_t)alGetSourcei },
	{ "alGetSourceiv", (uintptr_t)alGetSourceiv },
	{ "alGetString", (uintptr_t)alGetString },
	{ "alIsAuxiliaryEffectSlot", (uintptr_t)alIsAuxiliaryEffectSlot },
	{ "alIsBuffer", (uintptr_t)alIsBuffer },
	{ "alIsEffect", (uintptr_t)alIsEffect },
	{ "alIsEnabled", (uintptr_t)alIsEnabled },
	{ "alIsExtensionPresent", (uintptr_t)alIsExtensionPresent },
	{ "alIsFilter", (uintptr_t)alIsFilter },
	{ "alIsSource", (uintptr_t)alIsSource },
	{ "alListener3f", (uintptr_t)w_alListener3f },
	{ "alListener3i", (uintptr_t)alListener3i },
	{ "alListenerf", (uintptr_t)w_alListenerf },
	{ "alListenerfv", (uintptr_t)alListenerfv },
	{ "alListeneri", (uintptr_t)alListeneri },
	{ "alListeneriv", (uintptr_t)alListeneriv },
	{ "alSource3f", (uintptr_t)w_alSource3f },
	{ "alSource3i", (uintptr_t)alSource3i },
	{ "alSourcePause", (uintptr_t)alSourcePause },
	{ "alSourcePausev", (uintptr_t)alSourcePausev },
	{ "alSourcePlay", (uintptr_t)alSourcePlay },
	{ "alSourcePlayv", (uintptr_t)alSourcePlayv },
	{ "alSourceQueueBuffers", (uintptr_t)alSourceQueueBuffers },
	{ "alSourceRewind", (uintptr_t)alSourceRewind },
	{ "alSourceRewindv", (uintptr_t)alSourceRewindv },
	{ "alSourceStop", (uintptr_t)alSourceStopHook },
	{ "alSourceStopv", (uintptr_t)alSourceStopv },
	{ "alSourceUnqueueBuffers", (uintptr_t)alSourceUnqueueBuffers },
	{ "alSourcef", (uintptr_t)w_alSourcef },
	{ "alSourcefv", (uintptr_t)alSourcefv },
	{ "alSourcei", (uintptr_t)alSourcei },
	{ "alSourceiv", (uintptr_t)alSourceiv },
	{ "alSpeedOfSound", (uintptr_t)w_alSpeedOfSound },
	{ "alcCaptureCloseDevice", (uintptr_t)alcCaptureCloseDevice },
	{ "alcCaptureOpenDevice", (uintptr_t)alcCaptureOpenDevice },
	{ "alcCaptureSamples", (uintptr_t)alcCaptureSamples },
	{ "alcCaptureStart", (uintptr_t)alcCaptureStart },
	{ "alcCaptureStop", (uintptr_t)alcCaptureStop },
	{ "alcCloseDevice", (uintptr_t)alcCloseDevice },
	{ "alcCreateContext", (uintptr_t)alcCreateContextHook },
	{ "alcDestroyContext", (uintptr_t)alcDestroyContext },
	{ "alcGetContextsDevice", (uintptr_t)alcGetContextsDevice },
	{ "alcGetCurrentContext", (uintptr_t)alcGetCurrentContext },
	{ "alcGetEnumValue", (uintptr_t)alcGetEnumValue },
	{ "alcGetError", (uintptr_t)alcGetError },
	{ "alcGetIntegerv", (uintptr_t)alcGetIntegerv },
	{ "alcGetProcAddress", (uintptr_t)alcGetProcAddressHook },
	{ "alcGetString", (uintptr_t)alcGetString },
	{ "alcGetThreadContext", (uintptr_t)alcGetThreadContext },
	{ "alcIsExtensionPresent", (uintptr_t)alcIsExtensionPresent },
	{ "alcMakeContextCurrent", (uintptr_t)alcMakeContextCurrent },
	{ "alcOpenDevice", (uintptr_t)alcOpenDevice },
	{ "alcProcessContext", (uintptr_t)alcProcessContext },
	{ "alcSetThreadContext", (uintptr_t)alcSetThreadContext },
	{ "alcSuspendContext", (uintptr_t)alcSuspendContext },
};

#define NUM_AL (sizeof(al_funcs) / sizeof(*al_funcs))

/* names the game looks up get our entry points (wrappers included) */
static void *alGetProcAddressHook(const ALchar *name)
{
	for (size_t i = 0; i < NUM_AL; i++)
		if (strcmp(al_funcs[i].symbol, name) == 0)
			return (void *)al_funcs[i].func;
	log_printf("alGetProcAddress(%s): not provided\n", name);
	return NULL;
}

static void *alcGetProcAddressHook(ALCdevice *dev, const ALCchar *name)
{
	return alGetProcAddressHook(name);
}

void patch_openal(void)
{
	hook_sym("InitializeCriticalSection", ret0);
	for (size_t i = 0; i < NUM_AL; i++)
		hook_sym(al_funcs[i].symbol, al_funcs[i].func);
}
