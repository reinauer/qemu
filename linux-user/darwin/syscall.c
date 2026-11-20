/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Linux guest syscalls on Darwin hosts.
 *
 * The current Linux implementation remains separate: Darwin libc constants,
 * data structures and errno values must never be passed through as Linux ABI.
 */
#include "qemu/osdep.h"
#include "qemu.h"
#include "user-internals.h"
#include "user-mmap.h"
#include "signal-common.h"
#include "strace.h"
#include "fd-trans.h"
#include "qemu/path.h"
#include "qemu/cutils.h"
#include "exec/log.h"
#include "exec/mmap-lock.h"
#include "user/cpu_loop.h"
#include "user/signal.h"
#include "gdbstub/syscalls.h"
#include <sys/utsname.h>
#include <sys/mount.h>

TargetFdTrans **target_fd_trans;
QemuMutex target_fd_trans_lock;
unsigned int target_fd_max;

struct errno_pair {
    int host, guest;
};
static const struct errno_pair errno_map[] = {
#define ERR(e) {e, TARGET_##e}
    ERR(EPERM),
    ERR(ENOENT),
    ERR(ESRCH),
    ERR(EINTR),
    ERR(EIO),
    ERR(ENXIO),
    ERR(E2BIG),
    ERR(ENOEXEC),
    ERR(EBADF),
    ERR(ECHILD),
    ERR(EAGAIN),
    ERR(ENOMEM),
    ERR(EACCES),
    ERR(EFAULT),
    ERR(ENOTBLK),
    ERR(EBUSY),
    ERR(EEXIST),
    ERR(EXDEV),
    ERR(ENODEV),
    ERR(ENOTDIR),
    ERR(EISDIR),
    ERR(EINVAL),
    ERR(ENFILE),
    ERR(EMFILE),
    ERR(ENOTTY),
    ERR(ETXTBSY),
    ERR(EFBIG),
    ERR(ENOSPC),
    ERR(ESPIPE),
    ERR(EROFS),
    ERR(EMLINK),
    ERR(EPIPE),
    ERR(EDOM),
    ERR(ERANGE),
    ERR(EDEADLK),
    ERR(ENAMETOOLONG),
    ERR(ENOLCK),
    ERR(ENOSYS),
    ERR(ENOTEMPTY),
    ERR(ELOOP),
    ERR(ENOMSG),
    ERR(EIDRM),
    ERR(EOVERFLOW),
    ERR(EILSEQ),
    ERR(ENOTSOCK),
    ERR(EDESTADDRREQ),
    ERR(EMSGSIZE),
    ERR(EPROTOTYPE),
    ERR(ENOPROTOOPT),
    ERR(EPROTONOSUPPORT),
    ERR(ESOCKTNOSUPPORT),
    ERR(EOPNOTSUPP),
    ERR(EPFNOSUPPORT),
    ERR(EAFNOSUPPORT),
    ERR(EADDRINUSE),
    ERR(EADDRNOTAVAIL),
    ERR(ENETDOWN),
    ERR(ENETUNREACH),
    ERR(ENETRESET),
    ERR(ECONNABORTED),
    ERR(ECONNRESET),
    ERR(ENOBUFS),
    ERR(EISCONN),
    ERR(ENOTCONN),
    ERR(ESHUTDOWN),
    ERR(ETOOMANYREFS),
    ERR(ETIMEDOUT),
    ERR(ECONNREFUSED),
    ERR(EHOSTDOWN),
    ERR(EHOSTUNREACH),
    ERR(EALREADY),
    ERR(EINPROGRESS),
    ERR(ESTALE),
    ERR(EDQUOT),
    ERR(ECANCELED),
    ERR(EOWNERDEAD),
    ERR(ENOTRECOVERABLE),
    ERR(ENODATA),
    ERR(ENOSTR),
    ERR(ENOSR),
    ERR(ETIME),
    ERR(EPROTO),
    ERR(EBADMSG),
    ERR(EMULTIHOP),
    { ENOTSUP, TARGET_EOPNOTSUPP },
    ERR(ENOLINK),
#undef ERR
};

static int host_to_target_errno(int err)
{
    if (!err) {
        return 0;
    }
    if (err == QEMU_ERESTARTSYS) {
        return err;
    }
    for (size_t i = 0; i < ARRAY_SIZE(errno_map); i++) {
        if (errno_map[i].host == err) {
            return errno_map[i].guest;
        }
    }
    return TARGET_EIO;
}

static int target_to_host_errno(int err)
{
    /* This API returns a positive Darwin errno, not a syscall result. */
    int host_errno = EIO;
    if (!err) {
        return 0;
    }
    for (size_t i = 0; i < ARRAY_SIZE(errno_map); i++) {
        if (errno_map[i].guest == err) {
            return errno_map[i].host;
        }
    }
    return host_errno;
}

abi_long get_errno(abi_long ret)
{
    return ret == -1 ? -host_to_target_errno(errno) : ret;
}

const char *target_strerror(int err)
{
    return strerror(target_to_host_errno(err));
}

void target_exception_dump(CPUArchState *env, const char *fmt, int code)
{
    fprintf(stderr, fmt, code);
    cpu_dump_state(env_cpu(env), stderr, CPU_DUMP_FPU);
}

void syscall_init(void)
{
}

static abi_ulong target_brk, initial_target_brk;
void target_set_brk(abi_ulong value)
{
    initial_target_brk = target_brk = TARGET_PAGE_ALIGN(value);
}

abi_long do_brk(abi_ulong value)
{
    abi_ulong old = TARGET_PAGE_ALIGN(target_brk);
    abi_ulong next = TARGET_PAGE_ALIGN(value);
    if (value < initial_target_brk) {
        return target_brk;
    }
    if (next < old) {
        if (target_munmap(next, old - next)) {
            return target_brk;
        }
    } else if (next > old) {
        if (target_mmap(old, next - old, PROT_READ | PROT_WRITE,
                        MAP_FIXED_NOREPLACE | MAP_ANON | MAP_PRIVATE, -1,
                        0) != old) {
            return target_brk;
        }
    }
    return target_brk = value;
}

#ifdef TARGET_X86_64
abi_long do_arch_prctl(CPUX86State *env, int code, abi_ulong addr)
{
    int idx;
    switch (code) {
    case TARGET_ARCH_SET_GS:
    case TARGET_ARCH_SET_FS:
        idx = code == TARGET_ARCH_SET_GS ? R_GS : R_FS;
        cpu_x86_load_seg(env, idx, 0);
        env->segs[idx].base = addr;
        return 0;
    case TARGET_ARCH_GET_GS:
    case TARGET_ARCH_GET_FS:
        idx = code == TARGET_ARCH_GET_GS ? R_GS : R_FS;
        return put_user_ual(env->segs[idx].base, addr);
    default:
        return -TARGET_EINVAL;
    }
}
#endif

static bool is_self_proc_entry(const char *name, const char *entry)
{
    char by_pid[64];
    if (g_str_has_prefix(name, "/proc/self/") &&
        !strcmp(name + strlen("/proc/self/"), entry)) {
        return true;
    }
    snprintf(by_pid, sizeof(by_pid), "/proc/%d/%s", getpid(), entry);
    return !strcmp(name, by_pid);
}

int do_guest_openat(CPUArchState *env, int dirfd, const char *name, int flags,
                    mode_t mode, bool safe)
{
    if (is_self_proc_entry(name, "auxv")) {
        TaskState *ts = get_task_state(env_cpu(env));
        char tmp[] = "/private/tmp/qemu-linux-auxv-XXXXXX";
        int fd = mkstemp(tmp);
        if (fd < 0) {
            return -1;
        }
        unlink(tmp);
        if (flags & O_CLOEXEC) {
            fcntl(fd, F_SETFD, FD_CLOEXEC);
        }
        void *auxv =
            lock_user(VERIFY_READ, ts->info->saved_auxv, ts->info->auxv_len, 1);
        if (!auxv ||
            write(fd, auxv, ts->info->auxv_len) != ts->info->auxv_len) {
            unlock_user(auxv, ts->info->saved_auxv, 0);
            close(fd);
            errno = EFAULT;
            return -1;
        }
        unlock_user(auxv, ts->info->saved_auxv, 0);
        lseek(fd, 0, SEEK_SET);
        return fd;
    }
    if (is_self_proc_entry(name, "exe")) {
        name = real_exec_path;
    }
    return openat(dirfd, path(name), flags, mode);
}

ssize_t do_guest_readlink(const char *name, char *buf, size_t size)
{
    if (is_self_proc_entry(name, "exe")) {
        size_t n = MIN(size, strlen(real_exec_path));
        memcpy(buf, real_exec_path, n);
        return n;
    }
    return readlink(path(name), buf, size);
}

#include "thread.c.inc"

static abi_long guest_exit(CPUArchState *env, int code)
{
    if (block_signals()) {
        return -QEMU_ERESTARTSYS;
    }
    preexit_cleanup(env, code);
    _exit(code);
}

static abi_long guest_syscall(CPUArchState *env, int num, abi_long a1,
                              abi_long a2, abi_long a3, abi_long a4,
                              abi_long a5, abi_long a6)
{
    switch (num) {
#include "syscall-cases.inc"
    default:
        qemu_log_mask(LOG_UNIMP, "Unsupported Linux syscall %d on Darwin\n",
                      num);
        return -TARGET_ENOSYS;
    }
}

abi_long do_syscall(CPUArchState *env, int num, abi_long a1, abi_long a2,
                    abi_long a3, abi_long a4, abi_long a5, abi_long a6,
                    abi_long a7, abi_long a8)
{
    abi_long ret;
    if (qemu_loglevel_mask(LOG_STRACE)) {
        print_syscall(env, num, a1, a2, a3, a4, a5, a6);
    }
    ret = guest_syscall(env, num, a1, a2, a3, a4, a5, a6);
    if (qemu_loglevel_mask(LOG_STRACE)) {
        print_syscall_ret(env, num, ret, a1, a2, a3, a4, a5, a6);
    }
    return ret;
}
