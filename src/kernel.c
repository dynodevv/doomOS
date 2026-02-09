/*
 * kernel.c — doomOS Main Entry Point
 *
 * Boots via the Limine protocol, sets up a bump allocator over available RAM,
 * maps the linear framebuffer, and launches DOOM via the doomgeneric interface.
 *
 * The DOOM1.WAD shareware file is embedded in the .wad ELF section at link
 * time, so no filesystem driver is needed — the engine reads directly from RAM.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Limine boot protocol headers */
#include "limine.h"

/* doomgeneric interface */
#include "doomgeneric.h"
#include "doomkeys.h"

/* Local PS/2 keyboard driver */
#include "keyboard.h"

/* ================================================================== */
/*  Limine Request Tags                                               */
/* ================================================================== */

/*
 * Each LIMINE_*_REQUEST macro places a tagged structure in a special
 * linker section.  The bootloader scans the kernel ELF, finds the
 * tags, and fills in the response pointers before handing control to
 * _start.
 */

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request fb_request = {
    .id       = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id       = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id       = LIMINE_HHDM_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

/* ================================================================== */
/*  Embedded WAD — symbols provided by the linker script / objcopy    */
/* ================================================================== */

extern const uint8_t _binary_DOOM1_WAD_start[];
extern const uint8_t _binary_DOOM1_WAD_end[];

/* ================================================================== */
/*  Bump Allocator                                                    */
/* ================================================================== */

/*
 * A trivial bump allocator.  We walk the Limine memory map once at boot
 * and pick the largest USABLE region.  Every malloc() call simply
 * advances the pointer; free() is a no-op.  This is sufficient for
 * Doom's allocation pattern (large up-front allocations, never freed).
 */

static uint8_t *heap_base = NULL;
static uint8_t *heap_ptr  = NULL;
static uint8_t *heap_end  = NULL;

static void heap_init(void)
{
    struct limine_memmap_response *mm = memmap_request.response;
    if (!mm) {
        for (;;) __asm__ volatile ("hlt");
    }

    uint64_t best_size = 0;
    uint64_t best_base = 0;
    uint64_t hhdm_off  = hhdm_request.response->offset;

    for (uint64_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE && e->length > best_size) {
            best_size = e->length;
            best_base = e->base;
        }
    }

    if (best_size == 0) {
        for (;;) __asm__ volatile ("hlt");
    }

    /* Map through the Higher-Half Direct Map so we can access it */
    heap_base = (uint8_t *)(best_base + hhdm_off);
    heap_ptr  = heap_base;
    heap_end  = heap_base + best_size;
}

/* Align to 16 bytes for SSE-friendly allocations */
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

void *malloc(size_t size)
{
    size = ALIGN_UP(size, 16);
    if (heap_ptr + size > heap_end) {
        return NULL;  /* Out of memory */
    }
    void *ptr = heap_ptr;
    heap_ptr += size;
    return ptr;
}

void free(void *ptr)
{
    (void)ptr;  /* Bump allocator — free is a no-op */
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) {
        uint8_t *p = (uint8_t *)ptr;
        for (size_t i = 0; i < total; i++) {
            p[i] = 0;
        }
    }
    return ptr;
}

void *realloc(void *old_ptr, size_t new_size)
{
    /* Simple strategy: always allocate new, copy old data.
     * We don't know the old size, so we copy new_size bytes
     * (safe because the old allocation is still valid memory). */
    void *new_ptr = malloc(new_size);
    if (new_ptr && old_ptr) {
        uint8_t *dst = (uint8_t *)new_ptr;
        uint8_t *src = (uint8_t *)old_ptr;
        for (size_t i = 0; i < new_size; i++) {
            dst[i] = src[i];
        }
    }
    return new_ptr;
}

/* ================================================================== */
/*  String / Memory Utilities (freestanding — no libc)                */
/* ================================================================== */

void *memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *)s;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)c;
    return s;
}

void *memmove(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dest;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return pa[i] - pb[i];
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char *)a - *(unsigned char *)b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

char *strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *d = (char *)malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == '\0') return (char *)s;
    return (char *)last;
}

long strtol(const char *nptr, char **endptr, int base)
{
    long result = 0;
    int sign = 1;
    while (*nptr == ' ' || *nptr == '\t') nptr++;
    if (*nptr == '-') { sign = -1; nptr++; }
    else if (*nptr == '+') { nptr++; }
    if (base == 0) {
        if (*nptr == '0' && (nptr[1] == 'x' || nptr[1] == 'X')) { base = 16; nptr += 2; }
        else if (*nptr == '0') { base = 8; nptr++; }
        else { base = 10; }
    }
    while (*nptr) {
        int digit;
        if (*nptr >= '0' && *nptr <= '9') digit = *nptr - '0';
        else if (*nptr >= 'a' && *nptr <= 'f') digit = *nptr - 'a' + 10;
        else if (*nptr >= 'A' && *nptr <= 'F') digit = *nptr - 'A' + 10;
        else break;
        if (digit >= base) break;
        result = result * base + digit;
        nptr++;
    }
    if (endptr) *endptr = (char *)nptr;
    return result * sign;
}

int atoi(const char *s)
{
    return (int)strtol(s, NULL, 10);
}

int abs(int x) { return x < 0 ? -x : x; }

/* ================================================================== */
/*  Minimal FILE stubs for doomgeneric's WAD reading                  */
/* ================================================================== */

/*
 * doomgeneric's WAD loader calls fopen / fread / fseek / ftell / fclose.
 * We redirect these to operate over the embedded WAD blob in memory.
 */

typedef struct {
    const uint8_t *base;
    size_t         size;
    size_t         pos;
} MEMFILE;

/* We only ever open one file: the WAD */
static MEMFILE wad_file;
static int     wad_file_open = 0;

typedef MEMFILE FILE;

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define EOF (-1)

FILE *fopen(const char *path, const char *mode)
{
    (void)path;
    (void)mode;
    /* Any fopen call in Doom is for the WAD — point to embedded blob */
    wad_file.base = _binary_DOOM1_WAD_start;
    wad_file.size = (size_t)(_binary_DOOM1_WAD_end - _binary_DOOM1_WAD_start);
    wad_file.pos  = 0;
    wad_file_open = 1;
    return &wad_file;
}

size_t fread(void *ptr, size_t elem_size, size_t count, FILE *stream)
{
    size_t total = elem_size * count;
    if (stream->pos + total > stream->size) {
        total = stream->size - stream->pos;
    }
    memcpy(ptr, stream->base + stream->pos, total);
    stream->pos += total;
    return total / elem_size;
}

int fseek(FILE *stream, long offset, int whence)
{
    size_t new_pos;
    switch (whence) {
    case SEEK_SET: new_pos = (size_t)offset; break;
    case SEEK_CUR: new_pos = stream->pos + offset; break;
    case SEEK_END: new_pos = stream->size + offset; break;
    default: return -1;
    }
    if (new_pos > stream->size) return -1;
    stream->pos = new_pos;
    return 0;
}

long ftell(FILE *stream)
{
    return (long)stream->pos;
}

int fclose(FILE *stream)
{
    (void)stream;
    wad_file_open = 0;
    return 0;
}

int feof(FILE *stream)
{
    return stream->pos >= stream->size;
}

/* printf / fprintf stubs — Doom uses these for debug logging only */
int printf(const char *fmt, ...)  { (void)fmt; return 0; }
int fprintf(FILE *f, const char *fmt, ...) { (void)f; (void)fmt; return 0; }
int sprintf(char *buf, const char *fmt, ...)
{
    (void)buf;
    (void)fmt;
    buf[0] = '\0';
    return 0;
}
int snprintf(char *buf, size_t n, const char *fmt, ...)
{
    (void)fmt;
    if (n > 0) buf[0] = '\0';
    return 0;
}
int sscanf(const char *str, const char *fmt, ...)
{
    (void)str;
    (void)fmt;
    return 0;
}
int puts(const char *s) { (void)s; return 0; }
int putchar(int c) { (void)c; return 0; }

/* Doom calls exit() on fatal errors — halt the CPU */
void exit(int status)
{
    (void)status;
    for (;;) __asm__ volatile ("hlt");
}

/* getenv stub — no environment variables */
char *getenv(const char *name)
{
    (void)name;
    return NULL;
}

/* ================================================================== */
/*  Framebuffer State                                                 */
/* ================================================================== */

static uint32_t *framebuffer   = NULL;
static uint32_t  fb_width      = 0;
static uint32_t  fb_height     = 0;
static uint32_t  fb_pitch      = 0;  /* bytes per row */

/* ================================================================== */
/*  Simple Timing (software millisecond counter)                      */
/* ================================================================== */

/*
 * A simple software tick counter incremented by DG_SleepMs.
 * Not accurate wall-clock time, but sufficient for Doom's 35 Hz tic rate.
 */
static volatile uint32_t tick_count = 0;

static uint32_t get_ms(void)
{
    return tick_count;
}

/* ================================================================== */
/*  doomgeneric Callbacks                                             */
/* ================================================================== */

/*
 * DG_Init — called once by doomgeneric before the game loop.
 * We initialise the framebuffer and keyboard here.
 */
void DG_Init(void)
{
    /* Framebuffer already set up in _start; keyboard init */
    keyboard_init();
}

/*
 * DG_DrawFrame — called every frame (35 Hz).
 * doomgeneric renders into DG_ScreenBuffer (DOOM_RESX × DOOM_RESY, ARGB).
 * We blit that buffer onto the Limine linear framebuffer.
 *
 * Limine's default pixel format is 32-bit ARGB (blue in bits 0–7,
 * green 8–15, red 16–23, alpha 24–31) — identical to Doom's internal
 * format, so no colour conversion is needed.  If the framebuffer
 * resolution is larger than Doom's 320×200, we scale up with a simple
 * nearest-neighbour blit.
 */
extern uint32_t *DG_ScreenBuffer;
#define DOOM_RESX 320
#define DOOM_RESY 200

void DG_DrawFrame(void)
{
    if (!framebuffer || !DG_ScreenBuffer) return;

    uint32_t x_scale = fb_width  / DOOM_RESX;
    uint32_t y_scale = fb_height / DOOM_RESY;
    uint32_t scale   = x_scale < y_scale ? x_scale : y_scale;
    if (scale == 0) scale = 1;

    uint32_t off_x = (fb_width  - DOOM_RESX * scale) / 2;
    uint32_t off_y = (fb_height - DOOM_RESY * scale) / 2;

    uint32_t pitch_px = fb_pitch / 4;  /* pixels per row */

    for (uint32_t sy = 0; sy < DOOM_RESY; sy++) {
        for (uint32_t sx = 0; sx < DOOM_RESX; sx++) {
            uint32_t pixel = DG_ScreenBuffer[sy * DOOM_RESX + sx];
            for (uint32_t dy = 0; dy < scale; dy++) {
                for (uint32_t dx = 0; dx < scale; dx++) {
                    uint32_t fx = off_x + sx * scale + dx;
                    uint32_t fy = off_y + sy * scale + dy;
                    framebuffer[fy * pitch_px + fx] = pixel;
                }
            }
        }
    }
}

void DG_SleepMs(uint32_t ms)
{
    /* Busy-wait spin loop — no proper timer IRQ in this minimal kernel */
    for (volatile uint32_t i = 0; i < ms * 10000; i++) {
        __asm__ volatile ("pause");
    }
    tick_count += ms;
}

uint32_t DG_GetTicksMs(void)
{
    return get_ms();
}

void DG_GetKey(int *pressed, unsigned char *key)
{
    int doom_key;
    int is_pressed;
    if (keyboard_poll(&doom_key, &is_pressed)) {
        *pressed = is_pressed;
        *key     = (unsigned char)doom_key;
    } else {
        *pressed = 0;
        *key     = 0;
    }
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;  /* No window manager — ignore */
}

/* ================================================================== */
/*  Kernel Entry Point                                                */
/* ================================================================== */

void _start(void)
{
    /* ---- Framebuffer setup ---- */
    struct limine_framebuffer_response *fb_resp = fb_request.response;
    if (!fb_resp || fb_resp->framebuffer_count < 1) {
        for (;;) __asm__ volatile ("hlt");
    }

    struct limine_framebuffer *fb = fb_resp->framebuffers[0];
    framebuffer = (uint32_t *)fb->address;
    fb_width    = (uint32_t)fb->width;
    fb_height   = (uint32_t)fb->height;
    fb_pitch    = (uint32_t)fb->pitch;

    /* ---- Heap setup ---- */
    heap_init();

    /* ---- Launch DOOM ---- */
    /* doomgeneric_Create expects argc/argv; pass minimal values */
    char *argv[] = { "doom", "-iwad", "DOOM1.WAD", NULL };
    doomgeneric_Create(3, argv);

    /* Main game loop */
    for (;;) {
        doomgeneric_Tick();
    }
}
