/*
 * stdlib.h — Freestanding stub for doomOS
 *
 * Declares the standard library functions implemented in kernel.c.
 */

#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#define RAND_MAX 0x7FFF

void *malloc(size_t size);
void  free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

void exit(int status) __attribute__((noreturn));

int    atoi(const char *s);
long   strtol(const char *nptr, char **endptr, int base);
double atof(const char *s);
int    abs(int x);
char  *getenv(const char *name);

#endif /* _STDLIB_H */
