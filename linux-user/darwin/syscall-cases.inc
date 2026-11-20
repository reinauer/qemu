/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef TARGET_NR_statx
case TARGET_NR_statx:
    return guest_statx(a1, a2, a3, a4, a5);
#endif
#ifdef TARGET_NR_exit
case TARGET_NR_exit: {
    return guest_exit(env, a1);
}
#endif
#ifdef TARGET_NR_exit_group
case TARGET_NR_exit_group: {
    preexit_cleanup(env, a1);
    _exit(a1);
}
#endif
#ifdef TARGET_NR_read
case TARGET_NR_read: {
    return guest_io(a1, a2, a3, false, false, 0);
}
#endif
#ifdef TARGET_NR_write
case TARGET_NR_write: {
    return guest_io(a1, a2, a3, true, false, 0);
}
#endif
#ifdef TARGET_NR_pread64
case TARGET_NR_pread64: {
#ifdef TARGET_ABI32
    return guest_io(a1, a2, a3, false, true,
                    (uint64_t)(uint32_t)a4 | ((uint64_t)(uint32_t)a5 << 32));
#else
    return guest_io(a1, a2, a3, false, true, a4);
#endif
}
#endif
#ifdef TARGET_NR_pwrite64
case TARGET_NR_pwrite64: {
#ifdef TARGET_ABI32
    return guest_io(a1, a2, a3, true, true,
                    (uint64_t)(uint32_t)a4 | ((uint64_t)(uint32_t)a5 << 32));
#else
    return guest_io(a1, a2, a3, true, true, a4);
#endif
}
#endif
#ifdef TARGET_NR_readv
case TARGET_NR_readv: {
    return guest_iov(a1, a2, a3, false);
}
#endif
#ifdef TARGET_NR_writev
case TARGET_NR_writev: {
    return guest_iov(a1, a2, a3, true);
}
#endif
#ifdef TARGET_NR_open
case TARGET_NR_open: {
    return guest_open(env, TARGET_AT_FDCWD, a1, a2, a3);
}
#endif
#ifdef TARGET_NR_openat
case TARGET_NR_openat: {
    return guest_open(env, a1, a2, a3, a4);
}
#endif
#ifdef TARGET_NR_close
case TARGET_NR_close: {
    return get_errno(close(a1));
}
#endif
#ifdef TARGET_NR_lseek
case TARGET_NR_lseek: {
    return get_errno(lseek(a1, a2, a3));
}
#endif
#ifdef TARGET_NR_fcntl
case TARGET_NR_fcntl: {
    return guest_fcntl(a1, a2, a3);
}
#endif
#ifdef TARGET_NR_dup
case TARGET_NR_dup: {
    return get_errno(dup(a1));
}
#endif
#ifdef TARGET_NR_dup2
case TARGET_NR_dup2: {
    return get_errno(dup2(a1, a2));
}
#endif
#ifdef TARGET_NR_dup3
case TARGET_NR_dup3: {
    if (a1 == a2 || (a3 & ~TARGET_O_CLOEXEC)) {
        return -TARGET_EINVAL;
    }
    ret = get_errno(dup2(a1, a2));
    if (ret >= 0 && (a3 & TARGET_O_CLOEXEC)) {
        fcntl(ret, F_SETFD, FD_CLOEXEC);
    }
    return ret;
}
#endif
#ifdef TARGET_NR_pipe
case TARGET_NR_pipe: {
    int fds[2];
    int flags = num == TARGET_NR_pipe2 ? a2 : 0;
    if (flags & ~(TARGET_O_CLOEXEC | TARGET_O_NONBLOCK)) {
        return -TARGET_EINVAL;
    }
    if (pipe(fds)) {
        return get_errno(-1);
    }
    for (int i = 0; i < 2; i++) {
        if (flags & TARGET_O_CLOEXEC) {
            fcntl(fds[i], F_SETFD, FD_CLOEXEC);
        }
        if (flags & TARGET_O_NONBLOCK) {
            fcntl(fds[i], F_SETFL, O_NONBLOCK);
        }
    }
    if (put_user_u32(fds[0], a1) || put_user_u32(fds[1], a1 + 4)) {
        close(fds[0]);
        close(fds[1]);
        return -TARGET_EFAULT;
    }
    return 0;
}
#endif
#ifdef TARGET_NR_pipe2
case TARGET_NR_pipe2: {
    int fds[2];
    int flags = num == TARGET_NR_pipe2 ? a2 : 0;
    if (flags & ~(TARGET_O_CLOEXEC | TARGET_O_NONBLOCK)) {
        return -TARGET_EINVAL;
    }
    if (pipe(fds)) {
        return get_errno(-1);
    }
    for (int i = 0; i < 2; i++) {
        if (flags & TARGET_O_CLOEXEC) {
            fcntl(fds[i], F_SETFD, FD_CLOEXEC);
        }
        if (flags & TARGET_O_NONBLOCK) {
            fcntl(fds[i], F_SETFL, O_NONBLOCK);
        }
    }
    if (put_user_u32(fds[0], a1) || put_user_u32(fds[1], a1 + 4)) {
        close(fds[0]);
        close(fds[1]);
        return -TARGET_EFAULT;
    }
    return 0;
}
#endif
#ifdef TARGET_NR_fstat
case TARGET_NR_fstat: {
    return guest_stat(a1, 0, a2, -1, false);
}
#endif
#ifdef TARGET_NR_newfstatat
case TARGET_NR_newfstatat: {
    return guest_stat(a1, a2, a3, a4, sizeof(abi_ulong) == 4);
}
#endif
#ifdef TARGET_NR_fstatat64
case TARGET_NR_fstatat64: {
    return guest_stat(a1, a2, a3, a4, sizeof(abi_ulong) == 4);
}
#endif
#ifdef TARGET_NR_stat
case TARGET_NR_stat: {
    return guest_stat(TARGET_AT_FDCWD, a1, a2, 0, false);
}
#endif
#ifdef TARGET_NR_lstat
case TARGET_NR_lstat: {
    return guest_stat(TARGET_AT_FDCWD, a1, a2, TARGET_AT_SYMLINK_NOFOLLOW, false);
}
#endif
#ifdef TARGET_NR_getdents64
case TARGET_NR_getdents64: {
    return guest_getdents(a1, a2, a3);
}
#endif
#ifdef TARGET_NR_brk
case TARGET_NR_brk: {
    return do_brk(a1);
}
#endif
#ifdef TARGET_NR_mmap
case TARGET_NR_mmap: {
#ifdef TARGET_ABI32
    abi_ulong args[6];
    if (copy_from_user(args, a1, sizeof(args))) {
        return -TARGET_EFAULT;
    }
    a1 = tswapal(args[0]);
    a2 = tswapal(args[1]);
    a3 = tswapal(args[2]);
    a4 = tswapal(args[3]);
    a5 = tswapal(args[4]);
    a6 = tswapal(args[5]);
#endif
    int flags = 0;
    if (a4 & TARGET_MAP_SHARED) {
        flags |= MAP_SHARED;
    }
    if (a4 & TARGET_MAP_PRIVATE) {
        flags |= MAP_PRIVATE;
    }
    if (a4 & TARGET_MAP_ANONYMOUS) {
        flags |= MAP_ANON;
    }
    if (a4 & TARGET_MAP_FIXED) {
        flags |= MAP_FIXED;
    }
    if (a4 & TARGET_MAP_FIXED_NOREPLACE) {
        flags |= MAP_FIXED_NOREPLACE;
    }
    if (!(flags & (MAP_SHARED | MAP_PRIVATE))) {
        return -TARGET_EINVAL;
    }
    return get_errno(target_mmap(a1, a2, a3, flags, a5, a6));
}
#endif
#ifdef TARGET_NR_munmap
case TARGET_NR_munmap: {
    return get_errno(target_munmap(a1, a2));
}
#endif
#ifdef TARGET_NR_mprotect
case TARGET_NR_mprotect: {
    return get_errno(target_mprotect(a1, a2, a3));
}
#endif
#ifdef TARGET_NR_mremap
case TARGET_NR_mremap: {
    return get_errno(target_mremap(a1, a2, a3, a4, a5));
}
#endif
#ifdef TARGET_NR_madvise
case TARGET_NR_madvise: {
    return target_madvise(a1, a2, a3);
}
#endif
#ifdef TARGET_NR_msync
case TARGET_NR_msync: {
    int flags = 0;
    if (a3 & 1) {
        flags |= MS_ASYNC;
    }
    if (a3 & 2) {
        flags |= MS_INVALIDATE;
    }
    if (a3 & 4) {
        flags |= MS_SYNC;
    }
    return get_errno(msync(g2h_untagged(a1), a2, flags));
}
#endif
#ifdef TARGET_NR_getpid
case TARGET_NR_getpid: {
    return get_errno(getpid());
}
#endif
#ifdef TARGET_NR_getppid
case TARGET_NR_getppid: {
    return get_errno(getppid());
}
#endif
#ifdef TARGET_NR_getuid
case TARGET_NR_getuid: {
    return get_errno(getuid());
}
#endif
#ifdef TARGET_NR_geteuid
case TARGET_NR_geteuid: {
    return get_errno(geteuid());
}
#endif
#ifdef TARGET_NR_getgid
case TARGET_NR_getgid: {
    return get_errno(getgid());
}
#endif
#ifdef TARGET_NR_getegid
case TARGET_NR_getegid: {
    return get_errno(getegid());
}
#endif
/* Support posix_spawn's credential reset without emulating privilege changes. */
#ifdef TARGET_NR_setresuid
case TARGET_NR_setresuid: {
    if (((uid_t)a1 != (uid_t)-1 && (uid_t)a1 != getuid()) ||
        ((uid_t)a2 != (uid_t)-1 && (uid_t)a2 != geteuid()) ||
        (uid_t)a3 != (uid_t)-1) {
        return -TARGET_EPERM;
    }
    return 0;
}
#endif
#ifdef TARGET_NR_setresgid
case TARGET_NR_setresgid: {
    if (((gid_t)a1 != (gid_t)-1 && (gid_t)a1 != getgid()) ||
        ((gid_t)a2 != (gid_t)-1 && (gid_t)a2 != getegid()) ||
        (gid_t)a3 != (gid_t)-1) {
        return -TARGET_EPERM;
    }
    return 0;
}
#endif
#ifdef TARGET_NR_getpgrp
case TARGET_NR_getpgrp: {
    return get_errno(getpgrp());
}
#endif
#ifdef TARGET_NR_setsid
case TARGET_NR_setsid: {
    return get_errno(setsid());
}
#endif
#ifdef TARGET_NR_getpgid
case TARGET_NR_getpgid: {
    return get_errno(getpgid(a1));
}
#endif
#ifdef TARGET_NR_getsid
case TARGET_NR_getsid: {
    return get_errno(getsid(a1));
}
#endif
#ifdef TARGET_NR_setpgid
case TARGET_NR_setpgid: {
    return get_errno(setpgid(a1, a2));
}
#endif
#ifdef TARGET_NR_fsync
case TARGET_NR_fsync: {
    return get_errno(fsync(a1));
}
#endif
#ifdef TARGET_NR_fdatasync
case TARGET_NR_fdatasync: {
    return get_errno(fsync(a1));
}
#endif
#ifdef TARGET_NR_ftruncate
case TARGET_NR_ftruncate: {
    return get_errno(ftruncate(a1, a2));
}
#endif
#ifdef TARGET_NR_fchmod
case TARGET_NR_fchmod: {
    return get_errno(fchmod(a1, a2));
}
#endif
#ifdef TARGET_NR_fchown
case TARGET_NR_fchown: {
    return get_errno(fchown(a1, a2, a3));
}
#endif
#ifdef TARGET_NR_fchdir
case TARGET_NR_fchdir: {
    return get_errno(fchdir(a1));
}
#endif
#ifdef TARGET_NR_umask
case TARGET_NR_umask: {
    return get_errno(umask(a1));
}
#endif
#ifdef TARGET_NR_sched_yield
case TARGET_NR_sched_yield: {
    return get_errno(sched_yield());
}
#endif
#ifdef TARGET_NR_gettid
case TARGET_NR_gettid: {
    return ts->ts_tid;
}
#endif
#ifdef TARGET_NR_set_tid_address
case TARGET_NR_set_tid_address: {
    ts->child_tidptr = a1;
    return ts->ts_tid;
}
#endif
#ifdef TARGET_NR_arch_prctl
case TARGET_NR_arch_prctl: {
#ifdef TARGET_X86_64
    return do_arch_prctl(env, a1, a2);
#else
    return -TARGET_EINVAL;
#endif
}
#endif
#ifdef TARGET_NR_uname
case TARGET_NR_uname: {
    struct {
        char sys[65], node[65], release[65], version[65], machine[65],
            domain[65];
    } out = {0};
    g_strlcpy(out.sys, "Linux", sizeof(out.sys));
    gethostname(out.node, sizeof(out.node));
    g_strlcpy(out.release, qemu_uname_release, sizeof(out.release));
    g_strlcpy(out.version, "QEMU linux-user on Darwin", sizeof(out.version));
    g_strlcpy(out.machine, TARGET_NAME, sizeof(out.machine));
    return copy_to_user(a1, &out, sizeof(out)) ? -TARGET_EFAULT : 0;
}
#endif
#ifdef TARGET_NR_getcwd
case TARGET_NR_getcwd: {
    if (!a2) {
        return -TARGET_ERANGE;
    }
    p = lock_user(VERIFY_WRITE, a1, a2, 0);
    if (!p) {
        return -TARGET_EFAULT;
    }
    ret = getcwd(p, a2) ? strlen(p) + 1 : get_errno(-1);
    unlock_user(p, a1, ret > 0 ? ret : 0);
    return ret;
}
#endif
#ifdef TARGET_NR_chdir
case TARGET_NR_chdir: {

    p = lock_user_string(a1);
    if (!p) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(chdir(path(p)));
    unlock_user(p, a1, 0);
    return ret;
}
#endif
#ifdef TARGET_NR_unlink
case TARGET_NR_unlink: {

    p = lock_user_string(a1);
    if (!p) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(unlink(path(p)));
    unlock_user(p, a1, 0);
    return ret;
}
#endif
#ifdef TARGET_NR_mkdir
case TARGET_NR_mkdir: {

    p = lock_user_string(a1);
    if (!p) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(mkdir(path(p), a2));
    unlock_user(p, a1, 0);
    return ret;
}
#endif
#ifdef TARGET_NR_rmdir
case TARGET_NR_rmdir: {

    p = lock_user_string(a1);
    if (!p) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(rmdir(path(p)));
    unlock_user(p, a1, 0);
    return ret;
}
#endif
#ifdef TARGET_NR_chmod
case TARGET_NR_chmod: {

    p = lock_user_string(a1);
    if (!p) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(chmod(path(p), a2));
    unlock_user(p, a1, 0);
    return ret;
}
#endif
#ifdef TARGET_NR_chown
case TARGET_NR_chown: {

    p = lock_user_string(a1);
    if (!p) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(chown(path(p), a2, a3));
    unlock_user(p, a1, 0);
    return ret;
}
#endif
#ifdef TARGET_NR_lchown
case TARGET_NR_lchown: {

    p = lock_user_string(a1);
    if (!p) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(lchown(path(p), a2, a3));
    unlock_user(p, a1, 0);
    return ret;
}
#endif
#ifdef TARGET_NR_access
case TARGET_NR_access: {

    p = lock_user_string(a1);
    if (!p) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(access(path(p), a2));
    unlock_user(p, a1, 0);
    return ret;
}
#endif
#ifdef TARGET_NR_truncate
case TARGET_NR_truncate: {

    p = lock_user_string(a1);
    if (!p) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(truncate(path(p), a2));
    unlock_user(p, a1, 0);
    return ret;
}
#endif
#ifdef TARGET_NR_faccessat
case TARGET_NR_faccessat: {
    p = lock_user_string(a2);
    if (!p) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(faccessat(
        host_dirfd(a1), path(p), a3,
        (a4 & TARGET_AT_EACCESS ? AT_EACCESS : 0) |
            (a4 & TARGET_AT_SYMLINK_NOFOLLOW ? AT_SYMLINK_NOFOLLOW : 0)));
    unlock_user(p, a2, 0);
    return ret;
}
#endif
#ifdef TARGET_NR_faccessat2
case TARGET_NR_faccessat2: {
    p = lock_user_string(a2);
    if (!p) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(faccessat(
        host_dirfd(a1), path(p), a3,
        (a4 & TARGET_AT_EACCESS ? AT_EACCESS : 0) |
            (a4 & TARGET_AT_SYMLINK_NOFOLLOW ? AT_SYMLINK_NOFOLLOW : 0)));
    unlock_user(p, a2, 0);
    return ret;
}
#endif
#ifdef TARGET_NR_unlinkat
case TARGET_NR_unlinkat: {
    p = lock_user_string(a2);
    if (!p) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(num == TARGET_NR_unlinkat
                        ? unlinkat(host_dirfd(a1), path(p),
                                   a3 & TARGET_AT_REMOVEDIR ? AT_REMOVEDIR : 0)
                        : mkdirat(host_dirfd(a1), path(p), a3));
    unlock_user(p, a2, 0);
    return ret;
}
#endif
#ifdef TARGET_NR_mkdirat
case TARGET_NR_mkdirat: {
    p = lock_user_string(a2);
    if (!p) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(num == TARGET_NR_unlinkat
                        ? unlinkat(host_dirfd(a1), path(p),
                                   a3 & TARGET_AT_REMOVEDIR ? AT_REMOVEDIR : 0)
                        : mkdirat(host_dirfd(a1), path(p), a3));
    unlock_user(p, a2, 0);
    return ret;
}
#endif
#ifdef TARGET_NR_readlink
case TARGET_NR_readlink: {
    p = lock_user_string(a1);
    if (!p) {
        return -TARGET_EFAULT;
    }
    void *out = lock_user(VERIFY_WRITE, a2, a3, 0);
    if (!out) {
        unlock_user(p, a1, 0);
        return -TARGET_EFAULT;
    }
    ret = get_errno(do_guest_readlink(p, out, a3));
    unlock_user(out, a2, ret > 0 ? ret : 0);
    unlock_user(p, a1, 0);
    return ret;
}
#endif
#ifdef TARGET_NR_readlinkat
case TARGET_NR_readlinkat: {
    p = lock_user_string(a2);
    if (!p) {
        return -TARGET_EFAULT;
    }
    void *out = lock_user(VERIFY_WRITE, a3, a4, 0);
    if (!out) {
        unlock_user(p, a2, 0);
        return -TARGET_EFAULT;
    }
    ret = get_errno(is_self_proc_entry(p, "exe")
                        ? do_guest_readlink(p, out, a4)
                        : readlinkat(host_dirfd(a1), path(p), out, a4));
    unlock_user(out, a3, ret > 0 ? ret : 0);
    unlock_user(p, a2, 0);
    return ret;
}
#endif
#ifdef TARGET_NR_clock_gettime
case TARGET_NR_clock_gettime: {
    struct timespec out;
    int clock = host_clock(a1);
    if (clock < 0) {
        return -TARGET_EINVAL;
    }
    if (!a2) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(num == TARGET_NR_clock_gettime ? clock_gettime(clock, &out)
                                                   : clock_getres(clock, &out));
    return ret ? ret : (a2 ? put_timespec(a2, &out) : 0);
}
#endif
#ifdef TARGET_NR_clock_getres
case TARGET_NR_clock_getres: {
    struct timespec out;
    int clock = host_clock(a1);
    if (clock < 0) {
        return -TARGET_EINVAL;
    }
    ret = get_errno(num == TARGET_NR_clock_gettime ? clock_gettime(clock, &out)
                                                   : clock_getres(clock, &out));
    return ret ? ret : (a2 ? put_timespec(a2, &out) : 0);
}
#endif
#ifdef TARGET_NR_gettimeofday
case TARGET_NR_gettimeofday: {
    struct timeval now;
    struct target_timeval out;
    gettimeofday(&now, NULL);
    out.tv_sec = tswapal(now.tv_sec);
    out.tv_usec = tswapal(now.tv_usec);
    if (a1 && copy_to_user(a1, &out, sizeof(out))) {
        return -TARGET_EFAULT;
    }
    if (a2) {
        uint64_t zero = 0;
        if (copy_to_user(a2, &zero, 8)) {
            return -TARGET_EFAULT;
        }
    }
    return 0;
}
#endif
#ifdef TARGET_NR_time
case TARGET_NR_time: {
    time_t now = time(NULL);
    if (a1 && put_user_ual(now, a1)) {
        return -TARGET_EFAULT;
    }
    return now;
}
#endif
#ifdef TARGET_NR_nanosleep
case TARGET_NR_nanosleep: {
    struct timespec req, rem;
    ret = get_timespec(&req, a1);
    if (ret) {
        return ret;
    }
    ret = get_errno(nanosleep(&req, &rem));
    if (ret == -TARGET_EINTR && a2 && put_timespec(a2, &rem)) {
        return -TARGET_EFAULT;
    }
    return ret;
}
#endif
#ifdef TARGET_NR_clock_nanosleep
case TARGET_NR_clock_nanosleep: {
    struct timespec req, rem, now;
    int clock = host_clock(a1);
    if (clock < 0 || (a2 & ~1)) {
        return -TARGET_EINVAL;
    }
    ret = get_timespec(&req, a3);
    if (ret) {
        return ret;
    }
    if (a2 & 1) {
        clock_gettime(clock, &now);
        req.tv_sec -= now.tv_sec;
        req.tv_nsec -= now.tv_nsec;
        if (req.tv_nsec < 0) {
            req.tv_nsec += 1000000000;
            req.tv_sec--;
        }
        if (req.tv_sec < 0) {
            return 0;
        }
    }
    ret = get_errno(nanosleep(&req, &rem));
    if (ret == -TARGET_EINTR && !(a2 & 1) && a4 && put_timespec(a4, &rem)) {
        return -TARGET_EFAULT;
    }
    return ret;
}
#endif
#ifdef TARGET_NR_getrandom
case TARGET_NR_getrandom: {
    if (a3 & ~3) {
        return -TARGET_EINVAL;
    }
    if (a2 < 0) {
        return -TARGET_EINVAL;
    }
    p = lock_user(VERIFY_WRITE, a1, a2, 0);
    if (!p && a2) {
        return -TARGET_EFAULT;
    }
    arc4random_buf(p, a2);
    unlock_user(p, a1, a2);
    return a2;
}
#endif
#ifdef TARGET_NR_rt_sigaction
case TARGET_NR_rt_sigaction: {
    return guest_sigaction(a1, a2, a3, a4);
}
#endif
#ifdef TARGET_NR_rt_sigprocmask
case TARGET_NR_rt_sigprocmask: {
    return guest_sigmask(a1, a2, a3, a4);
}
#endif
#ifdef TARGET_NR_rt_sigreturn
case TARGET_NR_rt_sigreturn: {
    return do_rt_sigreturn(env);
}
#endif
#ifdef TARGET_NR_sigaltstack
case TARGET_NR_sigaltstack: {
    return do_sigaltstack(a1, a2, env);
}
#endif
#ifdef TARGET_NR_kill
case TARGET_NR_kill: {
    int sig = target_to_host_signal(a2);
    if (sig < 0 || sig >= NSIG) {
        return -TARGET_EINVAL;
    }
    return get_errno(kill(a1, sig));
}
#endif
#ifdef TARGET_NR_fork
case TARGET_NR_fork: {
    return get_errno(do_fork(env, TARGET_SIGCHLD, 0, 0, 0, 0));
}
#endif
#ifdef TARGET_NR_vfork
case TARGET_NR_vfork: {
    return get_errno(do_fork(env, TARGET_SIGCHLD, 0, 0, 0, 0));
}
#endif
#ifdef TARGET_NR_clone
case TARGET_NR_clone: {
#ifdef TARGET_CLONE_BACKWARDS
    return get_errno(do_fork(env, a1, a2, a3, a4, a5));
#else
    return get_errno(do_fork(env, a1, a2, a3, a5, a4));
#endif
}
#endif
#ifdef TARGET_NR_execve
case TARGET_NR_execve: {
    return guest_exec(a1, a2, a3);
}
#endif
#ifdef TARGET_NR_wait4
case TARGET_NR_wait4: {
    int status;
    struct rusage usage;
    int options = 0;
    if (a3 & 1) {
        options |= WNOHANG;
    }
    if (a3 & 2) {
        options |= WUNTRACED;
    }
    if (a3 & 8) {
        options |= WCONTINUED;
    }
    ret = get_errno(wait4(a1, &status, options, &usage));
    if (ret > 0 && a2 && put_user_u32(host_to_target_waitstatus(status), a2)) {
        return -TARGET_EFAULT;
    }
    if (ret > 0 && a4) {
        struct target_rusage out = {0};
        out.ru_utime.tv_sec = tswapal(usage.ru_utime.tv_sec);
        out.ru_utime.tv_usec = tswapal(usage.ru_utime.tv_usec);
        out.ru_stime.tv_sec = tswapal(usage.ru_stime.tv_sec);
        out.ru_stime.tv_usec = tswapal(usage.ru_stime.tv_usec);
        if (copy_to_user(a4, &out, sizeof(out))) {
            return -TARGET_EFAULT;
        }
    }
    return ret;
}
#endif
#ifdef TARGET_NR_futex
case TARGET_NR_futex: {
    return guest_futex(a1, a2, a3, a4, a6, false);
}
#endif
#ifdef TARGET_NR_ioctl
case TARGET_NR_ioctl: {
    if (a2 == TARGET_TIOCGWINSZ || a2 == TARGET_TIOCSWINSZ) {
        struct winsize ws;
        uint16_t target[4];
        if (a2 == TARGET_TIOCSWINSZ) {
            if (copy_from_user(target, a3, sizeof(target))) {
                return -TARGET_EFAULT;
            }
            ws.ws_row = tswap16(target[0]);
            ws.ws_col = tswap16(target[1]);
            ws.ws_xpixel = tswap16(target[2]);
            ws.ws_ypixel = tswap16(target[3]);
            return get_errno(ioctl(a1, TIOCSWINSZ, &ws));
        }
        if (ioctl(a1, TIOCGWINSZ, &ws)) {
            return get_errno(-1);
        }
        target[0] = tswap16(ws.ws_row);
        target[1] = tswap16(ws.ws_col);
        target[2] = tswap16(ws.ws_xpixel);
        target[3] = tswap16(ws.ws_ypixel);
        return copy_to_user(a3, target, sizeof(target)) ? -TARGET_EFAULT : 0;
    }
    if (a2 == TARGET_FIONREAD) {
        int n;
        if (ioctl(a1, FIONREAD, &n)) {
            return get_errno(-1);
        }
        return put_user_u32(n, a3);
    }
    return -TARGET_ENOTTY;
}
#endif
#ifdef TARGET_NR_pselect6
case TARGET_NR_pselect6: {
    return guest_pselect6(a1, a2, a3, a4, a5, a6);
}
#endif
#ifdef TARGET_NR_poll
case TARGET_NR_poll: {
    return guest_poll(a1, a2, a3);
}
#endif
#ifdef TARGET_NR_ppoll
case TARGET_NR_ppoll: {
    struct timespec timeout;
    int ms = -1;
    if (a3) {
        ret = get_timespec(&timeout, a3);
        if (ret) {
            return ret;
        }
        ms = MIN((uint64_t)timeout.tv_sec * 1000 +
                     (timeout.tv_nsec + 999999) / 1000000,
                 INT_MAX);
    }
    if (a4) {
        return -TARGET_ENOSYS;
    }
    return guest_poll(a1, a2, ms);
}
#endif
#ifdef TARGET_NR_tgkill
case TARGET_NR_tgkill: {
    abi_long tid = num == TARGET_NR_tgkill ? a2 : a1;
    int sig = target_to_host_signal(num == TARGET_NR_tgkill ? a3 : a2);
    if (sig < 0 || sig >= NSIG) {
        return -TARGET_EINVAL;
    }
    if (num == TARGET_NR_tgkill && a1 != getpid()) {
        return -TARGET_ESRCH;
    }
    ret = -TARGET_ESRCH;
    pthread_mutex_lock(&clone_lock);
    cpu_list_lock();
    CPUState *other;
    CPU_FOREACH(other)
    {
        TaskState *other_ts = get_task_state(other);
        if (other_ts->ts_tid == tid) {
            int err = pthread_kill(other_ts->darwin_thread, sig);
            ret = err ? -host_to_target_errno(err) : 0;
            break;
        }
    }
    cpu_list_unlock();
    pthread_mutex_unlock(&clone_lock);
    return ret;
}
#endif
#ifdef TARGET_NR_tkill
case TARGET_NR_tkill: {
    abi_long tid = num == TARGET_NR_tgkill ? a2 : a1;
    int sig = target_to_host_signal(num == TARGET_NR_tgkill ? a3 : a2);
    if (sig < 0 || sig >= NSIG) {
        return -TARGET_EINVAL;
    }
    if (num == TARGET_NR_tgkill && a1 != getpid()) {
        return -TARGET_ESRCH;
    }
    ret = -TARGET_ESRCH;
    pthread_mutex_lock(&clone_lock);
    cpu_list_lock();
    CPUState *other;
    CPU_FOREACH(other)
    {
        TaskState *other_ts = get_task_state(other);
        if (other_ts->ts_tid == tid) {
            int err = pthread_kill(other_ts->darwin_thread, sig);
            ret = err ? -host_to_target_errno(err) : 0;
            break;
        }
    }
    cpu_list_unlock();
    pthread_mutex_unlock(&clone_lock);
    return ret;
}
#endif
#ifdef TARGET_NR_getrlimit
case TARGET_NR_getrlimit: {
    return guest_rlimit(a1, 0, a2, false, true);
}
#endif
#ifdef TARGET_NR_prlimit64
case TARGET_NR_prlimit64: {
    if (a1 && a1 != getpid()) {
        return -TARGET_ESRCH;
    }
    return guest_rlimit(a2, a3, a4, true, false);
}
#endif
#ifdef TARGET_NR_prctl
case TARGET_NR_prctl: {
    if (a1 == 0x41555856) { /* PR_GET_AUXV */
        size_t size = MIN((abi_ulong)a3, ts->info->auxv_len);
        p = lock_user(VERIFY_READ, ts->info->saved_auxv, size, 1);
        if (!p && size) {
            return -TARGET_EFAULT;
        }
        ret = copy_to_user(a2, p, size) ? -TARGET_EFAULT : ts->info->auxv_len;
        unlock_user(p, ts->info->saved_auxv, 0);
        return ret;
    }
    if (a1 == 3) {
        return 1;
    } /* PR_GET_DUMPABLE */
    return -TARGET_EINVAL;
}
#endif
#ifdef TARGET_NR_getrusage
case TARGET_NR_getrusage: {
    return guest_rusage(a1, a2);
}
#endif
#ifdef TARGET_NR_socket
case TARGET_NR_socket: {
    int family = host_socket_family(a1), type = host_socket_type(a2);
    if (family < 0 || type < 0) {
        return get_errno(-1);
    }
    int fd = socket(family, type, a3);
    if (fd >= 0) {
        socket_flags(fd, a2);
    }
    return get_errno(fd);
}
#endif
#ifdef TARGET_NR_socketpair
case TARGET_NR_socketpair: {
    int family = host_socket_family(a1), type = host_socket_type(a2), fd[2];
    if (family < 0 || type < 0) {
        return get_errno(-1);
    }
    if (socketpair(family, type, a3, fd)) {
        return get_errno(-1);
    }
    socket_flags(fd[0], a2);
    socket_flags(fd[1], a2);
    if (put_user_u32(fd[0], a4) || put_user_u32(fd[1], a4 + 4)) {
        close(fd[0]);
        close(fd[1]);
        return -TARGET_EFAULT;
    }
    return 0;
}
#endif
#ifdef TARGET_NR_listen
case TARGET_NR_listen: {
    return get_errno(listen(a1, a2));
}
#endif
#ifdef TARGET_NR_shutdown
case TARGET_NR_shutdown: {
    return get_errno(shutdown(a1, a2));
}
#endif
#ifdef TARGET_NR_sendto
case TARGET_NR_sendto: {
    return guest_sendrecv(a1, a2, a3, a4, a5, a6, true);
}
#endif
#ifdef TARGET_NR_recvfrom
case TARGET_NR_recvfrom: {
    return guest_sendrecv(a1, a2, a3, a4, a5, a6, false);
}
#endif
#ifdef TARGET_NR_setsockopt
case TARGET_NR_setsockopt: {
    return guest_sockopt(a1, a2, a3, a4, a5, true);
}
#endif
#ifdef TARGET_NR_getsockopt
case TARGET_NR_getsockopt: {
    return guest_sockopt(a1, a2, a3, a4, a5, false);
}
#endif
#ifdef TARGET_NR_bind
case TARGET_NR_bind: {
    struct sockaddr_storage host;
    ret = get_sockaddr(&host, a2, a3);
    if (ret) {
        return ret;
    }
    return get_errno(num == TARGET_NR_bind
                         ? bind(a1, (struct sockaddr *)&host, a3)
                         : connect(a1, (struct sockaddr *)&host, a3));
}
#endif
#ifdef TARGET_NR_connect
case TARGET_NR_connect: {
    struct sockaddr_storage host;
    ret = get_sockaddr(&host, a2, a3);
    if (ret) {
        return ret;
    }
    return get_errno(num == TARGET_NR_bind
                         ? bind(a1, (struct sockaddr *)&host, a3)
                         : connect(a1, (struct sockaddr *)&host, a3));
}
#endif
#ifdef TARGET_NR_getsockname
case TARGET_NR_getsockname: {
    struct sockaddr_storage host;
    socklen_t len = sizeof(host);
    ret = get_errno(num == TARGET_NR_getsockname
                        ? getsockname(a1, (struct sockaddr *)&host, &len)
                        : getpeername(a1, (struct sockaddr *)&host, &len));
    return ret ? ret : put_sockaddr(a2, a3, &host, len);
}
#endif
#ifdef TARGET_NR_getpeername
case TARGET_NR_getpeername: {
    struct sockaddr_storage host;
    socklen_t len = sizeof(host);
    ret = get_errno(num == TARGET_NR_getsockname
                        ? getsockname(a1, (struct sockaddr *)&host, &len)
                        : getpeername(a1, (struct sockaddr *)&host, &len));
    return ret ? ret : put_sockaddr(a2, a3, &host, len);
}
#endif
#ifdef TARGET_NR_accept
case TARGET_NR_accept: {
    struct sockaddr_storage host;
    socklen_t len = sizeof(host);
    int flags = num == TARGET_NR_accept4 ? a4 : 0;
    if (flags & ~(0x800 | 0x80000)) {
        return -TARGET_EINVAL;
    }
    int fd = accept(a1, a2 ? (struct sockaddr *)&host : NULL, a2 ? &len : NULL);
    if (fd < 0) {
        return get_errno(-1);
    }
    socket_flags(fd, flags);
    if (a2) {
        ret = put_sockaddr(a2, a3, &host, len);
        if (ret) {
            close(fd);
            return ret;
        }
    }
    return fd;
}
#endif
#ifdef TARGET_NR_accept4
case TARGET_NR_accept4: {
    struct sockaddr_storage host;
    socklen_t len = sizeof(host);
    int flags = num == TARGET_NR_accept4 ? a4 : 0;
    if (flags & ~(0x800 | 0x80000)) {
        return -TARGET_EINVAL;
    }
    int fd = accept(a1, a2 ? (struct sockaddr *)&host : NULL, a2 ? &len : NULL);
    if (fd < 0) {
        return get_errno(-1);
    }
    socket_flags(fd, flags);
    if (a2) {
        ret = put_sockaddr(a2, a3, &host, len);
        if (ret) {
            close(fd);
            return ret;
        }
    }
    return fd;
}
#endif

#ifdef TARGET_NR_sendmsg
case TARGET_NR_sendmsg: {
    return guest_message(a1, a2, a3, true);
}
#endif
#ifdef TARGET_NR_recvmsg
case TARGET_NR_recvmsg: {
    return guest_message(a1, a2, a3, false);
}
#endif
