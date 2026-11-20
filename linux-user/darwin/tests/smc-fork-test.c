/* Local Darwin linux-user regression: nonresident TB-protected pages. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t expected_code;

static void fault(int sig, siginfo_t *info, void *context)
{
    /* The EOF check only verifies termination, not its existing signal ABI. */
    if (!expected_code && (sig == SIGBUS || sig == SIGSEGV)) {
        _exit(0);
    }
    _exit(sig == SIGSEGV && info->si_code == expected_code
          ? 0 : 90 + info->si_code);
}

static int child_status(pid_t pid)
{
    int status;

    if (pid < 0 || waitpid(pid, &status, 0) != pid) {
        return 255;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

int main(void)
{
    const size_t size = 65536;
    unsigned char code[] = { 0xb8, 1, 0, 0, 0, 0xc3 };
    unsigned char *p = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    struct sigaction sa = { .sa_sigaction = fault, .sa_flags = SA_SIGINFO };
    int (*fn)(void) = (void *)p;

    if (p == MAP_FAILED) {
        return 1;
    }
    memcpy(p, code, sizeof(code));
    if (fn() != 1) {
        return 2;
    }
    for (int i = 0; i < 20; i++) {
        pid_t pid = fork();
        int result;

        if (pid == 0) {
            alarm(3);
            /* Do not read the code page before the first child write. */
            *(volatile unsigned int *)(p + 1) = 2;
            _exit(fn() == 2 ? 0 : 3);
        }
        result = child_status(pid);
        if (result) {
            fprintf(stderr, "SMC child %d failed: %d\n", i, result);
            return 4;
        }
        if (fn() != 1) {
            return 5;
        }
        *(volatile unsigned int *)(p + 1) = 3;
        if (fn() != 3) {
            return 6;
        }
        *(volatile unsigned int *)(p + 1) = 1;
        if (fn() != 1) {
            return 7;
        }
    }
    puts("FORK SMC PASSED");

    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, NULL) || sigaction(SIGBUS, &sa, NULL)) {
        return 8;
    }
    for (int mode = 0; mode < 5; mode++) {
        pid_t pid = fork();
        int result;

        if (pid == 0) {
            volatile char *q;
            int fd = -1;
            char path[] = "/tmp/qemu-fault-eof-XXXXXX";

            alarm(3);
            expected_code = mode == 2 ? SEGV_MAPERR : SEGV_ACCERR;
            if (mode == 4) {
                fd = mkstemp(path);
                if (fd < 0 || unlink(path)) {
                    _exit(9);
                }
                expected_code = 0;
            }
            q = mmap(NULL, size, mode == 4 ? PROT_READ | PROT_WRITE
                                           : PROT_READ,
                     MAP_PRIVATE | (mode == 4 ? 0 : MAP_ANONYMOUS), fd, 0);
            if (q == MAP_FAILED) {
                _exit(10);
            }
            if ((mode == 1 || mode == 3) &&
                mprotect((void *)q, size, PROT_NONE)) {
                _exit(11);
            }
            if (mode == 2 && munmap((void *)q, size)) {
                _exit(12);
            }
            if (mode == 3) {
                char value = *q;
                (void)value;
            } else {
                *q = 1;
            }
            _exit(13);
        }
        result = child_status(pid);
        if (result) {
            fprintf(stderr, "fault mode %d failed: %d\n", mode, result);
            return 14;
        }
    }
    puts("FAULT CLASSIFICATION AND EOF TERMINATION PASSED");
    return 0;
}
