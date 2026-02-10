/*
 * sys/stat.h — Freestanding stub for doomOS
 */

#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>

struct stat {
    unsigned long st_size;
    unsigned long st_mode;
};

int stat(const char *path, struct stat *buf);
int mkdir(const char *path, unsigned int mode);

#define S_ISDIR(m) (0)

#endif /* _SYS_STAT_H */
