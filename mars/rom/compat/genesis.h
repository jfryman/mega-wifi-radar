/* The slice of SGDK's genesis.h that the shared src/ files use, for the 32X ROM's 68000 and SH-2 builds
 * (lode/mars/summary.md). Types match SGDK's exactly (bool is a byte), so shared structs keep their layout.
 * CPU-specific parts (vtimer, pads, the lock behind SYS_disableInts) come from mars_cpu.h in md/ or sh/. */
#ifndef MARS_GENESIS_H
#define MARS_GENESIS_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef u8 bool;
typedef vu8 vbool;

#define TRUE    1
#define FALSE   0
#define true    TRUE
#define false   FALSE
#ifndef NULL
#define NULL    0
#endif
#define PACKED __attribute__((__packed__))
#define FASTCALL
#define FORCE_INLINE inline __attribute__((always_inline))
#define NO_INLINE __attribute__((noinline))

#ifndef abs
#define abs(x)  ((x) < 0 ? -(x) : (x))
#endif
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

/* text palettes */
#define PAL0    0
#define PAL1    1
#define PAL2    2
#define PAL3    3

/* pad bits, as SGDK's joy.h */
#define BUTTON_UP       0x0001
#define BUTTON_DOWN     0x0002
#define BUTTON_LEFT     0x0004
#define BUTTON_RIGHT    0x0008
#define BUTTON_A        0x0040
#define BUTTON_B        0x0010
#define BUTTON_C        0x0020
#define BUTTON_START    0x0080
#define BUTTON_X        0x0400
#define BUTTON_Y        0x0200
#define BUTTON_Z        0x0100
#define BUTTON_MODE     0x0800
#define BUTTON_DIR      0x000F
#define BUTTON_BTN      0x0FF0
#define BUTTON_ALL      0x0FFF
#define JOY_1           0

/* compat/libc.c */
void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t max);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
int strcmp(const char *a, const char *b);
int vsprintf(char *buf, const char *fmt, va_list ap);
int sprintf(char *buf, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#define KLog(s)         do { } while (0)
#define kprintf(...)    do { } while (0)

#include "mars_cpu.h"   /* vtimer, JOY_readJoypad, SYS_*: md/compat or sh/compat */

#endif
