/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#define _TIME_BITS 64
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { perror(#x); return 1; } } while (0)
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int ready;

static void *worker(void *unused)
{
    pthread_mutex_lock(&mutex);
    ready = 1;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
    return NULL;
}

int main(void)
{
    char name[] = "/private/tmp/linonmac-time64-XXXXXX";
    int fd = mkstemp(name);
    CHECK(fd >= 0);
    struct timespec times[2] = { {2200000000LL, 123}, {2200000001LL, 456} };
    CHECK(futimens(fd, times) == 0);
    struct stat st;
    CHECK(fstat(fd, &st) == 0);
    CHECK(st.st_atim.tv_sec == times[0].tv_sec && st.st_atim.tv_nsec == 123);
    CHECK(st.st_mtim.tv_sec == times[1].tv_sec && st.st_mtim.tv_nsec == 456);
    struct timespec now, delay = {0, 1000000};
    CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0 && now.tv_sec > 0);
    CHECK(clock_nanosleep(CLOCK_MONOTONIC, 0, &delay, NULL) == 0);
    pthread_t thread;
    pthread_mutex_lock(&mutex);
    CHECK(pthread_create(&thread, NULL, worker, NULL) == 0);
    while (!ready) {
        CHECK(pthread_cond_timedwait(&cond, &mutex, &times[0]) == 0);
    }
    pthread_mutex_unlock(&mutex);
    CHECK(pthread_join(thread, NULL) == 0);
    close(fd);
    unlink(name);
    puts("I386 TIME64 CHECKS PASSED");
    return 0;
}
