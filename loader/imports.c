/* imports.c -- the imports of libMaxPayne.so, resolved to goodluckOS's glibc,
 * libm, zlib and ARM's Mali driver.
 *
 * Bionic (Android's libc) and glibc share the kernel's numbers (errno,
 * open flags, syscalls, ioctls) and the base calling convention, so most
 * imports go straight through. What differs:
 *   - floats: the .so is softfp, so every import that takes or returns a
 *     float or double gets a SOFTFP wrapper;
 *   - object sizes: bionic's pthread mutex, cond and sem are one word, so the
 *     word holds a pointer to the glibc object (made on first use, as the
 *     game also uses bionic's static initialisers); pthread_attr_t, stat,
 *     FILE, sigset_t and mbstate_t are emulated or translated;
 *   - data: __sF (stdin/out/err), _ctype_, __stack_chk_guard.
 * After max_vita's imports.c (fgsfds, Andy Nguyen, MIT). */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <fenv.h>
#include <locale.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sched.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include <zlib.h>

#include "so_util.h"
#include "util.h"

/* ---- stdio: bionic's __sF[3] (84-byte FILEs) and stderr ---- */

static uint8_t fake_sF[3][0x54];
static FILE *fake_stderr = (FILE *)fake_sF[2];

static FILE *F(FILE *f)
{
	if ((uint8_t *)f == fake_sF[0]) return stdin;
	if ((uint8_t *)f == fake_sF[1]) return stdout;
	if ((uint8_t *)f == fake_sF[2]) return stderr;
	return f;
}

static int w_fclose(FILE *f) { return fclose(F(f)); }
static size_t w_fread(void *p, size_t s, size_t n, FILE *f) { return fread(p, s, n, F(f)); }
static size_t w_fwrite(const void *p, size_t s, size_t n, FILE *f) { return fwrite(p, s, n, F(f)); }
static int w_fseek(FILE *f, long o, int w) { return fseek(F(f), o, w); }
static long w_ftell(FILE *f) { return ftell(F(f)); }
static int w_feof(FILE *f) { return feof(F(f)); }
static int w_ferror(FILE *f) { return ferror(F(f)); }
static int w_fflush(FILE *f) { return fflush(f ? F(f) : NULL); }
static int w_fgetc(FILE *f) { return fgetc(F(f)); }
static char *w_fgets(char *s, int n, FILE *f) { return fgets(s, n, F(f)); }
static int w_fputc(int c, FILE *f) { return fputc(c, F(f)); }
static int w_fputs(const char *s, FILE *f) { return fputs(s, F(f)); }
static int w_ungetc(int c, FILE *f) { return ungetc(c, F(f)); }
static int w_setvbuf(FILE *f, char *b, int m, size_t s) { return setvbuf(F(f), b, m, s); }
static wint_t w_getwc(FILE *f) { return getwc(F(f)); }
static wint_t w_putwc(wchar_t c, FILE *f) { return putwc(c, F(f)); }
static wint_t w_ungetwc(wint_t c, FILE *f) { return ungetwc(c, F(f)); }

static int w_fprintf(FILE *f, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vfprintf(F(f), fmt, ap);
	va_end(ap);
	return n;
}

static int w_printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vfprintf(stdout, fmt, ap);
	va_end(ap);
	return n;
}

static int w_android_log_print(int prio, const char *tag, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	size_t len = strlen(buf);
	return fprintf(stderr, "%s: %s%s", tag, buf, len && buf[len - 1] == '\n' ? "" : "\n");
}

/* ---- files ---- */

/* bionic's struct stat on ARM (the kernel's stat64) */
struct bionic_stat {
	uint64_t st_dev;
	uint8_t pad0[4];
	uint32_t st_ino32;
	uint32_t st_mode, st_nlink, st_uid, st_gid;
	uint64_t st_rdev;
	uint8_t pad3[4];
	int64_t st_size;
	uint32_t st_blksize;
	uint64_t st_blocks;
	uint32_t st_atime_, st_atime_nsec, st_mtime_, st_mtime_nsec, st_ctime_, st_ctime_nsec;
	uint64_t st_ino;
};
_Static_assert(offsetof(struct bionic_stat, st_mtime_) == 0x50, "bionic stat layout");
_Static_assert(sizeof(struct bionic_stat) == 104, "bionic stat size");

static void to_bionic_stat(const struct stat64 *s, struct bionic_stat *b)
{
	memset(b, 0, sizeof(*b));
	b->st_dev = s->st_dev;
	b->st_ino32 = (uint32_t)s->st_ino;
	b->st_ino = s->st_ino;
	b->st_mode = s->st_mode;
	b->st_nlink = s->st_nlink;
	b->st_uid = s->st_uid;
	b->st_gid = s->st_gid;
	b->st_rdev = s->st_rdev;
	b->st_size = s->st_size;
	b->st_blksize = s->st_blksize;
	b->st_blocks = s->st_blocks;
	b->st_atime_ = s->st_atim.tv_sec;
	b->st_atime_nsec = s->st_atim.tv_nsec;
	b->st_mtime_ = s->st_mtim.tv_sec;
	b->st_mtime_nsec = s->st_mtim.tv_nsec;
	b->st_ctime_ = s->st_ctim.tv_sec;
	b->st_ctime_nsec = s->st_ctim.tv_nsec;
}

static int w_stat(const char *path, struct bionic_stat *b)
{
	struct stat64 s;
	int r = stat64(path, &s);
	if (r == 0)
		to_bionic_stat(&s, b);
	return r;
}

static int w_fstat(int fd, struct bionic_stat *b)
{
	struct stat64 s;
	int r = fstat64(fd, &s);
	if (r == 0)
		to_bionic_stat(&s, b);
	return r;
}

static int w_open(const char *path, int flags, int mode) { return open(path, flags, mode); }
/* glibc's struct dirent64 has bionic's struct dirent layout */
static struct dirent64 *w_readdir(DIR *d) { return readdir64(d); }

/* ---- pthread ---- */

/* bionic's static initialisers */
#define BIONIC_MUTEX_RECURSIVE_INIT 0x4000
#define BIONIC_MUTEX_ERRORCHECK_INIT 0x8000
#define IS_HEAP(v) ((v) > 0x10000)

static pthread_mutex_t *new_mutex(int type)
{
	pthread_mutexattr_t a;
	pthread_mutexattr_init(&a);
	pthread_mutexattr_settype(&a, type);
	pthread_mutex_t *m = malloc(sizeof(*m));
	pthread_mutex_init(m, &a);
	pthread_mutexattr_destroy(&a);
	return m;
}

static pthread_mutex_t *mutex_get(volatile uintptr_t *slot)
{
	uintptr_t v = *slot;
	if (IS_HEAP(v))
		return (pthread_mutex_t *)v;
	int type = v == BIONIC_MUTEX_RECURSIVE_INIT ? PTHREAD_MUTEX_RECURSIVE :
		v == BIONIC_MUTEX_ERRORCHECK_INIT ? PTHREAD_MUTEX_ERRORCHECK : PTHREAD_MUTEX_NORMAL;
	pthread_mutex_t *m = new_mutex(type);
	if (__sync_bool_compare_and_swap(slot, v, (uintptr_t)m))
		return m;
	pthread_mutex_destroy(m);
	free(m);
	return (pthread_mutex_t *)*slot;
}

/* bionic's pthread_mutexattr_t is the type number itself (same numbers) */
static int w_mutexattr_init(int *a) { *a = PTHREAD_MUTEX_NORMAL; return 0; }
static int w_mutexattr_settype(int *a, int type) { *a = type; return 0; }

static int w_mutex_init(uintptr_t *slot, const int *attr)
{
	*slot = (uintptr_t)new_mutex(attr ? *attr : PTHREAD_MUTEX_NORMAL);
	return 0;
}

static int w_mutex_destroy(uintptr_t *slot)
{
	if (IS_HEAP(*slot)) {
		pthread_mutex_destroy((pthread_mutex_t *)*slot);
		free((void *)*slot);
	}
	*slot = 0;
	return 0;
}

static int w_mutex_lock(uintptr_t *slot) { return pthread_mutex_lock(mutex_get(slot)); }
static int w_mutex_unlock(uintptr_t *slot) { return pthread_mutex_unlock(mutex_get(slot)); }

static pthread_cond_t *cond_get(volatile uintptr_t *slot)
{
	uintptr_t v = *slot;
	if (IS_HEAP(v))
		return (pthread_cond_t *)v;
	pthread_cond_t *c = malloc(sizeof(*c));
	pthread_cond_init(c, NULL);
	if (__sync_bool_compare_and_swap(slot, v, (uintptr_t)c))
		return c;
	pthread_cond_destroy(c);
	free(c);
	return (pthread_cond_t *)*slot;
}

static int w_cond_init(uintptr_t *slot, const void *attr)
{
	*slot = 0;
	cond_get(slot);
	return 0;
}

static int w_cond_destroy(uintptr_t *slot)
{
	if (IS_HEAP(*slot)) {
		pthread_cond_destroy((pthread_cond_t *)*slot);
		free((void *)*slot);
	}
	*slot = 0;
	return 0;
}

static int w_cond_broadcast(uintptr_t *c) { return pthread_cond_broadcast(cond_get(c)); }
static int w_cond_signal(uintptr_t *c) { return pthread_cond_signal(cond_get(c)); }
static int w_cond_wait(uintptr_t *c, uintptr_t *m) { return pthread_cond_wait(cond_get(c), mutex_get(m)); }
static int w_cond_timedwait(uintptr_t *c, uintptr_t *m, const struct timespec *t)
{
	return pthread_cond_timedwait(cond_get(c), mutex_get(m), t);
}

/* bionic's pthread_attr_t */
typedef struct {
	uint32_t flags;
	void *stack_base;
	size_t stack_size;
	size_t guard_size;
	int32_t sched_policy;
	int32_t sched_priority;
} bionic_attr;

static int w_attr_init(bionic_attr *a)
{
	memset(a, 0, sizeof(*a));
	a->stack_size = 1024 * 1024;
	return 0;
}
static int w_attr_getstacksize(const bionic_attr *a, size_t *s) { *s = a->stack_size; return 0; }
static int w_attr_setstacksize(bionic_attr *a, size_t s) { a->stack_size = s; return 0; }
static int w_attr_getschedparam(const bionic_attr *a, struct sched_param *p) { p->sched_priority = a->sched_priority; return 0; }
static int w_attr_setschedparam(bionic_attr *a, const struct sched_param *p) { a->sched_priority = p->sched_priority; return 0; }

static int w_pthread_create(pthread_t *t, const bionic_attr *ba, void *(*fn)(void *), void *arg)
{
	pthread_attr_t a;
	pthread_attr_init(&a);
	size_t stack = ba && ba->stack_size ? ba->stack_size : 1024 * 1024;
	if (stack < 256 * 1024)
		stack = 256 * 1024;
	pthread_attr_setstacksize(&a, stack);
	if (ba && (ba->flags & 1)) /* PTHREAD_ATTR_FLAG_DETACHED */
		pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
	int r = pthread_create(t, &a, fn, arg);
	pthread_attr_destroy(&a);
	return r;
}

/* bionic's sem_t is one word too */
static int w_sem_init(sem_t **slot, int pshared, unsigned value)
{
	*slot = malloc(sizeof(sem_t));
	return sem_init(*slot, 0, value);
}
static int w_sem_destroy(sem_t **slot)
{
	int r = sem_destroy(*slot);
	free(*slot);
	*slot = NULL;
	return r;
}
static int w_sem_post(sem_t **s) { return sem_post(*s); }
static int w_sem_wait(sem_t **s) { return sem_wait(*s); }
static int w_sem_trywait(sem_t **s) { return sem_trywait(*s); }
static int w_sem_getvalue(sem_t **s, int *v) { return sem_getvalue(*s, v); }

/* ---- misc libc ---- */

static int stack_chk_guard = 0x42424242;
static void w_stack_chk_fail(void) { fatal_error("stack smashing detected in libMaxPayne.so"); }
static int *w_errno(void) { return __errno_location(); }
static pid_t w_gettid(void) { return syscall(SYS_gettid); }
static long w_syscall(long n, long a, long b, long c, long d, long e, long f) { return syscall(n, a, b, c, d, e, f); }
static int w_ioctl(int fd, unsigned long req, void *arg) { return ioctl(fd, req, arg); }
static void w_exit(int code) { exit_game(code); }
static int w_sigemptyset(uint32_t *set) { *set = 0; return 0; } /* bionic's sigset_t is 32 bits */
static char *w_setlocale(int cat, const char *loc) { static char c[] = "C"; return c; }
static void *w_dlopen(const char *f, int m) { return NULL; }

static long w_sysconf(int name)
{
	switch (name) { /* bionic's numbers */
	case 0x06: return sysconf(_SC_CLK_TCK);
	case 0x27: case 0x28: return sysconf(_SC_PAGESIZE);
	case 0x60: return sysconf(_SC_NPROCESSORS_CONF);
	case 0x61: return sysconf(_SC_NPROCESSORS_ONLN);
	case 0x62: return sysconf(_SC_PHYS_PAGES);
	case 0x63: return sysconf(_SC_AVPHYS_PAGES);
	}
	log_printf("sysconf: unknown bionic name 0x%x\n", name);
	errno = EINVAL;
	return -1;
}

/* bionic's mbstate_t is 4 bytes, glibc's 8: keep our own */
static size_t w_mbrtowc(wchar_t *wc, const char *s, size_t n, void *ps)
{
	static __thread mbstate_t st;
	return mbrtowc(wc, s, n, &st);
}
static size_t w_wcrtomb(char *s, wchar_t wc, void *ps)
{
	static __thread mbstate_t st;
	return wcrtomb(s, wc, &st);
}

static void *w_aeabi_memcpy(void *d, const void *s, size_t n) { return memcpy(d, s, n); }
static void *w_aeabi_memmove(void *d, const void *s, size_t n) { return memmove(d, s, n); }
static void *w_aeabi_memset(void *d, size_t n, int c) { return memset(d, c, n); }
static void *w_aeabi_memclr(void *d, size_t n) { return memset(d, 0, n); }

/* the C++ runtime inside the .so unwinds through its own exception index */
static uintptr_t w_gnu_Unwind_Find_exidx(uintptr_t pc, int *count)
{
	if (pc >= so_base && pc < so_base + so_span) {
		*count = so_exidx_count;
		return so_exidx;
	}
	*count = 0;
	return 0;
}

/* bionic's _ctype_: a pointer to 257 flag bytes, indexed by c + 1 */
static char ctype_table[257];
static const char *ctype_ptr = ctype_table;

static void init_ctype(void)
{
	for (int c = 0; c < 128; c++) {
		char f = 0;
		if (isupper(c)) f |= 0x01;
		if (islower(c)) f |= 0x02;
		if (isdigit(c)) f |= 0x04;
		if (isspace(c)) f |= 0x08;
		if (ispunct(c)) f |= 0x10;
		if (iscntrl(c)) f |= 0x20;
		if (isxdigit(c) && !isdigit(c)) f |= 0x40;
		if (c == ' ') f |= 0x80;
		ctype_table[c + 1] = f;
	}
}

/* ---- softfp maths and number parsing ---- */

#define F1(n) static SOFTFP float w_##n(float x) { return n(x); }
#define D1(n) static SOFTFP double w_##n(double x) { return n(x); }
#define F2(n) static SOFTFP float w_##n(float x, float y) { return n(x, y); }
#define D2(n) static SOFTFP double w_##n(double x, double y) { return n(x, y); }
D1(acos) F1(acosf) F1(asinf) F2(atan2f) F1(atanf) D1(cos) F1(cosf) D1(exp)
D1(floor) F1(floorf) D2(fmod) F2(fmodf) D1(log) F1(log10f) D2(pow) F2(powf)
D1(sin) F1(sinf) D1(sqrt) F1(sqrtf) D1(tan) F1(tanf)
static SOFTFP double w_atof(const char *s) { return atof(s); }
static SOFTFP double w_strtod(const char *s, char **e) { return strtod(s, e); }
static SOFTFP float w_strtof(const char *s, char **e) { return strtof(s, e); }

/* ---- GLES 2.0 and EGL, from ARM's libMali.so (in glos/lib) ---- */

static void *mali;
static void (*p_glClearColor)(float, float, float, float);
static void (*p_glClearDepthf)(float);
static void (*p_glDepthRangef)(float, float);
static void (*p_glTexParameterf)(unsigned, unsigned, float);
static void (*p_glUniform1f)(int, float);
static void (*p_glUniform3f)(int, float, float, float);
static SOFTFP void w_glClearColor(float r, float g, float b, float a) { p_glClearColor(r, g, b, a); }
static SOFTFP void w_glClearDepthf(float d) { p_glClearDepthf(d); }
static SOFTFP void w_glDepthRangef(float n, float f) { p_glDepthRangef(n, f); }
static SOFTFP void w_glTexParameterf(unsigned t, unsigned p, float v) { p_glTexParameterf(t, p, v); }
static SOFTFP void w_glUniform1f(int l, float x) { p_glUniform1f(l, x); }
static SOFTFP void w_glUniform3f(int l, float x, float y, float z) { p_glUniform3f(l, x, y, z); }

static void *w_eglGetProcAddress(const char *name)
{
	uintptr_t a = import_lookup(name);
	if (a)
		return (void *)a;
	/* anything else may take floats in the wrong registers */
	log_printf("eglGetProcAddress(%s): not provided\n", name);
	return NULL;
}

/* ---- the table ---- */

#define FN(name, f) { #name, (uintptr_t)(f) }
#define SAME(name) { #name, (uintptr_t)&name }
#define GL(name) { #name, 0 } /* filled from libMali.so */

static DynLibFunction imports[] = {
	/* Android-only libraries: no assets through JNI, no Immersion vibration */
	FN(AAssetManager_fromJava, ret0), FN(AAssetManager_open, ret0), FN(AAsset_close, ret0),
	FN(AAsset_getLength, ret0), FN(AAsset_getRemainingLength, ret0), FN(AAsset_read, retm1),
	FN(AAsset_seek, retm1),
	FN(ImmVibeCloseDevice, retm1), FN(ImmVibeGetEffectState, retm1),
	FN(ImmVibeGetIVTEffectIndexFromName, retm1), FN(ImmVibeInitialize2, retm1),
	FN(ImmVibeOpenDevice, retm1), FN(ImmVibePlayUHLEffect, retm1),
	FN(ImmVibeStopPlayingEffect, retm1), FN(ImmVibeTerminate, retm1),
	FN(__android_log_print, w_android_log_print),

	FN(__aeabi_memclr, w_aeabi_memclr), FN(__aeabi_memclr8, w_aeabi_memclr),
	FN(__aeabi_memcpy, w_aeabi_memcpy), FN(__aeabi_memcpy4, w_aeabi_memcpy),
	FN(__aeabi_memmove, w_aeabi_memmove), FN(__aeabi_memmove4, w_aeabi_memmove),
	FN(__aeabi_memset, w_aeabi_memset),
	FN(__cxa_atexit, ret0), /* the game leaves through exit_game() */
	FN(__errno, w_errno),
	FN(__gnu_Unwind_Find_exidx, w_gnu_Unwind_Find_exidx),
	FN(__google_potentially_blocking_region_begin, ret0),
	FN(__google_potentially_blocking_region_end, ret0),
	FN(__sF, fake_sF), FN(stderr, &fake_stderr),
	FN(__stack_chk_fail, w_stack_chk_fail), FN(__stack_chk_guard, &stack_chk_guard),
	FN(_ctype_, &ctype_ptr),
	FN(abort, abort), FN(exit, w_exit), FN(raise, raise),
	FN(sigaction, ret0), FN(sigemptyset, w_sigemptyset),
	FN(setjmp, ret0), /* only the JPEG reader's error path, never longjmp'd: bionic's jmp_buf is smaller */
	FN(dlopen, w_dlopen),

	FN(acos, w_acos), FN(acosf, w_acosf), FN(asinf, w_asinf), FN(atan2f, w_atan2f),
	FN(atanf, w_atanf), FN(cos, w_cos), FN(cosf, w_cosf), FN(exp, w_exp),
	FN(floor, w_floor), FN(floorf, w_floorf), FN(fmod, w_fmod), FN(fmodf, w_fmodf),
	FN(log, w_log), FN(log10f, w_log10f), FN(pow, w_pow), FN(powf, w_powf),
	FN(sin, w_sin), FN(sinf, w_sinf), FN(sqrt, w_sqrt), FN(sqrtf, w_sqrtf),
	FN(tan, w_tan), FN(tanf, w_tanf),
	FN(atof, w_atof), FN(strtod, w_strtod), FN(strtof, w_strtof),
	SAME(fegetround), SAME(fesetround),

	SAME(atoi), SAME(strtol), SAME(strtoul), SAME(bsearch), SAME(qsort),
	SAME(rand), SAME(srand),
	SAME(calloc), SAME(free), SAME(malloc), SAME(realloc),
	SAME(isspace), SAME(tolower), SAME(toupper), SAME(towlower), SAME(towupper),
	SAME(btowc), SAME(wctob), SAME(wctype), SAME(iswctype),
	FN(mbrtowc, w_mbrtowc), FN(wcrtomb, w_wcrtomb),
	SAME(wcscoll), SAME(wcsftime), SAME(wcslen), SAME(wcsxfrm),
	SAME(wmemchr), SAME(wmemcmp), SAME(wmemcpy), SAME(wmemmove), SAME(wmemset),
	SAME(memchr), SAME(memcmp), SAME(memcpy), SAME(memmove), SAME(memset),
	SAME(strcasecmp), SAME(strcat), SAME(strchr), SAME(strcmp), SAME(strcoll),
	SAME(strcpy), SAME(stpcpy), SAME(strerror), SAME(strlen), SAME(strncasecmp),
	SAME(strncat), SAME(strncmp), SAME(strncpy), SAME(strpbrk), SAME(strrchr),
	SAME(strstr), SAME(strtok), SAME(strxfrm),
	SAME(snprintf), SAME(sprintf), SAME(vsnprintf), SAME(vsprintf), SAME(sscanf),
	FN(printf, w_printf), FN(fprintf, w_fprintf),
	SAME(getenv), FN(setlocale, w_setlocale), FN(sysconf, w_sysconf),
	FN(syscall, w_syscall), FN(gettid, w_gettid), FN(ioctl, w_ioctl),

	SAME(clock_gettime), SAME(gettimeofday), SAME(time), SAME(asctime),
	SAME(localtime), SAME(localtime_r), SAME(strftime), SAME(nanosleep), SAME(usleep),

	SAME(fopen), SAME(fdopen), FN(fclose, w_fclose), FN(fread, w_fread),
	FN(fwrite, w_fwrite), FN(fseek, w_fseek), FN(ftell, w_ftell), FN(feof, w_feof),
	FN(ferror, w_ferror), FN(fflush, w_fflush), FN(fgetc, w_fgetc), FN(getc, w_fgetc),
	FN(fgets, w_fgets), FN(fputc, w_fputc), FN(putc, w_fputc), FN(fputs, w_fputs),
	FN(ungetc, w_ungetc), FN(setvbuf, w_setvbuf),
	FN(getwc, w_getwc), FN(putwc, w_putwc), FN(ungetwc, w_ungetwc),
	SAME(close), SAME(lseek), SAME(mkdir), FN(open, w_open), SAME(read), SAME(write),
	SAME(writev), SAME(poll), SAME(remove), FN(stat, w_stat), FN(fstat, w_fstat),
	SAME(opendir), SAME(closedir), FN(readdir, w_readdir),
	SAME(gzopen), SAME(gzgets), SAME(gzclose),

	FN(pthread_attr_init, w_attr_init), FN(pthread_attr_destroy, ret0),
	FN(pthread_attr_getstacksize, w_attr_getstacksize),
	FN(pthread_attr_setstacksize, w_attr_setstacksize),
	FN(pthread_attr_getschedparam, w_attr_getschedparam),
	FN(pthread_attr_setschedparam, w_attr_setschedparam),
	FN(pthread_create, w_pthread_create), SAME(pthread_join), SAME(pthread_self),
	SAME(pthread_setname_np), FN(pthread_setschedparam, ret0),
	SAME(pthread_key_create), SAME(pthread_key_delete),
	SAME(pthread_getspecific), SAME(pthread_setspecific), SAME(pthread_once),
	FN(pthread_mutexattr_init, w_mutexattr_init), FN(pthread_mutexattr_destroy, ret0),
	FN(pthread_mutexattr_settype, w_mutexattr_settype),
	FN(pthread_mutex_init, w_mutex_init), FN(pthread_mutex_destroy, w_mutex_destroy),
	FN(pthread_mutex_lock, w_mutex_lock), FN(pthread_mutex_unlock, w_mutex_unlock),
	FN(pthread_cond_init, w_cond_init), FN(pthread_cond_destroy, w_cond_destroy),
	FN(pthread_cond_broadcast, w_cond_broadcast), FN(pthread_cond_signal, w_cond_signal),
	FN(pthread_cond_wait, w_cond_wait), FN(pthread_cond_timedwait, w_cond_timedwait),
	SAME(sched_get_priority_max), SAME(sched_get_priority_min), SAME(sched_yield),
	FN(sem_init, w_sem_init), FN(sem_destroy, w_sem_destroy), FN(sem_post, w_sem_post),
	FN(sem_wait, w_sem_wait), FN(sem_trywait, w_sem_trywait), FN(sem_getvalue, w_sem_getvalue),

	FN(eglGetProcAddress, w_eglGetProcAddress), GL(eglGetDisplay), GL(eglQueryString),
	GL(glActiveTexture), GL(glAttachShader), GL(glBindAttribLocation), GL(glBindBuffer),
	GL(glBindFramebuffer), GL(glBindRenderbuffer), GL(glBindTexture), GL(glBlendFunc),
	GL(glBufferData), GL(glClear), FN(glClearColor, w_glClearColor),
	FN(glClearDepthf, w_glClearDepthf), GL(glCompileShader), GL(glCompressedTexImage2D),
	GL(glCreateProgram), GL(glCreateShader), GL(glCullFace), GL(glDeleteBuffers),
	GL(glDeleteFramebuffers), GL(glDeleteRenderbuffers), GL(glDeleteShader),
	GL(glDeleteTextures), GL(glDepthFunc), GL(glDepthMask), FN(glDepthRangef, w_glDepthRangef),
	GL(glDisable), GL(glDisableVertexAttribArray), GL(glDrawElements), GL(glEnable),
	GL(glEnableVertexAttribArray), GL(glFinish), GL(glFramebufferRenderbuffer),
	GL(glFramebufferTexture2D), GL(glGenBuffers), GL(glGenFramebuffers),
	GL(glGenRenderbuffers), GL(glGenTextures), GL(glGetAttribLocation), GL(glGetBooleanv),
	GL(glGetError), GL(glGetIntegerv), GL(glGetProgramInfoLog), GL(glGetProgramiv),
	GL(glGetShaderInfoLog), GL(glGetShaderiv), GL(glGetString), GL(glGetUniformLocation),
	GL(glLinkProgram), GL(glReadPixels), GL(glRenderbufferStorage), GL(glScissor),
	GL(glShaderSource), GL(glTexImage2D), FN(glTexParameterf, w_glTexParameterf),
	GL(glTexParameteri), FN(glUniform1f, w_glUniform1f), GL(glUniform1i),
	FN(glUniform3f, w_glUniform3f), GL(glUniform3fv), GL(glUniform4fv),
	GL(glUniformMatrix4fv), GL(glUseProgram), GL(glVertexAttribPointer), GL(glViewport),
};

#define NUM_IMPORTS (sizeof(imports) / sizeof(*imports))

uintptr_t import_lookup(const char *name)
{
	for (size_t i = 0; i < NUM_IMPORTS; i++)
		if (strcmp(imports[i].symbol, name) == 0)
			return imports[i].func;
	return 0;
}

void *mali_sym(const char *name)
{
	return mali ? dlsym(mali, name) : NULL;
}

int imports_load_gl(void)
{
	mali = dlopen("libMali.so", RTLD_NOW | RTLD_GLOBAL);
	if (!mali) {
		log_printf("can't load libMali.so: %s (start through ./glos/glos-gpu run mali)\n", dlerror());
		return -1;
	}
	for (size_t i = 0; i < NUM_IMPORTS; i++) {
		if (imports[i].func)
			continue;
		imports[i].func = (uintptr_t)dlsym(mali, imports[i].symbol);
		if (!imports[i].func) {
			log_printf("libMali.so has no %s\n", imports[i].symbol);
			return -1;
		}
	}
#define LOAD(n) if (!(*(void **)&p_##n = dlsym(mali, #n))) { log_printf("libMali.so has no " #n "\n"); return -1; }
	LOAD(glClearColor) LOAD(glClearDepthf) LOAD(glDepthRangef) LOAD(glTexParameterf)
	LOAD(glUniform1f) LOAD(glUniform3f)
#undef LOAD
	return 0;
}

int resolve_imports(void)
{
	init_ctype();
	return so_resolve(imports, NUM_IMPORTS);
}
