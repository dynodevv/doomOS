/*
 * stdio.h — Freestanding stub for doomOS
 *
 * Provides FILE and standard I/O declarations backed by the
 * memory-mapped WAD reader in kernel.c.
 */

#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define EOF (-1)

/* FILE is an opaque type — defined fully in kernel.c */
typedef struct _MEMFILE FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE  *fopen(const char *path, const char *mode);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
int    fseek(FILE *stream, long offset, int whence);
long   ftell(FILE *stream);
int    fclose(FILE *stream);
int    feof(FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int    fflush(FILE *stream);
int    ferror(FILE *stream);
int    fgetc(FILE *stream);
char  *fgets(char *s, int size, FILE *stream);
int    fputc(int c, FILE *stream);
int    fputs(const char *s, FILE *stream);
int    ungetc(int c, FILE *stream);
void   rewind(FILE *stream);
FILE  *freopen(const char *path, const char *mode, FILE *stream);
int    remove(const char *path);
int    rename(const char *old, const char *new);

int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t n, const char *fmt, ...);
int sscanf(const char *str, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int vsprintf(char *buf, const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);

int puts(const char *s);
int putchar(int c);

int    fileno(FILE *stream);

#endif /* _STDIO_H */
