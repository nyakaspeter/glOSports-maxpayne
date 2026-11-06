/* hooks_game.c -- War Drum's Android layer (OS_*, AND_*, WarGamepad_*) and
 * a few game functions, redirected to goodluckOS.
 * After max_vita's hooks/game.c (fgsfds, Andy Nguyen, MIT). */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "so_util.h"
#include "util.h"

static int *deviceChip, *deviceForm, *definedDevice;
static int language;

static void *jni_env_trap(void)
{
	fatal_error("the game asked for JNI (NVThreadGetCurrentJNIEnv, called from %p)",
		__builtin_return_address(0));
}

static int AND_DeviceType(void)
{
	/* bits 6+: memory in MB, 0x1: phone; not a Tegra */
	return (256 << 6) | (3 << 2) | 0x1;
}

static int AND_SystemInitialize(void)
{
	/* a phone without a Tegra: no bloom, as on the Vita */
	*deviceForm = 1;
	*deviceChip = 19;
	*definedDevice = 27;
	return 0;
}

static int OS_ScreenGetWidth(void) { return env_int("MP_WIDTH", 640); }
static int OS_ScreenGetHeight(void) { return env_int("MP_HEIGHT", 480); }

static char *OS_FileGetArchiveName(int mode)
{
	/* main (1) is the OBB, which is unpacked into game/ */
	char *out = malloc(16);
	strcpy(out, mode == 1 ? "main.obb" : "");
	return out;
}

static SOFTFP int OS_MoviePlay(const char *file, int a2, int a3, float a4);

static int ProcessEvents(void)
{
	pad_update();
	const char *test = movie_test_due();
	if (test)
		OS_MoviePlay(test, 0, 0, 1.0f);
	return pad_quit_requested(); /* 1 leaves the game */
}

static void ExitAndroidGame(int code)
{
	exit_game(0);
}

typedef struct {
	int (*func)(void *);
	void *arg;
	char *handle;
} thread_start;

static void *thread_stub(void *p)
{
	thread_start s = *(thread_start *)p;
	free(p);
	s.handle[0x41] = 1; /* running */
	s.func(s.arg);
	return NULL;
}

/* The game never reads or frees the handle beyond the running flag; every
 * thread it starts this way is one-shot. */
static void *OS_ThreadLaunch(int (*func)(void *), void *arg, int r2, const char *name, int r4, int priority)
{
	char *handle = calloc(1, 0x48);
	thread_start *s = malloc(sizeof(*s));
	s->func = func;
	s->arg = arg;
	s->handle = handle;
	pthread_attr_t a;
	pthread_attr_init(&a);
	pthread_attr_setstacksize(&a, 512 * 1024);
	pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
	pthread_t t;
	int r = pthread_create(&t, &a, thread_stub, s);
	pthread_attr_destroy(&a);
	if (r != 0) {
		free(s);
		free(handle);
		return NULL;
	}
	if (name)
		pthread_setname_np(t, name);
	*(int *)(handle + 0x24) = (int)t;
	return handle;
}

static int ReadDataFromPrivateStorage(const char *file, void **data, int *size)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", fs_root, file);
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	int ret = 0;
	if (sz > 0) {
		void *buf = malloc(sz);
		if (buf && fread(buf, sz, 1, f) == 1) {
			*data = buf;
			*size = sz;
			ret = 1;
		} else {
			free(buf);
		}
	}
	fclose(f);
	return ret;
}

static int WriteDataToPrivateStorage(const char *file, const void *data, int size)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", fs_root, file);
	FILE *f = fopen(path, "wb");
	if (!f)
		return 0;
	int ret = fwrite(data, size, 1, f);
	fclose(f);
	return ret;
}

/* 8 is a PS3 pad (0, 5, 6: Xbox 360; 4, 7: Moga) */
static int WarGamepad_GetGamepadType(int port)
{
	return port == 0 ? 8 : -1;
}

static int WarGamepad_GetGamepadButtons(int port)
{
	return port == 0 ? pad_buttons() : 0;
}

static SOFTFP float WarGamepad_GetGamepadAxis(int port, int axis)
{
	return port == 0 ? pad_axis(axis) : 0.0f;
}

/* crouch as a toggle, as on the Vita: the pad's crouch button is control #5 */
typedef struct { int unk[14]; } InputControl;
static InputControl *sm_control;
static int (*InputControl_getButton)(InputControl *, int);

static int ConfiguredInput_readCrouch(void *this)
{
	static int prev, latch;
	int now = InputControl_getButton(&sm_control[5], 0);
	if (now != prev) {
		prev = now;
		if (now)
			latch = !latch;
	}
	return latch;
}

/* the language the menu sets, kept in game/language.txt */
static void language_path(char *buf, size_t n)
{
	snprintf(buf, n, "%s/language.txt", fs_root);
}

static int GetAndroidCurrentLanguage(void)
{
	return language;
}

static void SetAndroidCurrentLanguage(int lang)
{
	if (lang == language)
		return;
	language = lang;
	char path[512];
	language_path(path, sizeof(path));
	FILE *f = fopen(path, "w");
	if (f) {
		fprintf(f, "%d\n", lang);
		fclose(f);
	}
}

static int detail_shadows, detail_drop_lod;
static float detail_decals, detail_debris;
static int X_DetailLevel_getCharacterShadows(void) { return detail_shadows; }
static int X_DetailLevel_getDropHighestLOD(void) { return detail_drop_lod; }
static SOFTFP float X_DetailLevel_getDecalLimitMultiplier(void) { return detail_decals; }
static SOFTFP float X_DetailLevel_getDebrisProjectileLimitMultiplier(void) { return detail_debris; }

/* The legal text stays up for a 5 s timer that MaxPayne_ModeSwitch's
 * constructionCode starts (movs r3, #0; movt r3, #0x40a0: 5.0f, into
 * [this + 104]) and update() counts down: start it at 0.0f, so the first
 * update goes on to the menu. Checked against the original bytes. */
/* a bl in the game's code reaches +-16 MB: a jump to the loader's function
 * from a page right after the library */
static uint8_t *veneers;
static size_t veneers_used;

static uintptr_t veneer(uintptr_t target)
{
	if (!veneers) {
		uintptr_t want = (so_base + so_span + 0xfffff) & ~0xfffffu;
		veneers = mmap((void *)want, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (veneers == MAP_FAILED)
			fatal_error("veneer page: no memory");
	}
	uint8_t *v = veneers + veneers_used;
	static const uint8_t ldr_pc[4] = { 0xdf, 0xf8, 0x00, 0xf0 }; /* ldr.w pc, [pc] */
	memcpy(v, ldr_pc, 4);
	*(uint32_t *)(v + 4) = target; /* the loader may be ARM code: its own bit 0 picks the mode */
	veneers_used += 8;
	__builtin___clear_cache((char *)v, (char *)v + 8);
	return (uintptr_t)v;
}

/* the bl at off (checked against its original bytes) to target */
static int patch_bl(uintptr_t off, const uint8_t orig[4], uintptr_t target)
{
	if (memcmp((void *)(so_base + off), orig, 4) != 0)
		return 0;
	uintptr_t v = veneer(target);
	int32_t d = (int32_t)(v - (so_base + off + 4));
	if (d < -(1 << 24) || d >= (1 << 24))
		return 0;
	uint32_t imm = ((uint32_t)d >> 1) & 0xffffff;
	uint32_t S = (imm >> 23) & 1, I1 = (imm >> 22) & 1, I2 = (imm >> 21) & 1;
	uint32_t J1 = (!I1) ^ S, J2 = (!I2) ^ S;
	uint16_t hw[2] = {
		(uint16_t)(0xf000 | (S << 10) | ((imm >> 11) & 0x3ff)),
		(uint16_t)(0xd000 | (J1 << 13) | (J2 << 11) | (imm & 0x7ff)),
	};
	memcpy((void *)(so_base + off), hw, 4);
	return 1;
}

/* MaxPayne_ModeSwitch::update draws the legal text (X_ProgressBar::RenderBG,
 * a whole frame of its own with the swap) before it looks at the timer, so
 * even with the timer at 0 one frame of it showed before the menu. Its
 * RenderBG call comes here and is left out while the legal text is up
 * (X_ModeSwitch::setIsLegalText mirrors the flag into HackForSkipLegalText;
 * RenderBG draws nothing after that anyway, the progress bar is gone). */
static void (*X_ProgressBar__RenderBG)(void);
static uint8_t *legal_text_up;

static void render_bg_after_legal(void)
{
	if (!*legal_text_up)
		X_ProgressBar__RenderBG();
}

/* When the legal text is over, update() ends the progress bar with
 * X_ProgressBar::advance(name, 1.0f), which draws the legal picture once
 * more, full bar, with its swap: the one frame that still showed. It runs
 * with the progress bar's "do not render" flag set, then the flag is put
 * back. */
static SOFTFP void (*X_ProgressBar__advance)(const char *, float);
static uint8_t *progress_bar_do_not_render;

static SOFTFP void advance_without_frame(const char *name, float progress)
{
	uint8_t was = *progress_bar_do_not_render;
	*progress_bar_do_not_render = 1;
	X_ProgressBar__advance(name, progress);
	*progress_bar_do_not_render = was;
}

static void skip_legal_text(void)
{
	static const uint8_t movt_5[4] = { 0xc4, 0xf2, 0xa0, 0x03 }; /* movt r3, #0x40a0 */
	static const uint8_t movt_0[4] = { 0xc0, 0xf2, 0x00, 0x03 }; /* movt r3, #0 */
	uint8_t *p = (uint8_t *)(so_base + 0x43b50c);
	if (memcmp(p, movt_5, 4) != 0) {
		log_printf("game: legal text timer not where expected, left alone\n");
		return;
	}
	memcpy(p, movt_0, 4);

	X_ProgressBar__RenderBG = (void *)so_find_addr("_ZN13X_ProgressBar8RenderBGEv");
	legal_text_up = (uint8_t *)so_find_addr("HackForSkipLegalText");
	static const uint8_t bl_render_bg[4] = { 0xd0, 0xf2, 0x11, 0xf9 };
	if (!patch_bl(0x43c422, bl_render_bg, (uintptr_t)render_bg_after_legal))
		log_printf("game: legal text frame not left out\n");

	X_ProgressBar__advance = (void *)so_find_addr("_ZN13X_ProgressBar7advanceEPKcf");
	/* X_ProgressBar::setDoNotRender stores into the byte its GOT slot points at */
	progress_bar_do_not_render = *(uint8_t **)(so_base + 0x912880);
	static const uint8_t bl_advance[4] = { 0xcf, 0xf2, 0xdd, 0xff };
	if (!patch_bl(0x43c4e6, bl_advance, (uintptr_t)advance_without_frame))
		log_printf("game: last legal text frame not left out\n");
}

/* videos: on Android a Java MediaPlayer shows them over the game, whose
 * ApplicationTick meanwhile only polls OS_MovieIsPlaying and the pad. movie.c
 * plays game/movies/ and each poll, on the thread that holds the screen,
 * shows the newest picture (the player's own GL context). A or Start skips
 * in the player; the game's own skip calls OS_MovieStop -> AND_StopMovie. */
static SOFTFP int OS_MoviePlay(const char *file, int a2, int a3, float a4)
{
	movie_play(file);
	return 0;
}

static int OS_MovieIsPlaying(int *a)
{
	movie_present();
	return movie_playing();
}

static void AND_StopMovie(void) { movie_stop(); }

/* the logos (credits1) are a 4:3 picture stored at 860x480 with no aspect in
 * the file; the intro has its 640x400 picture inside a black border baked
 * into its 852x480 */
static const struct { const char *name; int dar_w, dar_h, cx, cy, cw, ch; } movie_shape_table[] = {
	{ "credits1.mpg.m4v", 4, 3, 0, 0, 0, 0 },
	{ "intro.mpg.m4v", 0, 0, 106, 40, 640, 400 },
	{ NULL },
};

void patch_game(void)
{
	if (env_int("MP_SKIP_LEGAL", 1))
		skip_legal_text();

	char path[512];
	language_path(path, sizeof(path));
	FILE *f = fopen(path, "r");
	if (f) {
		if (fscanf(f, "%d", &language) != 1 || language < 0 || language > 6)
			language = 0;
		fclose(f);
	}
	detail_shadows = env_int("MP_CHARACTER_SHADOWS", 1); /* 1 blob, 2 foot shadows */
	detail_drop_lod = env_int("MP_DROP_HIGHEST_LOD", 1);
	detail_decals = env_float("MP_DECAL_LIMIT", 0.33f);
	detail_debris = env_float("MP_DEBRIS_LIMIT", 0.33f);

	hook_sym("_Z24NVThreadGetCurrentJNIEnvv", jni_env_trap);
	hook_sym("_Z15OS_ThreadLaunchPFjPvES_jPKci16OSThreadPriority", OS_ThreadLaunch);

	/* store flags, network, links */
	hook_sym("_Z20OS_ServiceAppCommandPKcS0_", ret0);
	hook_sym("_Z23OS_ServiceAppCommandIntPKci", ret0);
	hook_sym("_Z25OS_ServiceIsWifiAvailablev", ret0);
	hook_sym("_Z28OS_ServiceIsNetworkAvailablev", ret0);
	hook_sym("_Z12AND_OpenLinkPKc", ret0);

	/* videos (movies/intro.mpg.m4v, credits1.mpg.m4v): movie.c; the rest of
	 * the Java player's calls do nothing. The OS_ versions of StopMovie and
	 * MovieIsSkippable are 4-byte jumps to the AND_ ones: too short for a
	 * hook, which would overwrite the next function's start */
	movie_shapes(movie_shape_table);
	hook_sym("_Z12OS_MoviePlayPKcbbf", OS_MoviePlay);
	hook_sym("_Z17OS_MovieIsPlayingPi", OS_MovieIsPlaying);
	hook_sym("_Z13AND_StopMoviev", AND_StopMovie);
	hook_sym("_Z20AND_MovieIsSkippableb", ret0);
	hook_sym("_Z18AND_MovieTextScalei", ret0);
	hook_sym("_Z20OS_MoviePlayinWindowPKciiiibbf", ret0);

	hook_sym("_Z17OS_ScreenGetWidthv", OS_ScreenGetWidth);
	hook_sym("_Z18OS_ScreenGetHeightv", OS_ScreenGetHeight);
	hook_sym("_Z9NvAPKOpenPKc", ret0);
	hook_sym("_Z13ProcessEventsb", ProcessEvents);

	hook_sym("_Z25GetAndroidCurrentLanguagev", GetAndroidCurrentLanguage);
	hook_sym("_Z25SetAndroidCurrentLanguagei", SetAndroidCurrentLanguage);

	hook_sym("_Z14AND_DeviceTypev", AND_DeviceType);
	hook_sym("_Z16AND_DeviceLocalev", ret0); /* English */
	hook_sym("_Z20AND_SystemInitializev", AND_SystemInitialize);
	hook_sym("_Z21AND_ScreenSetWakeLockb", ret0);
	hook_sym("_Z22AND_FileGetArchiveName13OSFileArchive", OS_FileGetArchiveName);
	hook_sym("_Z26ReadDataFromPrivateStoragePKcRPcRi", ReadDataFromPrivateStorage);
	hook_sym("_Z25WriteDataToPrivateStoragePKcS0_i", WriteDataToPrivateStorage);

	hook_sym("_Z25WarGamepad_GetGamepadTypei", WarGamepad_GetGamepadType);
	hook_sym("_Z28WarGamepad_GetGamepadButtonsi", WarGamepad_GetGamepadButtons);
	hook_sym("_Z25WarGamepad_GetGamepadAxisii", WarGamepad_GetGamepadAxis);

	/* no vibration, no TouchSense */
	hook_sym("_Z12VibratePhonei", ret0);
	hook_sym("_Z14Mobile_Vibratei", ret0);
	hook_sym("_ZN10TouchSenseC2Ev", ret0);

	hook_sym("_Z15ExitAndroidGamev", ExitAndroidGame);

	hook_sym("_ZN13X_DetailLevel19getCharacterShadowsEv", X_DetailLevel_getCharacterShadows);
	hook_sym("_ZN13X_DetailLevel34getDebrisProjectileLimitMultiplierEv", X_DetailLevel_getDebrisProjectileLimitMultiplier);
	hook_sym("_ZN13X_DetailLevel23getDecalLimitMultiplierEv", X_DetailLevel_getDecalLimitMultiplier);
	hook_sym("_ZN13X_DetailLevel13dropHighesLODEv", X_DetailLevel_getDropHighestLOD);

	if (env_int("MP_CROUCH_TOGGLE", 1)) {
		sm_control = (void *)so_find_addr("_ZN24MaxPayne_ConfiguredInput10sm_controlE");
		InputControl_getButton = (void *)so_find_addr("_ZNK21MaxPayne_InputControl9getButtonEi");
		hook_sym("_ZNK24MaxPayne_ConfiguredInput10readCrouchEv", ConfiguredInput_readCrouch);
	}

	deviceChip = (int *)so_find_addr("deviceChip");
	deviceForm = (int *)so_find_addr("deviceForm");
	definedDevice = (int *)so_find_addr("definedDevice");
}
