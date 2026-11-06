/* so_util.h -- map an Android .so into this process, relocate and hook it.
 * After max_vita's so_util (Andy Nguyen, MIT), with mmap/mprotect instead of
 * kubridge. */
#ifndef SO_UTIL_H
#define SO_UTIL_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	const char *symbol;
	uintptr_t func;
} DynLibFunction;

extern uintptr_t so_base;
extern size_t so_span;
extern uintptr_t so_exidx;
extern size_t so_exidx_count;

int so_load(const char *filename);
/* Fills the .so's GOT from the import table; names not in it are listed on
 * stderr and point to an address that faults with the GOT slot as PC. */
int so_resolve(const DynLibFunction *funcs, size_t num_funcs);
void so_make_text_writable(int writable);
void so_flush_caches(void);
void so_execute_init_array(void);
/* Address of a symbol the .so defines, 0 if there is none. */
uintptr_t so_symbol(const char *name);
/* Same, but a missing symbol is fatal (wrong libMaxPayne.so version). */
uintptr_t so_find_addr(const char *name);

/* Overwrite the start of a Thumb (addr odd or not) / ARM function with a
 * jump to dst. */
void hook_thumb(uintptr_t addr, uintptr_t dst);
void hook_arm(uintptr_t addr, uintptr_t dst);
#define hook_sym(name, dst) hook_thumb(so_find_addr(name), (uintptr_t)(dst))

#endif
