/*
 * unistd.h — Freestanding stub for doomOS
 */

#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <sys/types.h>

int access(const char *path, int mode);
int close(int fd);

#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0

#endif /* _UNISTD_H */
