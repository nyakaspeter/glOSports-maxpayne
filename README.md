# glOSports-maxpayne — Max Payne Mobile, native, on the GA36-MB

Max Payne through Rockstar's Android build (War Drum Studios, 2012): a small
loader (`loader/`) maps the game's own `libMaxPayne.so` (armeabi-v7a),
resolves its imports to goodluckOS's glibc, ARM's Mali GLES 2.0 driver (from
the folder's `glos/`, see `goodluckOS-mali-driver`), OpenAL Soft and SDL's pad, and
runs it, the way [max_vita](https://github.com/fgsfdsfgs/max_vita) (MIT) does
on the Vita. It is the rehearsal for GTA San Andreas, whose Android build
(`libGTASA.so`) is made the same way. The console stays stock goodluckOS.

Status: runs. Menus, a new game, the opening cutscene and the
first levels render correctly at 640×480 full screen (4:3), ~55 fps (Vsync),
138 MB RAM in use. No import is left unresolved. A stop through
`appctl stop-application` puts the driver and framebuffer back. Sound plays;
it sounded dull on the first run (the game asks OpenAL for 22050 Hz; it now
mixes at 44100 Hz, not yet re-checked by ear). Not yet checked: saving,
quitting from the game's menu, later levels.

### 4:3

The game's driver picks a 16:9 display mode (width = height × 16/9, so
640×360 letterboxed) unless its `Use4x3` flag is set (then height × 4/3,
`P_ES2DriverCaps` constructor). That alone stretched the picture vertically
by 4/3 (measured on the same subway spot: Max 4/3 taller, same width): the
camera's vertical extent is scaled by `AspectRatioYMult` (0.75, for 16:9,
`P_Camera::validate`). The loader sets `Use4x3` and multiplies
`AspectRatioYMult` by (16/9)/(4/3), to 1.0: same horizontal view, more above
and below.

### Tearing

ARM's fbdev EGL returns from `eglSwapBuffers` at once (0.1 ms). Its own
thread waits for the GPU to finish the frame (~8 ms) and then flips with
`FBIOPAN_DISPLAY`, which returns once the flip has happened at a vblank
(the panel runs at ~55 Hz). The framebuffer has two buffers, so while that
flip is pending the next frame's GPU work would go into the buffer still on
screen: tearing, and a back-and-forth between two frames where half of the
shown buffer was already overwritten.

The loader's own `ioctl` (exported, so libMali's calls land in it) counts
the flips, and `NVEventEGLSwapBuffers` waits before each frame's flush until
the previous frame's flip has landed. The game builds the frame on the CPU
meanwhile; Mali-400 writes the window only once the frame is flushed.
Measured in gameplay: 55 fps, 1–5 ms a frame in the wait. Traps on the
way: the driver flips once when the surface is made (counting starts at the
first swap); a flip that lands after its wait gave up must not leave the
flip count ahead of the swaps, or every later frame tears again; and the
driver merges the flips of frames swapped while it was behind (around a
loading screen), so the missing ones never come: waiting for them cost
every later frame the full 100 ms (found in `glOSports-gtavcs` as a steady
10 fps). So a wait that gives up starts counting afresh, a flip is never
counted ahead of the swaps, and a swap is counted before `eglSwapBuffers`
(its flip can land before that returns).
`MP_VSYNC=0` turns the wait and the swap interval off. `MP_PAN_TRACE=n`
(with `MP_PAN_TRACE_DELAY` seconds) logs n swap and flip events with times.
A thread waiting in `FBIO_WAITFORVSYNC` alongside doubles the flip time
(28 fps): don't measure vblanks that way.

## Settings (`start.sh`)

| Variable | Default | |
|---|---|---|
| `MP_STICK_SCALE` | 1.5 | the GA36-MB's sticks reach only ~2/3 of the declared range |
| `MP_STICK_DEADZONE` | 0.12 | radial, after scaling, rescaled to 0..1 |
| `MP_VSYNC` | 1 | EGL swap interval, and the wait for the previous flip (see Tearing) |
| `MP_FPS_COUNTER` | 0 (1 in `start.sh`) | frames per second in the top-right corner, a 5x7 pixel font drawn with scissored clears (no shader, the game's GL state is put back) |
| `MP_FPS_CAP` | 0 | 30 caps the game at 30 fps: each frame waits for its slot on a 1/30 s grid before the swap (a late frame starts the grid again, no catching up); 0 no cap |
| `MP_LOG_FPS` | 0 | frames per second, time in `eglSwapBuffers`, swaps and pans into the log |
| `MP_SKIP_LEGAL` | 1 | no legal text at start: its 5 s timer starts at 0, and the two frames of it `MaxPayne_ModeSwitch::update` still drew are left out: `X_ProgressBar::RenderBG` before it looks at the timer (skipped while the game's legal-text flag is up) and the closing `X_ProgressBar::advance` (run with the progress bar's do-not-render flag set). The first frame shown is the menu (all checked against the original instructions) |
| `MP_4X3` | 1 | full 4:3 screen (see 4:3); 0 gives the game's 16:9 letterbox |
| `MP_AUDIO_RATE` | 44100 | OpenAL mixing rate (the game asks for 22050); 0 keeps the game's |
| `MP_CROUCH_TOGGLE` | 1 | crouch latches, as on the Vita |
| `MP_CHARACTER_SHADOWS`, `MP_DROP_HIGHEST_LOD`, `MP_DECAL_LIMIT`, `MP_DEBRIS_LIMIT` | 1, 1, 0.33, 0.33 | the detail getters, max_vita's values |

The pad is the PS3 pad of the game's `WarGamepad_*` layer: A/B/X/Y are
cross/circle/square/triangle by position, L1/R1 the shoulders, L2/R2 the
triggers (axes 4/5), Select/Start.

## Videos

The startup logos and the intro (`game/movies/credits1.mpg.m4v`, H.264
860x480; `intro.mpg.m4v`, MPEG-4 Part 2 852x480; both from the OBB) play as
they come: 25 fps, no frame dropped. A or Start skips. On Android a Java
MediaPlayer shows them while `ApplicationTick` only polls
`OS_MovieIsPlaying` and the pad; here `OS_MoviePlay`, `OS_MovieIsPlaying` and
`AND_StopMovie` (`OS_MovieStop` is a 4-byte jump to it) go to `loader/movie.c`
(the player of `glOSports-gtactw`), and each poll, on the thread that holds
the screen, shows the newest picture. The logos are a 4:3 picture stored at
860x480 with no aspect in the file, shown as 4:3; the intro's 640x400 picture
sits in a black border baked into its frame, cropped off (`movie_shapes`, in
`loader/hooks_game.c`). The FFmpeg build has the MPEG-4 decoder for the intro.
`GAME_MOVIE_TEST=<file>@<seconds>` starts a video that many seconds in (test
runs).

## Build

```
./build.sh      # loader/ -> build/maxpayne, in the maxpayne-builder Docker image (Debian bookworm armhf)
./package.sh    # -> out/ports/maxpayne/ and out/.local/share/applications/maxpayne.puppy
```

`build.sh` runs `tools/build-ffmpeg.sh` first when `build/ffmpeg/` is
missing: a static FFmpeg 6.1.2 (MP4 demuxer, H.264, MPEG-4 Part 2 and AAC
decoders, NEON), from ffmpeg.org's `ffmpeg-6.1.2.tar.xz` (sha256
`3b624649…`) unpacked in `upstream/ffmpeg-6.1.2`.

Test tools are shared: `../goodluckOS-tools/testing-sync.sh maxpayne` puts them
on the console (`glos/testing/`), `testing-clean.sh maxpayne` takes them off,
`shot.sh maxpayne file.png` takes a screenshot (see `goodluckOS-tools`).

## The loader (`loader/`)

| File | |
|---|---|
| `so_util.c` | maps the two segments into one anonymous mapping, relocates (RELATIVE, GLOB_DAT, JUMP_SLOT), finds symbols through the ELF hash table, patches functions with `LDR PC, [PC]` |
| `imports.c` | the .so's 286 imports, bionic → glibc |
| `hooks_game.c` | War Drum's `OS_*` / `AND_*` / `WarGamepad_*` layer: threads, screen size, private storage, language, pad, the videos (to `movie.c`), no network, no vibration, `ExitAndroidGame` |
| `hooks_gl.c` | `NVEventEGL*`: a GLES 2.0 context on EGL on `/dev/fb0` (ARM's fbdev window); the game's own GLSL ES shaders run as shipped |
| `hooks_al.c` | the OpenAL Soft built into the .so (OpenSL ES backend) is bypassed: its 128 entry points jump to the folder's OpenAL Soft |
| `pad.c` | SDL2 game controller, stick shaping |

### The ABI gap

Android armeabi-v7a passes floats in integer registers (softfp); goodluckOS
is armhf. Every import and hook that takes or returns a float or double goes
through a function declared `__attribute__((pcs("aapcs")))` (`SOFTFP` in
`util.h`); the loader itself stays armhf so it links the system and `glos/`
libraries unchanged. Variadic calls (`printf` family) are base-AAPCS in both
ABIs and need nothing.

- libm: `acos acosf asinf atan2f atanf cos cosf exp floor floorf fmod fmodf
  log log10f pow powf sin sinf tan tanf sqrt sqrtf`; libc: `atof strtod strtof`
- GLES: `glClearColor glClearDepthf glDepthRangef glTexParameterf glUniform1f
  glUniform3f`
- OpenAL: the `…f` / `…3f` setters, `alDopplerFactor`, `alDopplerVelocity`,
  `alSpeedOfSound`, `alGetFloat`, `alGetDouble`; `alGetProcAddress` hands out
  these wrappers
- hooks: `WarGamepad_GetGamepadAxis`, the two float detail getters

### Bionic vs glibc

Same kernel numbers (errno, open flags, syscalls, ioctls) and base calling
convention, so most imports go straight through. What differs:

- pthread mutex, cond and `sem_t` are one word in bionic: the word holds a
  pointer to the glibc object, made on first use (the game also uses bionic's
  static initialisers, 0 / 0x4000 recursive / 0x8000 error-checking).
  `pthread_attr_t` and `pthread_mutexattr_t` are emulated in bionic's layout.
- `struct stat` is filled in bionic's (the kernel's stat64) layout;
  `readdir` is glibc's `readdir64` (same layout as bionic's dirent).
- `__sF` / `stderr`: fake FILEs that the stdio wrappers translate to glibc's.
- `_ctype_` is built at start; `sysconf` maps bionic's names; `mbstate_t`,
  `sigset_t` are smaller in bionic (own state, `sigaction` ignored);
  `setjmp` returns 0 (only the JPEG reader's error path uses it and nothing
  calls `longjmp`).
- The .so carries its own C++ runtime and unwinder; `__gnu_Unwind_Find_exidx`
  returns its `.ARM.exidx`.

### Kept from max_vita, dropped, replaced

- **Kept:** the hook list (`hooks/game.c`), the AL buffer-unqueue fix, the
  device identity that keeps bloom off (`AND_SystemInitialize`), crouch toggle.
- **Replaced:** kubridge by `mmap` / `mprotect` and `__builtin___clear_cache`;
  vitaGL by ARM's EGL; FIOS by plain files.
- **Dropped:** the Vita's Cg shader replacements (`mipmapReplace`,
  `fragColorReplace`, `colorReplace`), the RGBA8 FBO override (`MP_RGBA8`
  sets it if needed), touch.

## Reference loader (`upstream/`, not committed)

The GTA San Andreas, Chinatown Wars and Bully references live in their own ports' `upstream/` (glOSports-gtasa, glOSports-gtactw, glOSports-bully).

| Folder | Source | Commit | Used for |
|---|---|---|---|
| `upstream/max_vita` | [fgsfdsfgs/max_vita](https://github.com/fgsfdsfgs/max_vita) (MIT) | `e92e9ad` | the hook list, the import table, the ELF loader |

## Third-party components

Code carried over from the Vita loaders it names keeps their MIT notices;
those, FFmpeg (LGPL-2.1, linked statically for the videos), OpenAL Soft and
the other bundled libraries, and ARM's Mali driver are listed with their
licences in `licenses/`, which `package.sh` copies into the game folder. The
game itself is not included.
