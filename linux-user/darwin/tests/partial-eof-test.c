/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { \
    if (!(x)) { \
        perror(#x); \
        return 1; \
    } \
} while (0)
int main(void)
{
    char name[] = "/tmp/linonmac-partial-eof-XXXXXX";
    int fd = mkstemp(name);
    CHECK(fd >= 0);
    unlink(name);
    unsigned char data[0x18000];
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = i / 4096 + 1;
    }
    CHECK(write(fd, data, sizeof(data)) == sizeof(data));

    /* Legacy ELF loaders reserve alignment padding beyond the file's EOF. */
    unsigned char *p = mmap(NULL, 0x200000, PROT_READ,
                             MAP_PRIVATE, fd, 0);
    CHECK(p != MAP_FAILED);
    CHECK(mprotect(p + 0x100000, 0x100000, PROT_NONE) == 0);
    unsigned char *q = mmap(p + 0x1e6000, 8192, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_FIXED, fd, 0x6000);
    CHECK(q == p + 0x1e6000);
    CHECK(memcmp(q, data + 0x6000, 8192) == 0);
    q[4096] = 99;
    CHECK(q[4096] == 99);
    CHECK(munmap(p, 0x200000) == 0);

    /* Valid neighboring guest pages in a shared host page must survive. */
    p = mmap(NULL, 0x4000, PROT_READ, MAP_PRIVATE, fd, 0);
    CHECK(p != MAP_FAILED);
    q = mmap(p + 0x2000, 4096, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_FIXED, fd, 0);
    CHECK(q == p + 0x2000);
    CHECK(p[0] == 1 && p[4096] == 2 && p[0x2000] == 1 && p[0x3000] == 4);
    CHECK(munmap(p, 0x4000) == 0);
    close(fd);
    puts("PARTIAL EOF MAPPING CHECKS PASSED");
    return 0;
}
