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
#include <sys/ioctl.h>
#include <sys/filio.h>
#include <sys/ttycom.h>
#include <sys/uio.h>
#include <sys/mount.h>
#include <termios.h>

#define TARGET_AT_FDCWD -100

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

static int host_dirfd(int fd)
{
    return fd == TARGET_AT_FDCWD ? AT_FDCWD : fd;
}

static int host_open_flags(int flags)
{
    int host = flags & TARGET_O_ACCMODE;
#define FLAG(t, h)                                                             \
    if (flags & TARGET_##t) {                                                  \
        host |= h;                                                             \
    }
    FLAG(O_CREAT, O_CREAT);
    FLAG(O_EXCL, O_EXCL);
    FLAG(O_TRUNC, O_TRUNC);
    FLAG(O_APPEND, O_APPEND);
    FLAG(O_NONBLOCK, O_NONBLOCK);
    FLAG(O_NOCTTY, O_NOCTTY);
    FLAG(O_CLOEXEC, O_CLOEXEC);
    FLAG(O_DIRECTORY, O_DIRECTORY);
    FLAG(O_NOFOLLOW, O_NOFOLLOW);
    FLAG(O_SYNC, O_SYNC);
    FLAG(O_DSYNC, O_DSYNC);
#undef FLAG
    return host;
}

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

static abi_long guest_open(CPUArchState *env, int fd, abi_ulong addr, int flags,
                           mode_t mode)
{
    char *name = lock_user_string(addr);
    int ret;
    if (!name) {
        return -TARGET_EFAULT;
    }
    if ((flags & TARGET_O_PATH) ||
        (flags & TARGET_O_TMPFILE) == TARGET_O_TMPFILE) {
        unlock_user(name, addr, 0);
        return -TARGET_EOPNOTSUPP;
    }
    ret = get_errno(do_guest_openat(env, host_dirfd(fd), name,
                                    host_open_flags(flags), mode, false));
    unlock_user(name, addr, 0);
    return ret;
}

static abi_long guest_io(int fd, abi_ulong addr, abi_ulong count, bool writing,
                         bool positioned, off_t offset)
{
    void *buf;
    abi_long ret;
    if (count > SSIZE_MAX) {
        return -TARGET_EINVAL;
    }
    if (!count) {
        return 0;
    }
    count = MIN(count, 16 * 1024 * 1024);
    buf = lock_user(writing ? VERIFY_READ : VERIFY_WRITE, addr, count, writing);
    if (!buf) {
        return -TARGET_EFAULT;
    }
    /* Keep Darwin's kernel from faulting a same-file mmap during I/O. */
    g_autofree void *bounce = g_try_malloc(count);
    if (!bounce) {
        unlock_user(buf, addr, 0);
        return -TARGET_ENOMEM;
    }
    if (writing) {
        memcpy(bounce, buf, count);
    }
    if (positioned) {
        ret = get_errno(writing ? pwrite(fd, bounce, count, offset)
                                : pread(fd, bounce, count, offset));
    } else {
        ret = get_errno(writing ? write(fd, bounce, count)
                                : read(fd, bounce, count));
    }
    if (!writing && ret > 0) {
        memcpy(buf, bounce, ret);
    }
    unlock_user(buf, addr, !writing && ret > 0 ? ret : 0);
    return ret;
}

static abi_long guest_iov(int fd, abi_ulong addr, int count, bool writing)
{
    struct target_iovec *target;
    struct iovec *host;
    void **guest;
    size_t total = 0;
    g_autofree char *bounce = NULL;
    abi_long ret = -TARGET_EFAULT;
    if (count < 0 || count > IOV_MAX) {
        return -TARGET_EINVAL;
    }
    if (!count) {
        return 0;
    }
    target = lock_user(VERIFY_READ, addr, count * sizeof(*target), 1);
    if (!target) {
        return -TARGET_EFAULT;
    }
    host = g_new0(struct iovec, count);
    guest = g_new0(void *, count);
    for (int i = 0; i < count; i++) {
        size_t len = tswapal(target[i].iov_len);
        if (len > SSIZE_MAX || total > SSIZE_MAX - len) {
            ret = -TARGET_EINVAL;
            goto done;
        }
        total += len;
    }
    total = MIN(total, 16 * 1024 * 1024);
    bounce = g_try_malloc(total ?: 1);
    if (!bounce) {
        ret = -TARGET_ENOMEM;
        goto done;
    }
    size_t offset = 0;
    for (int i = 0; i < count; i++) {
        abi_ulong base = tswapal(target[i].iov_base);
        size_t len = MIN(tswapal(target[i].iov_len), total - offset);
        host[i].iov_len = len;
        host[i].iov_base = bounce + offset;
        if (len) {
            guest[i] = lock_user(writing ? VERIFY_READ : VERIFY_WRITE, base,
                                 len, writing);
            if (!guest[i]) {
                goto done;
            }
            if (writing) {
                memcpy(host[i].iov_base, guest[i], len);
            }
        }
        offset += len;
    }
    ret = get_errno(writing ? writev(fd, host, count) : readv(fd, host, count));
done:;
    size_t remaining = !writing && ret > 0 ? ret : 0;
    for (int i = 0; i < count; i++) {
        if (guest[i]) {
            size_t copied = MIN(remaining, host[i].iov_len);
            if (copied) {
                memcpy(guest[i], host[i].iov_base, copied);
            }
            unlock_user(guest[i], tswapal(target[i].iov_base), copied);
            remaining -= copied;
        }
    }
    g_free(guest);
    g_free(host);
    unlock_user(target, addr, 0);
    return ret;
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

static abi_long guest_fcntl(int fd, int cmd, abi_long arg)
{
    switch (cmd) {
    case TARGET_F_DUPFD:
        return get_errno(fcntl(fd, F_DUPFD, arg));
    case TARGET_F_DUPFD_CLOEXEC:
        return get_errno(fcntl(fd, F_DUPFD_CLOEXEC, arg));
    case TARGET_F_GETFD:
        return get_errno(fcntl(fd, F_GETFD));
    case TARGET_F_SETFD:
        return get_errno(fcntl(fd, F_SETFD, arg));
    case TARGET_F_GETFL: {
        int flags = fcntl(fd, F_GETFL), out;
        if (flags < 0) {
            return get_errno(-1);
        }
        out = flags & O_ACCMODE;
        if (flags & O_APPEND) {
            out |= TARGET_O_APPEND;
        }
        if (flags & O_NONBLOCK) {
            out |= TARGET_O_NONBLOCK;
        }
        return out;
    }
    case TARGET_F_SETFL:
        return get_errno(fcntl(fd, F_SETFL, host_open_flags(arg)));
    default:
        return -TARGET_EINVAL;
    }
}

static abi_long guest_syscall(CPUArchState *env, int num, abi_long a1,
                              abi_long a2, abi_long a3, abi_long a4,
                              abi_long a5, abi_long a6)
{
    abi_long ret;
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
