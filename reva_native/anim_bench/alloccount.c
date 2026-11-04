/* alloccount.c -- how often the engine asks for memory. A frame path should never need to: arenas and fixed pools hold the frame (engine plan, Phase 2).
 * Build the no-display engine with it: add -DREVA_ALLOCCOUNT anim_bench/alloccount.c -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free (Linux). Every [simhash] line is then
 * followed by [alloc] tick T calls N bytes B: the running totals of calls and of bytes asked for. After the boot, N should stop moving. REVA_ALLOCWHO=<tick> prints a backtrace of
 * each call from that tick on, for addr2line. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>
void *__real_malloc(size_t n);
void *__real_calloc(size_t a, size_t b);
void *__real_realloc(void *p, size_t n);
void __real_free(void *p);
long g_allocN = 0, g_allocBytes = 0, g_allocFrom = -1, g_allocTick = 0;
static void who(void) { static int inside; if (g_allocFrom < 0 || g_allocTick < g_allocFrom || inside) return; inside = 1; void *b[12]; int n = backtrace(b, 12); fputs("[alloc] from", stderr); for (int i = 2; i < n; i++) fprintf(stderr, " %p", b[i]); fputc('\n', stderr); inside = 0; }
void *__wrap_malloc(size_t n) { g_allocN++; g_allocBytes += (long)n; who(); return __real_malloc(n); }
void *__wrap_calloc(size_t a, size_t b) { g_allocN++; g_allocBytes += (long)(a * b); who(); return __real_calloc(a, b); }
void *__wrap_realloc(void *p, size_t n) { g_allocN++; g_allocBytes += (long)n; who(); return __real_realloc(p, n); }
void __wrap_free(void *p) { __real_free(p); }
