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
#include "qemu/guest-random.h"
#include "qemu/rcu.h"
#include "tcg/tcg.h"
#include "tcg/startup.h"
#include "thread-abi.h"
#include "gdbstub/syscalls.h"
#include "elf.h"
#include <mach-o/dyld.h>
#include <sys/utsname.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <sys/filio.h>
#include <sys/ttycom.h>
#include <sys/uio.h>
#include <sys/mount.h>
#include <dirent.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <termios.h>

#define TARGET_AT_FDCWD -100
#define TARGET_AT_SYMLINK_NOFOLLOW 0x100
#define TARGET_AT_NO_AUTOMOUNT 0x800
#define TARGET_AT_REMOVEDIR 0x200
#define TARGET_AT_EACCESS 0x200
#define TARGET_AT_EMPTY_PATH 0x1000
#define TARGET_AT_STATX_SYNC_TYPE 0x6000

#include "host-compat.h"

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

int host_to_target_waitstatus(int status)
{
    if (WIFSIGNALED(status)) {
        return (status & ~0x7f) | host_to_target_signal(WTERMSIG(status));
    }
    if (WIFSTOPPED(status)) {
        return (status & ~0xff00) |
               (host_to_target_signal(WSTOPSIG(status)) << 8);
    }
    return status;
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

static abi_long guest_host_stat(int fd, abi_ulong name_addr, int flags,
                                struct stat *st)
{
    if (!name_addr && flags != -1) {
        return -TARGET_EFAULT;
    }
    char *name = NULL;
    int ret;
    if (name_addr) {
        name = lock_user_string(name_addr);
        if (!name) {
            return -TARGET_EFAULT;
        }
        if (!*name && (flags & TARGET_AT_EMPTY_PATH)) {
            ret = fstat(fd, st);
        } else {
            ret = fstatat(
                host_dirfd(fd), path(name), st,
                flags & TARGET_AT_SYMLINK_NOFOLLOW ? AT_SYMLINK_NOFOLLOW : 0);
        }
        unlock_user(name, name_addr, 0);
    } else {
        ret = fstat(fd, st);
    }
    return get_errno(ret);
}

static uint64_t guest_device(dev_t device)
{
    uint64_t maj = major(device), min = minor(device);
    return ((maj & 0xfff) << 8) | ((maj & ~0xfffULL) << 32) |
           (min & 0xff) | ((min & ~0xffULL) << 12);
}

static abi_long guest_stat(int fd, abi_ulong name_addr, abi_ulong out,
                           int flags)
{
    struct stat st;
    struct target_stat dst = {0};
    abi_long ret = guest_host_stat(fd, name_addr, flags, &st);
    if (ret < 0) {
        return ret;
    }
    dst.st_nlink = tswapal(st.st_nlink);
    dst.st_mode = tswap32(st.st_mode);
    dst.st_uid = tswap32(st.st_uid);
    dst.st_gid = tswap32(st.st_gid);
    dst.st_dev = tswapal(guest_device(st.st_dev));
    dst.st_rdev = tswapal(guest_device(st.st_rdev));
    dst.st_ino = tswapal(st.st_ino);
    dst.st_size = tswapal(st.st_size);
    dst.st_blksize = tswapal(st.st_blksize);
    dst.st_blocks = tswapal(st.st_blocks);
    dst.target_st_atime = tswapal(st.st_atimespec.tv_sec);
    dst.target_st_atime_nsec = tswapal(st.st_atimespec.tv_nsec);
    dst.target_st_mtime = tswapal(st.st_mtimespec.tv_sec);
    dst.target_st_mtime_nsec = tswapal(st.st_mtimespec.tv_nsec);
    dst.target_st_ctime = tswapal(st.st_ctimespec.tv_sec);
    dst.target_st_ctime_nsec = tswapal(st.st_ctimespec.tv_nsec);
    return copy_to_user(out, &dst, sizeof(dst)) ? -TARGET_EFAULT : 0;
}

static abi_long guest_statx(int fd, abi_ulong name, int flags,
                           unsigned mask, abi_ulong addr)
{
    struct stat st;
    struct target_statx out = {0};
    abi_long ret;
    if ((flags & ~(TARGET_AT_SYMLINK_NOFOLLOW | TARGET_AT_EMPTY_PATH |
                   TARGET_AT_NO_AUTOMOUNT | TARGET_AT_STATX_SYNC_TYPE)) ||
        (flags & TARGET_AT_STATX_SYNC_TYPE) == TARGET_AT_STATX_SYNC_TYPE ||
        (mask & 0x80000000)) {
        return -TARGET_EINVAL;
    }
    ret = guest_host_stat(fd, name, flags, &st);
    if (ret) {
        return ret;
    }
    out.stx_mask = tswap32(0xfff); /* BASIC_STATS and BTIME */
    out.stx_blksize = tswap32(st.st_blksize);
    out.stx_nlink = tswap32(st.st_nlink);
    out.stx_uid = tswap32(st.st_uid);
    out.stx_gid = tswap32(st.st_gid);
    out.stx_mode = tswap16(st.st_mode);
    out.stx_ino = tswap64(st.st_ino);
    out.stx_size = tswap64(st.st_size);
    out.stx_blocks = tswap64(st.st_blocks);
#define COPY_STATX_TIME(field, host) \
    out.field.tv_sec = tswap64(st.host.tv_sec); \
    out.field.tv_nsec = tswap32(st.host.tv_nsec)
    COPY_STATX_TIME(stx_atime, st_atimespec);
    COPY_STATX_TIME(stx_btime, st_birthtimespec);
    COPY_STATX_TIME(stx_ctime, st_ctimespec);
    COPY_STATX_TIME(stx_mtime, st_mtimespec);
#undef COPY_STATX_TIME
    out.stx_rdev_major = tswap32(major(st.st_rdev));
    out.stx_rdev_minor = tswap32(minor(st.st_rdev));
    out.stx_dev_major = tswap32(major(st.st_dev));
    out.stx_dev_minor = tswap32(minor(st.st_dev));
    return copy_to_user(addr, &out, sizeof(out)) ? -TARGET_EFAULT : 0;
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

static abi_long guest_getdents(int fd, abi_ulong addr, unsigned count)
{
    g_autofree char *host = NULL;
    char *out;
    off_t base;
    ssize_t bytes;
    unsigned used = 0;
    if (count < 24) {
        return -TARGET_EINVAL;
    }
    count = MIN(count, 1024 * 1024);
    host = g_malloc(count);
    out = lock_user(VERIFY_WRITE, addr, count, 0);
    if (!out) {
        return -TARGET_EFAULT;
    }
    bytes = __getdirentries64(fd, host, count, &base);
    if (bytes < 0) {
        unlock_user(out, addr, 0);
        return get_errno(-1);
    }
    for (ssize_t off = 0; off < bytes;) {
        struct dirent *de = (struct dirent *)(host + off);
        unsigned n = strlen(de->d_name) + 1;
        unsigned len = ROUND_UP(19 + n, 8);
        if (!de->d_reclen || off + de->d_reclen > bytes || used + len > count) {
            unlock_user(out, addr, 0);
            return -TARGET_EIO;
        }
        memset(out + used, 0, len);
        stq_p(out + used, tswap64(de->d_ino));
        stq_p(out + used + 8, tswap64(de->d_seekoff));
        stw_p(out + used + 16, tswap16(len));
        out[used + 18] = de->d_type;
        memcpy(out + used + 19, de->d_name, n);
        used += len;
        off += de->d_reclen;
    }
    unlock_user(out, addr, used);
    return used;
}

static int host_clock(int id)
{
    switch (id) {
    case 0:
        return CLOCK_REALTIME;
    case 1:
    case 4:
    case 6:
    case 7:
        return CLOCK_MONOTONIC;
    case 2:
        return CLOCK_PROCESS_CPUTIME_ID;
    case 3:
        return CLOCK_THREAD_CPUTIME_ID;
    case 5:
        return CLOCK_REALTIME;
    default:
        return -1;
    }
}

static abi_long put_timespec(abi_ulong addr, const struct timespec *ts)
{
    struct target_timespec target = {.tv_sec = tswapal(ts->tv_sec),
                                     .tv_nsec = tswapal(ts->tv_nsec)};
    return copy_to_user(addr, &target, sizeof(target)) ? -TARGET_EFAULT : 0;
}

static abi_long get_timespec(struct timespec *ts, abi_ulong addr)
{
    struct target_timespec target;
    if (copy_from_user(&target, addr, sizeof(target))) {
        return -TARGET_EFAULT;
    }
    ts->tv_sec = (abi_long)tswapal(target.tv_sec);
    ts->tv_nsec = (abi_long)tswapal(target.tv_nsec);
    if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000) {
        return -TARGET_EINVAL;
    }
    return 0;
}

static abi_long guest_sigaction(int sig, abi_ulong act_addr, abi_ulong old_addr,
                                size_t size)
{
    struct target_sigaction *act = NULL, *old = NULL;
    abi_long ret;
    if (size != sizeof(target_sigset_t)) {
        return -TARGET_EINVAL;
    }
    if (act_addr) {
        act = lock_user(VERIFY_READ, act_addr, sizeof(*act), 1);
        if (!act) {
            return -TARGET_EFAULT;
        }
    }
    if (old_addr) {
        old = lock_user(VERIFY_WRITE, old_addr, sizeof(*old), 0);
        if (!old) {
            unlock_user(act, act_addr, 0);
            return -TARGET_EFAULT;
        }
    }
    ret = do_sigaction(sig, act, old, 0);
    unlock_user(act, act_addr, 0);
    unlock_user(old, old_addr, ret == 0 ? sizeof(*old) : 0);
    return ret;
}

static abi_long guest_sigmask(int how, abi_ulong set_addr, abi_ulong old_addr,
                              size_t size)
{
    target_sigset_t target;
    sigset_t set, old;
    int ret;
    if (size != sizeof(target)) {
        return -TARGET_EINVAL;
    }
    if (set_addr) {
        if (copy_from_user(&target, set_addr, size)) {
            return -TARGET_EFAULT;
        }
        target_to_host_sigset(&set, &target);
    }
    switch (how) {
    case TARGET_SIG_BLOCK:
        how = SIG_BLOCK;
        break;
    case TARGET_SIG_UNBLOCK:
        how = SIG_UNBLOCK;
        break;
    case TARGET_SIG_SETMASK:
        how = SIG_SETMASK;
        break;
    default:
        return -TARGET_EINVAL;
    }
    ret = do_sigprocmask(how, set_addr ? &set : NULL, &old);
    if (ret) {
        return get_errno(ret);
    }
    if (old_addr) {
        host_to_target_sigset(&target, &old);
        if (copy_to_user(old_addr, &target, size)) {
            return -TARGET_EFAULT;
        }
    }
    return 0;
}


static abi_long guest_exec(abi_ulong name_addr, abi_ulong argv_addr,
                           abi_ulong env_addr)
{
    g_autoptr(GPtrArray) args = g_ptr_array_new_with_free_func(g_free);
    g_autoptr(GPtrArray) envs = g_ptr_array_new_with_free_func(g_free);
    char *name = lock_user_string(name_addr);
    g_autofree char *executable = NULL;
    char self[PATH_MAX];
    uint32_t self_size = sizeof(self);
    unsigned char magic[256];
    int fd, ret;
    if (!name) {
        return -TARGET_EFAULT;
    }
    executable = g_strdup(path(name));
    unlock_user(name, name_addr, 0);
    for (int list = 0; list < 2; list++) {
        abi_ulong at = list ? env_addr : argv_addr;
        GPtrArray *array = list ? envs : args;
        for (unsigned i = 0; at; i++, at += sizeof(abi_ulong)) {
            abi_ulong ptr;
            char *str;
            if (i >= 131072) {
                return -TARGET_E2BIG;
            }
            if (get_user_ual(ptr, at)) {
                return -TARGET_EFAULT;
            }
            if (!ptr) {
                break;
            }
            str = lock_user_string(ptr);
            if (!str) {
                return -TARGET_EFAULT;
            }
            g_ptr_array_add(array, g_strdup(str));
            unlock_user(str, ptr, 0);
        }
    }
    g_ptr_array_add(envs, NULL);
    if (access(executable, X_OK)) {
        return get_errno(-1);
    }
    fd = open(executable, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return get_errno(-1);
    }
    ret = read(fd, magic, sizeof(magic));
    close(fd);
    if (ret >= 2 && magic[0] == '#' && magic[1] == '!') {
        char *line = (char *)magic + 2;
        char *end = memchr(magic, '\n', ret);
        if (!end) {
            if (ret == sizeof(magic)) {
                return -TARGET_ENOEXEC;
            }
            end = (char *)magic + ret;
        }
        *end = 0;
        while (end > line && (end[-1] == ' ' || end[-1] == '\t')) {
            *--end = 0;
        }
        while (*line == ' ' || *line == '\t') {
            line++;
        }
        char *opt = line;
        while (*opt && *opt != ' ' && *opt != '\t') {
            opt++;
        }
        if (*opt) {
            *opt++ = 0;
        }
        while (*opt == ' ' || *opt == '\t') {
            opt++;
        }
        if (!*line) {
            return -TARGET_ENOEXEC;
        }
        if (args->len) {
            g_ptr_array_remove_index(args, 0);
        }
        g_ptr_array_insert(args, 0, g_strdup(executable));
        if (*opt) {
            g_ptr_array_insert(args, 0, g_strdup(opt));
        }
        g_ptr_array_insert(args, 0, g_strdup(line));
        char *interpreter = g_strdup(path(line));
        g_free(executable);
        executable = interpreter;
        if (access(executable, X_OK)) {
            return get_errno(-1);
        }
        fd = open(executable, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            return get_errno(-1);
        }
        ret = read(fd, magic, sizeof(magic));
        close(fd);
    }
    if (ret >= 4 && !memcmp(magic, ELFMAG, 4)) {
        if (_NSGetExecutablePath(self, &self_size)) {
            return -TARGET_ENAMETOOLONG;
        }
        /* Preserve argv[0] while recursively entering the same guest ABI. */
        char *argv0 =
            args->len ? g_strdup(args->pdata[0]) : g_strdup(executable);
        if (args->len) {
            g_ptr_array_remove_index(args, 0);
        }
        g_ptr_array_insert(args, 0, g_strdup(executable));
        g_ptr_array_insert(args, 0, argv0);
        g_ptr_array_insert(args, 0, g_strdup("-0"));
        if (qemu_loglevel_mask(LOG_STRACE)) {
            g_ptr_array_insert(args, 0, g_strdup("-strace"));
        }
        g_ptr_array_insert(args, 0, g_strdup(interp_prefix));
        g_ptr_array_insert(args, 0, g_strdup("-L"));
        g_ptr_array_insert(args, 0, g_strdup(self));
        g_ptr_array_add(args, NULL);
        execve(self, (char **)args->pdata, (char **)envs->pdata);
    } else {
        g_ptr_array_add(args, NULL);
        execve(executable, (char **)args->pdata, (char **)envs->pdata);
    }
    return get_errno(-1);
}

#include "futex.c.inc"
#include "thread.c.inc"

static abi_long guest_exit(CPUArchState *env, int code)
{
    CPUState *cpu = env_cpu(env);
    if (block_signals()) {
        return -QEMU_ERESTARTSYS;
    }
    pthread_mutex_lock(&clone_lock);
    if (CPU_NEXT(first_cpu)) {
        TaskState *ts = get_task_state(cpu);
        if (ts->child_tidptr) {
            put_user_u32(0, ts->child_tidptr);
            futex_wake(ts->child_tidptr, INT_MAX, UINT32_MAX);
        }
        object_unparent(OBJECT(cpu));
        object_unref(OBJECT(cpu));
        pthread_mutex_unlock(&clone_lock);
        thread_cpu = NULL;
        g_free(ts);
        rcu_unregister_thread();
        pthread_exit(NULL);
    }
    pthread_mutex_unlock(&clone_lock);
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

static abi_long guest_poll(abi_ulong addr, unsigned count, int timeout)
{
    struct target_pollfd {
        int32_t fd;
        int16_t events, revents;
    } *target;
    struct pollfd *host;
    abi_long ret;
    if (count > 65536) {
        return -TARGET_EINVAL;
    }
    target = lock_user(VERIFY_WRITE, addr, count * sizeof(*target), 1);
    if (!target && count) {
        return -TARGET_EFAULT;
    }
    host = g_new0(struct pollfd, count);
    for (unsigned i = 0; i < count; i++) {
        host[i].fd = (int32_t)tswap32(target[i].fd);
        host[i].events = tswap16(target[i].events) & 0x3f;
    }
    ret = get_errno(poll(host, count, timeout));
    if (ret >= 0) {
        for (unsigned i = 0; i < count; i++) {
            target[i].revents = tswap16(host[i].revents);
        }
    }
    g_free(host);
    unlock_user(target, addr, ret >= 0 ? count * sizeof(*target) : 0);
    return ret;
}

static abi_long guest_rlimit(int resource, abi_ulong new_addr,
                             abi_ulong old_addr)
{
    static const int map[] = {
        RLIMIT_CPU, RLIMIT_FSIZE, RLIMIT_DATA,   RLIMIT_STACK,   RLIMIT_CORE,
        RLIMIT_RSS, RLIMIT_NPROC, RLIMIT_NOFILE, RLIMIT_MEMLOCK, RLIMIT_AS};
    struct rlimit limit;
    uint64_t target[2];
    if ((unsigned)resource >= ARRAY_SIZE(map)) {
        return -TARGET_EINVAL;
    }
    if (getrlimit(map[resource], &limit)) {
        return get_errno(-1);
    }
    if (old_addr) {
        target[0] = tswap64(limit.rlim_cur == RLIM_INFINITY ? UINT64_MAX
                                                            : limit.rlim_cur);
        target[1] = tswap64(limit.rlim_max == RLIM_INFINITY ? UINT64_MAX
                                                            : limit.rlim_max);
        if (copy_to_user(old_addr, target, sizeof(target))) {
            return -TARGET_EFAULT;
        }
    }
    if (new_addr) {
        if (copy_from_user(target, new_addr, sizeof(target))) {
            return -TARGET_EFAULT;
        }
        limit.rlim_cur = tswap64(target[0]);
        limit.rlim_max = tswap64(target[1]);
        if (limit.rlim_cur == UINT64_MAX) {
            limit.rlim_cur = RLIM_INFINITY;
        }
        if (limit.rlim_max == UINT64_MAX) {
            limit.rlim_max = RLIM_INFINITY;
        }
        return get_errno(setrlimit(map[resource], &limit));
    }
    return 0;
}

static abi_long guest_rusage(int who, abi_ulong addr)
{
    struct rusage usage;
    struct target_rusage out = {0};
    int host = who == 0 ? RUSAGE_SELF : who == -1 ? RUSAGE_CHILDREN : -99;
    if (host == -99) {
        return -TARGET_EINVAL;
    }
    if (getrusage(host, &usage)) {
        return get_errno(-1);
    }
    out.ru_utime.tv_sec = tswapal(usage.ru_utime.tv_sec);
    out.ru_utime.tv_usec = tswapal(usage.ru_utime.tv_usec);
    out.ru_stime.tv_sec = tswapal(usage.ru_stime.tv_sec);
    out.ru_stime.tv_usec = tswapal(usage.ru_stime.tv_usec);
#define COPY_RUSAGE(field) out.field = tswapal(usage.field)
    out.ru_maxrss = tswapal(usage.ru_maxrss / 1024);
    COPY_RUSAGE(ru_ixrss);
    COPY_RUSAGE(ru_idrss);
    COPY_RUSAGE(ru_isrss);
    COPY_RUSAGE(ru_minflt);
    COPY_RUSAGE(ru_majflt);
    COPY_RUSAGE(ru_nswap);
    COPY_RUSAGE(ru_inblock);
    COPY_RUSAGE(ru_oublock);
    COPY_RUSAGE(ru_msgsnd);
    COPY_RUSAGE(ru_msgrcv);
    COPY_RUSAGE(ru_nsignals);
    COPY_RUSAGE(ru_nvcsw);
    COPY_RUSAGE(ru_nivcsw);
#undef COPY_RUSAGE
    return copy_to_user(addr, &out, sizeof(out)) ? -TARGET_EFAULT : 0;
}

#include "socket.c.inc"

static abi_long guest_syscall(CPUArchState *env, int num, abi_long a1,
                              abi_long a2, abi_long a3, abi_long a4,
                              abi_long a5, abi_long a6)
{
    TaskState *ts = get_task_state(env_cpu(env));
    void *p;
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
