#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

/* libMaxPayne.so is armeabi-v7a softfp: floats travel in core registers.
 * Every function of ours that it calls with or for a float or double is
 * declared with this; the loader itself is armhf. */
#define SOFTFP __attribute__((pcs("aapcs")))

/* the game folder's game/ (StorageRootBuffer) */
extern char fs_root[256];

int log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void fatal_error(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
float env_float(const char *name, float def);
int env_int(const char *name, int def);

int ret0(void);
int ret1(void);
int retm1(void);

/* imports.c: the .so's imports (bionic, libm, GLES, EGL, zlib) */
int imports_load_gl(void);
int resolve_imports(void);
uintptr_t import_lookup(const char *name);
void *mali_sym(const char *name);

/* hooks_game.c, hooks_gl.c, hooks_al.c */
void patch_game(void);
void patch_opengl(void);
void patch_openal(void);
void egl_shutdown(void);
int egl_width(void);
int egl_height(void);
int egl_movie_context(int on);
void egl_present(void);

/* movie.c */
int movie_play(const char *name);    /* starts it; 0: no such file */
int movie_play_wait(const char *name); /* as a cutscene: returns when it ends */
int movie_playing(void);
void movie_stop(void);
int movie_frame(void);               /* egl_swap: draw the video's picture */
void movie_frame_done(void);
void movie_present(void);              /* from the thread that holds the screen */
void movie_shapes(const void *table); /* see movie.c: per-file aspect and crop */
const char *movie_test_due(void);   /* GAME_MOVIE_TEST: a video to start now, or NULL */
void exit_game(int code) __attribute__((noreturn));

/* pad.c */
void pad_init(void);
void pad_update(void);
int pad_quit_requested(void);
int pad_buttons(void);
float pad_axis(int axis);

#endif
