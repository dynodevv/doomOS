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
#include <stdarg.h>

/* Our freestanding stubs */
#include <stdio.h>
#include <sys/stat.h>

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

/* Forward declaration — defined near _start */
static void halt(void) __attribute__((noreturn));

static uint8_t *heap_base = NULL;
static uint8_t *heap_ptr  = NULL;
static uint8_t *heap_end  = NULL;

static void heap_init(void)
{
    struct limine_memmap_response *mm = memmap_request.response;
    if (!mm) {
        halt();
    }

    if (!hhdm_request.response) {
        halt();
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
        halt();
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
    /* Overflow check */
    if (nmemb && size > (size_t)-1 / nmemb) return NULL;
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
    /*
     * Bump allocator limitation: we don't track allocation sizes.
     * We always allocate new memory and copy new_size bytes.
     * This is safe in our unikernel because old memory remains
     * mapped and accessible within the heap region.
     */
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

double atof(const char *s)
{
    double result = 0.0;
    double fraction = 0.0;
    int sign = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') { result = result * 10.0 + (*s - '0'); s++; }
    if (*s == '.') {
        s++;
        double place = 0.1;
        while (*s >= '0' && *s <= '9') { fraction += (*s - '0') * place; place *= 0.1; s++; }
    }
    return sign * (result + fraction);
}

int abs(int x) { return x < 0 ? -x : x; }

/* ================================================================== */
/*  Minimal FILE stubs for doomgeneric's WAD reading                  */
/* ================================================================== */

/*
 * doomgeneric's WAD loader calls fopen / fread / fseek / ftell / fclose.
 * We redirect these to operate over the embedded WAD blob in memory.
 * FILE is typedef'd to struct _MEMFILE in our stdio.h stub.
 */

/* We support a small number of open "files" */
#define MAX_OPEN_FILES 8

struct _MEMFILE {
    const uint8_t *base;
    size_t         size;
    size_t         pos;
    int            in_use;
};

static struct _MEMFILE open_files[MAX_OPEN_FILES];

/* stdin/stdout/stderr stubs (never used meaningfully) */
FILE *stdin  = NULL;
FILE *stdout = NULL;
FILE *stderr = NULL;

/* errno stub */
int errno = 0;

FILE *fopen(const char *path, const char *mode)
{
    (void)path;
    (void)mode;
    /* Find a free file slot and point it to the embedded WAD blob */
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i].in_use) {
            open_files[i].base   = _binary_DOOM1_WAD_start;
            open_files[i].size   = (size_t)(_binary_DOOM1_WAD_end - _binary_DOOM1_WAD_start);
            open_files[i].pos    = 0;
            open_files[i].in_use = 1;
            return &open_files[i];
        }
    }
    return NULL;
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
    if (stream) stream->in_use = 0;
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
int vprintf(const char *fmt, __builtin_va_list ap)  { (void)fmt; (void)ap; return 0; }
int vfprintf(FILE *f, const char *fmt, __builtin_va_list ap) { (void)f; (void)fmt; (void)ap; return 0; }
int vsprintf(char *buf, const char *fmt, __builtin_va_list ap) { (void)fmt; (void)ap; buf[0]='\0'; return 0; }
int vsnprintf(char *buf, size_t n, const char *fmt, __builtin_va_list ap)
{
    (void)fmt; (void)ap;
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

/* Additional FILE stubs */
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    (void)ptr; (void)size; (void)nmemb; (void)stream;
    return nmemb;
}
int fflush(FILE *stream) { (void)stream; return 0; }
int ferror(FILE *stream) { (void)stream; return 0; }
int fgetc(FILE *stream)
{
    if (!stream || stream->pos >= stream->size) return EOF;
    return stream->base[stream->pos++];
}
char *fgets(char *s, int size, FILE *stream)
{
    if (!stream || size <= 0) return NULL;
    int i = 0;
    while (i < size - 1 && stream->pos < stream->size) {
        s[i] = (char)stream->base[stream->pos++];
        if (s[i] == '\n') { i++; break; }
        i++;
    }
    if (i == 0) return NULL;
    s[i] = '\0';
    return s;
}
int fputc(int c, FILE *stream) { (void)c; (void)stream; return c; }
int fputs(const char *s, FILE *stream) { (void)s; (void)stream; return 0; }
int ungetc(int c, FILE *stream) { (void)c; (void)stream; return c; }
void rewind(FILE *stream) { if (stream) stream->pos = 0; }
FILE *freopen(const char *path, const char *mode, FILE *stream)
{
    (void)path; (void)mode; (void)stream;
    return NULL;
}
int remove(const char *path) { (void)path; return -1; }
int fileno(FILE *stream) { (void)stream; return -1; }

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
/*  Additional Stubs for doomgeneric                                  */
/* ================================================================== */

/* string.h extras */
char *strcat(char *dest, const char *src)
{
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

char *strncat(char *dest, const char *src, size_t n)
{
    char *d = dest;
    while (*d) d++;
    for (size_t i = 0; i < n && src[i]; i++) *d++ = src[i];
    *d = '\0';
    return dest;
}

char *strstr(const char *haystack, const char *needle)
{
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char *)haystack;
    }
    return NULL;
}

size_t strspn(const char *s, const char *accept)
{
    size_t count = 0;
    for (; *s; s++) {
        const char *a = accept;
        int found = 0;
        for (; *a; a++) if (*s == *a) { found = 1; break; }
        if (!found) break;
        count++;
    }
    return count;
}

size_t strcspn(const char *s, const char *reject)
{
    size_t count = 0;
    for (; *s; s++) {
        const char *r = reject;
        for (; *r; r++) if (*s == *r) return count;
        count++;
    }
    return count;
}

char *strtok(char *str, const char *delim)
{
    static char *saved;
    if (str) saved = str;
    if (!saved) return NULL;
    saved += strspn(saved, delim);
    if (!*saved) { saved = NULL; return NULL; }
    char *token = saved;
    saved += strcspn(saved, delim);
    if (*saved) *saved++ = '\0';
    else saved = NULL;
    return token;
}

char *strerror(int errnum)
{
    (void)errnum;
    return "error";
}

int strcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return *(unsigned char *)a - *(unsigned char *)b;
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n && *a && *b; i++, a++, b++) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
    }
    return 0;
}

/* unistd.h / fcntl.h stubs */
int access(const char *path, int mode) { (void)path; (void)mode; return -1; }
int close(int fd) { (void)fd; return 0; }
int open(const char *path, int flags, ...) { (void)path; (void)flags; return -1; }
int stat(const char *path, struct stat *buf) { (void)path; (void)buf; return -1; }
int mkdir(const char *path, unsigned int mode) { (void)path; (void)mode; return -1; }
int rename(const char *old, const char *new_name) { (void)old; (void)new_name; return -1; }

/* system() stub — Doom calls this on I_Error for cleanup */
int system(const char *cmd) { (void)cmd; return -1; }

/* ================================================================== */
/*  Sound Stubs (no audio hardware driver)                            */
/* ================================================================== */

/*
 * doomgeneric's sound system expects these functions from i_sound.c.
 * Since we have no audio driver, all are no-ops.
 */

/* Global referenced by s_sound.c */
int snd_musicdevice = 0;

void I_InitSound(void) {}
void I_ShutdownSound(void) {}
void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}

int I_GetSfxLumpNum(void *sfxinfo) { (void)sfxinfo; return 0; }
int I_StartSound(void *sfxinfo, int channel, int vol, int sep)
{
    (void)sfxinfo; (void)channel; (void)vol; (void)sep;
    return 0;
}
void I_StopSound(int channel) { (void)channel; }
int  I_SoundIsPlaying(int channel) { (void)channel; return 0; }
void I_UpdateSound(void) {}
void I_UpdateSoundParams(int channel, int vol, int sep)
{
    (void)channel; (void)vol; (void)sep;
}
void I_PrecacheSounds(void *sounds, int num) { (void)sounds; (void)num; }

void *I_RegisterSong(void *data, int len) { (void)data; (void)len; return NULL; }
void  I_UnRegisterSong(void *handle) { (void)handle; }
void  I_PlaySong(void *handle, int looping) { (void)handle; (void)looping; }
void  I_PauseSong(void) {}
void  I_ResumeSong(void) {}
void  I_StopSong(void) {}
int   I_MusicIsPlaying(void) { return 0; }
void  I_SetMusicVolume(int volume) { (void)volume; }

void  I_BindSoundVariables(void) {}

/* math.h — implementations for freestanding environment */
double floor(double x)
{
    /* Cast to integer truncates toward zero; adjust for negative values */
    long long i = (long long)x;
    double d = (double)i;
    return (x < d) ? d - 1.0 : d;
}

double ceil(double x)
{
    double f = floor(x);
    return (x > f) ? f + 1.0 : f;
}

double fabs(double x) { return x < 0.0 ? -x : x; }
float  fabsf(float x) { return x < 0.0f ? -x : x; }

double sqrt(double x)
{
    double result;
    __asm__ volatile ("fsqrt" : "=t"(result) : "0"(x));
    return result;
}

double sin(double x)
{
    double result;
    __asm__ volatile ("fsin" : "=t"(result) : "0"(x));
    return result;
}

double cos(double x)
{
    double result;
    __asm__ volatile ("fcos" : "=t"(result) : "0"(x));
    return result;
}

double atan2(double y, double x)
{
    double result;
    __asm__ volatile ("fpatan" : "=t"(result) : "0"(x), "u"(y) : "st(1)");
    return result;
}

double log(double x)
{
    double result;
    double one = 1.0;
    __asm__ volatile (
        "fldln2\n\t"
        "fxch %%st(1)\n\t"
        "fyl2x"
        : "=t"(result) : "0"(x), "u"(one) : "st(1)"
    );
    return result;
}

double pow(double base, double exp)
{
    /* Simple integer exponent fast path; general case via exp(exp*log(base)) */
    if (base == 0.0) return 0.0;
    if (exp == 0.0) return 1.0;
    double ln_base = log(fabs(base));
    double result;
    double val = exp * ln_base;
    /* e^val via x87: 2^(val/ln2) */
    __asm__ volatile (
        "fldl2e\n\t"
        "fmulp %%st, %%st(1)\n\t"
        "fld %%st(0)\n\t"
        "frndint\n\t"
        "fsub %%st, %%st(1)\n\t"
        "fxch\n\t"
        "f2xm1\n\t"
        "fld1\n\t"
        "faddp\n\t"
        "fscale\n\t"
        "fstp %%st(1)"
        : "=t"(result) : "0"(val)
    );
    return (base < 0.0 && ((int)exp & 1)) ? -result : result;
}

double fmod(double x, double y)
{
    if (y == 0.0) return 0.0;
    return x - floor(x / y) * y;
}

double ldexp(double x, int exp)
{
    /* x * 2^exp */
    while (exp > 0) { x *= 2.0; exp--; }
    while (exp < 0) { x *= 0.5; exp++; }
    return x;
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
#define DOOM_RESX DOOMGENERIC_RESX
#define DOOM_RESY DOOMGENERIC_RESY

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

int DG_GetKey(int *pressed, unsigned char *key)
{
    int doom_key;
    int is_pressed;
    if (keyboard_poll(&doom_key, &is_pressed)) {
        *pressed = is_pressed;
        *key     = (unsigned char)doom_key;
        return 1;
    }
    return 0;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;  /* No window manager — ignore */
}

/* ================================================================== */
/*  Kernel Entry Point                                                */
/* ================================================================== */

/* BSS boundaries — provided by the linker script */
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

static void halt(void) __attribute__((noreturn));
static void halt(void)
{
    for (;;) __asm__ volatile ("hlt");
}

void _start(void) __attribute__((noreturn));
void _start(void)
{
    /* ---- Zero BSS (freestanding — not done automatically) ---- */
    {
        uint64_t *p = (uint64_t *)__bss_start;
        uint64_t *end = (uint64_t *)__bss_end;
        while (p < end)
            *p++ = 0;
    }

    /* ---- Verify Limine base revision handshake ---- */
    if (!LIMINE_BASE_REVISION_SUPPORTED) {
        halt();
    }

    /* ---- Framebuffer setup ---- */
    struct limine_framebuffer_response *fb_resp = fb_request.response;
    if (!fb_resp || fb_resp->framebuffer_count < 1) {
        halt();
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
