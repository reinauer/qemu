/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

static void *worker(void *unused)
{
    sigset_t mask;
    sigfillset(&mask);
    assert(pthread_sigmask(SIG_BLOCK, &mask, NULL) == 0);
    return NULL;
}

int main(void)
{
    unsigned char *code = mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(code != MAP_FAILED);
    /* mov $1,%eax; ret -- executing this makes QEMU protect the code page. */
    memcpy(code, "\xb8\x01\x00\x00\x00\xc3", 6);
    int (*fn)(void) = (void *)code;
    assert(fn() == 1);
    for (int i = 2; i < 12; i++) {
        pthread_t thread;
        assert(pthread_create(&thread, NULL, worker, NULL) == 0);
        assert(pthread_join(thread, NULL) == 0);
        /* Thread shutdown must not block the main thread's SMC fault. */
        memcpy(code + 1, &i, sizeof(i));
        assert(fn() == i);
    }
    assert(munmap(code, 16384) == 0);
    puts("thread exit preserves main-thread SMC fault handling");
    return 0;
}
