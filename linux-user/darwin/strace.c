/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu.h"
#include "strace.h"
#include "exec/log.h"
#include "user-internals.h"
static const char *syscall_name(int num)
{
    switch (num) {
#include "syscall-names.inc"
    default:
        return "unknown";
    }
}
void print_syscall(CPUArchState *env, int num, abi_long a1, abi_long a2,
                   abi_long a3, abi_long a4, abi_long a5, abi_long a6)
{
    qemu_log("%d %s[%d](%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64
             ",%" PRId64 ",%" PRId64 ")",
             getpid(), syscall_name(num), num, (int64_t)a1, (int64_t)a2,
             (int64_t)a3, (int64_t)a4, (int64_t)a5, (int64_t)a6);
}
void print_syscall_ret(CPUArchState *env, int num, abi_long ret, abi_long a1,
                       abi_long a2, abi_long a3, abi_long a4, abi_long a5,
                       abi_long a6)
{
    qemu_log(" = %" PRId64 "\n", (int64_t)ret);
}
void print_taken_signal(int sig, const target_siginfo_t *info)
{
    qemu_log("--- signal %d code=%d addr=%" PRIx64 " ---\n", sig, info->si_code,
             (uint64_t)info->_sifields._sigfault._addr);
    if (sig == TARGET_SIGSEGV || sig == TARGET_SIGBUS) {
        cpu_dump_state(thread_cpu, stderr, CPU_DUMP_FPU);
    }
}
