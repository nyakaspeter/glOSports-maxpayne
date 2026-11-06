/* main.c -- load Max Payne Mobile's libMaxPayne.so (Android, armeabi-v7a,
 * version 1.7) into this process, point its imports at goodluckOS, patch its
 * Android layer and run it. After max_vita's main.c (fgsfds, Andy Nguyen,
 * MIT). */
#define _GNU_SOURCE
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ucontext.h>
#include <unistd.h>
#include <SDL.h>

#include "so_util.h"
#include "util.h"

void exit_game(int code)
{
	egl_shutdown();
	SDL_Quit();
	fflush(stdout);
	fflush(stderr);
	_exit(code);
}

static void crash_handler(int sig, siginfo_t *si, void *uc_)
{
	ucontext_t *uc = uc_;
	uintptr_t pc = uc->uc_mcontext.arm_pc, lr = uc->uc_mcontext.arm_lr;
#define REL(a) ((a) >= so_base && (a) < so_base + so_span ? (a) - so_base : 0)
	fprintf(stderr, "maxpayne: signal %d at %p (fault address %p)\n"
		"  pc 0x%08x (libMaxPayne.so+0x%x)  lr 0x%08x (libMaxPayne.so+0x%x)  sp 0x%08lx\n",
		sig, (void *)pc, si->si_addr, pc, REL(pc), lr, REL(lr), uc->uc_mcontext.arm_sp);
	fflush(stderr);
	_exit(128 + sig);
}

static void check_data(void)
{
	static const char *files[] = {
		"libMaxPayne.so", "MaxPayneSoundsv2.msf", "x_data.ras", "x_english.ras",
		"x_level1.ras", "x_level2.ras", "x_level3.ras", "data", "es2/DefaultPixel.txt",
	};
	char path[512];
	struct stat st;
	for (size_t i = 0; i < sizeof(files) / sizeof(*files); i++) {
		snprintf(path, sizeof(path), "%s/%s", fs_root, files[i]);
		if (stat(path, &st) < 0)
			fatal_error("%s is missing: see README.md for the game files", path);
	}
}

int main(int argc, char **argv)
{
	char exe[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n <= 0)
		fatal_error("can't find the game folder");
	exe[n] = '\0';
	snprintf(fs_root, sizeof(fs_root), "%s/game", dirname(exe));
	if (chdir(fs_root) < 0)
		fatal_error("%s is missing: see README.md for the game files", fs_root);
	check_data();

	struct sigaction sa = { 0 };
	sa.sa_sigaction = crash_handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
	sigaction(SIGILL, &sa, NULL);
	sigaction(SIGFPE, &sa, NULL);
	sigaction(SIGABRT, &sa, NULL);

	char path[512];
	snprintf(path, sizeof(path), "%s/libMaxPayne.so", fs_root);
	int r = so_load(path);
	if (r < 0)
		fatal_error("can't load %s (%d)", path, r);
	log_printf("libMaxPayne.so at %p\n", (void *)so_base);

	if (imports_load_gl() < 0)
		fatal_error("can't load ARM's GLES driver");
	int missing = resolve_imports();
	if (missing)
		log_printf("%d imports unresolved\n", missing);

	so_make_text_writable(1);
	patch_openal();
	patch_opengl();
	patch_game();
	so_make_text_writable(0);
	so_flush_caches();

	pad_init();
	so_execute_init_array();

	snprintf(path, sizeof(path), "%s/savegames", fs_root);
	mkdir(path, 0755);

	strcpy((char *)so_find_addr("StorageRootBuffer"), fs_root);
	*(uint8_t *)so_find_addr("IsAndroidPaused") = 0;
	/* The driver sizes its display mode to height * 4/3 instead of * 16/9,
	 * and the camera's vertical extent (AspectRatioYMult, 0.75 for 16:9)
	 * grows by the same 4/3: the horizontal view stays, more shows above
	 * and below. */
	if (env_int("MP_4X3", 1)) {
		*(uint8_t *)so_find_addr("Use4x3") = 1;
		*(float *)so_find_addr("AspectRatioYMult") *= (16.0f / 9.0f) / (4.0f / 3.0f);
	}
	if (getenv("MP_RGBA8"))
		*(uint8_t *)so_find_addr("UseRGBA8") = env_int("MP_RGBA8", 0);

	void (*initGraphics)(void) = (void *)so_find_addr("_Z12initGraphicsv");
	void (*ShowJoystick)(int) = (void *)so_find_addr("_Z12ShowJoystickb");
	int (*NVEventAppMain)(int, char **) = (void *)so_find_addr("_Z14NVEventAppMainiPPc");

	initGraphics();
	ShowJoystick(0);
	NVEventAppMain(0, NULL);

	exit_game(0);
}
