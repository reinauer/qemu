/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define CHECK(x) do { \
    if (!(x)) { \
        perror(#x); \
        return 1; \
    } \
} while (0)
int main(void)
{
    char path[80], self[4096], by_pid[4096], via_at[4096];
    unsigned char aux_self[4096], aux_pid[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self));
    CHECK(n > 0);
    snprintf(path, sizeof(path), "/proc/%d/exe", getpid());
    CHECK(readlink(path, by_pid, sizeof(by_pid)) == n);
    CHECK(memcmp(self, by_pid, n) == 0);
    CHECK(readlinkat(AT_FDCWD, path, via_at, sizeof(via_at)) == n);
    CHECK(memcmp(self, via_at, n) == 0);
    int fd = open(path, O_RDONLY);
    CHECK(fd >= 0);
    char elf[4]; CHECK(read(fd, elf, 4) == 4);
    CHECK(memcmp(elf, "\177ELF", 4) == 0);
    close(fd);
    fd = open("/proc/self/auxv", O_RDONLY);
    CHECK(fd >= 0); n = read(fd, aux_self, sizeof(aux_self)); close(fd);
    CHECK(n > 0);
    snprintf(path, sizeof(path), "/proc/%d/auxv", getpid());
    fd = open(path, O_RDONLY); CHECK(fd >= 0);
    CHECK(read(fd, aux_pid, sizeof(aux_pid)) == n); close(fd);
    CHECK(memcmp(aux_self, aux_pid, n) == 0);
    puts("PROC SELF CHECKS PASSED");
    return 0;
}
