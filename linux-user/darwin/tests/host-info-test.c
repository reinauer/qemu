/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/sysinfo.h>
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
    struct sysinfo info;
    CHECK(sysinfo(&info) == 0);
    uint64_t bytes = (uint64_t)info.totalram * info.mem_unit;
    CHECK(bytes >= 1024 * 1024 && bytes < (1ULL << 50));
    CHECK(info.freeram <= info.totalram && info.uptime > 0 && info.procs > 0);
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CHECK(sched_getaffinity(0, sizeof(mask), &mask) == 0 &&
          CPU_COUNT(&mask) > 0);
    CHECK(sched_getaffinity(0, 1, &mask) == -1 && errno == EINVAL);
    printf("HOST INFO CHECKS PASSED ram=%llu cpus=%d\n",
           (unsigned long long)bytes, CPU_COUNT(&mask));
    return 0;
}
