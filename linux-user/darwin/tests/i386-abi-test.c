/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { perror(#x); return 1; } } while (0)

int main(void)
{
    struct {
        uint32_t index, base, limit, flags;
    } descriptor = { .index = UINT32_MAX, .limit = 0xfffff, .flags = 0x51 };
    uint32_t value = 123;
    descriptor.base = (uintptr_t)&value;
    CHECK(syscall(SYS_set_thread_area, &descriptor) == 0);
    CHECK(descriptor.index >= 6 && descriptor.index <= 8);
    uint32_t index = descriptor.index;
    descriptor.base = 0;
    CHECK(syscall(SYS_get_thread_area, &descriptor) == 0);
    CHECK(descriptor.base == (uintptr_t)&value && descriptor.limit == 0xfffff);
    descriptor = (typeof(descriptor)){ .index = index, .flags = 0x28 };
    CHECK(syscall(SYS_set_thread_area, &descriptor) == 0);
    descriptor.index = 100;
    CHECK(syscall(SYS_set_thread_area, &descriptor) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_get_thread_area, NULL) == -1 && errno == EFAULT);

    struct { uint32_t limits[2], guard; } limits = { .guard = 0x12345678 };
    CHECK(syscall(SYS_ugetrlimit, RLIMIT_NOFILE, &limits) == 0);
    CHECK(limits.limits[0] > 0 && limits.guard == 0x12345678);
    CHECK(syscall(SYS_getrlimit, RLIMIT_NOFILE, &limits) == 0);
    CHECK(limits.guard == 0x12345678);

    unsigned long args[6] = {0, 4096, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1UL, 0};
    void *p = (void *)syscall(SYS_mmap, args);
    CHECK(p != MAP_FAILED);
    memset(p, 0x5a, 4096);
    CHECK(((unsigned char *)p)[4095] == 0x5a);
    CHECK(munmap(p, 4096) == 0);

    char name[] = "/private/tmp/linonmac-i386-abi-XXXXXX";
    int fd = mkstemp(name);
    CHECK(fd >= 0);
    off_t offset = (1ULL << 32) + 0x4000;
    CHECK(pwrite(fd, "large-offset", 12, offset) == 12);
    char data[12];
    CHECK(pread(fd, data, 12, offset) == 12);
    CHECK(memcmp(data, "large-offset", 12) == 0);
    struct stat64 st;
    CHECK(syscall(SYS_fstat64, fd, &st) == 0 && st.st_size == offset + 12);
    uint64_t position = 0;
    CHECK(syscall(SYS__llseek, fd, 1, 0x4000, &position, SEEK_SET) == 0);
    CHECK(position == offset);
    p = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, offset);
    CHECK(p != MAP_FAILED && memcmp(p, data, 12) == 0);
    CHECK(munmap(p, 4096) == 0);
    CHECK(ftruncate(fd, offset + 4096) == 0);
    CHECK(fstat64(fd, &st) == 0 && st.st_size == offset + 4096);

    int64_t stamp[4] = {2200000000LL, 123456789, 2200000001LL, 987654321};
    CHECK(syscall(SYS_utimensat_time64, fd, NULL, stamp, 0) == 0);
    CHECK(syscall(SYS_fstat64, fd, &st) == 0);
    CHECK((uint32_t)st.st_mtim.tv_sec == (uint32_t)stamp[2]);
    CHECK(st.st_mtim.tv_nsec == stamp[3]);
    int64_t now[2];
    CHECK(syscall(SYS_clock_gettime64, CLOCK_MONOTONIC, now) == 0);
    CHECK(now[0] > 0 && now[1] >= 0 && now[1] < 1000000000);
    CHECK(syscall(SYS_clock_gettime64, CLOCK_MONOTONIC, NULL) == -1 &&
          errno == EFAULT);
    int64_t delay[2] = {0, 1000000};
    CHECK(syscall(SYS_clock_nanosleep_time64, CLOCK_MONOTONIC, 0,
                  delay, NULL) == 0);
    uint32_t futex = 0;
    int64_t deadline[2] = {0, 1};
    CHECK(syscall(SYS_futex_time64, &futex, FUTEX_WAIT_PRIVATE, 0,
                  deadline, NULL, 0) == -1 && errno == ETIMEDOUT);
    close(fd);
    unlink(name);

    int pair[2];
    unsigned long socketargs[4] = {AF_UNIX, SOCK_STREAM, 0, (uintptr_t)pair};
    CHECK(syscall(SYS_socketcall, 8, socketargs) == 0);
    char message[] = "legacy socketcall";
    unsigned long sendargs[4] = {
        pair[0], (uintptr_t)message, sizeof(message), 0,
    };
    CHECK(syscall(SYS_socketcall, 9, sendargs) == sizeof(message));
    char received[sizeof(message)];
    unsigned long recvargs[4] = {
        pair[1], (uintptr_t)received, sizeof(received), 0,
    };
    CHECK(syscall(SYS_socketcall, 10, recvargs) == sizeof(received));
    CHECK(memcmp(received, message, sizeof(message)) == 0);
    close(pair[0]);
    close(pair[1]);
    puts("I386 ABI CHECKS PASSED");
    return 0;
}
