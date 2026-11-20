/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            perror(#x);                                                        \
            return 1;                                                          \
        }                                                                      \
    } while (0)
int main(void)
{
    char dir[] = "/tmp/linonmac-fs-XXXXXX";
    CHECK(mkdtemp(dir));
    int parent = open(dir, O_RDONLY | O_DIRECTORY);
    CHECK(parent >= 0);
    int fd = openat(parent, "data", O_CREAT | O_RDWR, 0600);
    CHECK(fd >= 0);
    CHECK(write(fd, "data", 4) == 4);
    CHECK(symlinkat("data", parent, "link") == 0);
    char path[256];
    snprintf(path, sizeof(path), "%s/link", dir);
    char target[16];
    CHECK(readlink(path, target, sizeof(target)) == 4 &&
          !memcmp(target, "data", 4));
    struct timespec times[2] = { {1600000000, 123456789},
                                {1600000001, 987654321} };
    CHECK(futimens(fd, times) == 0);
    struct stat st;
    CHECK(fstat(fd, &st) == 0 && st.st_mtime == 1600000001 &&
          st.st_mtim.tv_nsec == 987654321);
    times[0].tv_nsec = UTIME_OMIT;
    times[1].tv_nsec = UTIME_NOW;
    CHECK(utimensat(parent, "data", times, 0) == 0);
    CHECK(fstat(fd, &st) == 0 && st.st_atime == 1600000000 &&
          st.st_mtime > 1600000001);
    times[0].tv_nsec = -5;
    CHECK(futimens(fd, times) == -1 && errno == EINVAL);
    CHECK(unlinkat(parent, "link", 0) == 0 && unlinkat(parent, "data", 0) == 0);
    close(fd);
    close(parent);
    CHECK(rmdir(dir) == 0);
    puts("FILESYSTEM EXTRA CHECKS PASSED");
    return 0;
}
