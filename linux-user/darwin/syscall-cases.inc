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
    return guest_io(a1, a2, a3, false, true, a4);
}
#endif
#ifdef TARGET_NR_pwrite64
case TARGET_NR_pwrite64: {
    return guest_io(a1, a2, a3, true, true, a4);
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
    return guest_stat(a1, 0, a2, -1);
}
#endif
#ifdef TARGET_NR_newfstatat
case TARGET_NR_newfstatat: {
    return guest_stat(a1, a2, a3, a4);
}
#endif
#ifdef TARGET_NR_fstatat64
case TARGET_NR_fstatat64: {
    return guest_stat(a1, a2, a3, a4);
}
#endif
#ifdef TARGET_NR_stat
case TARGET_NR_stat: {
    return guest_stat(TARGET_AT_FDCWD, a1, a2, 0);
}
#endif
#ifdef TARGET_NR_lstat
case TARGET_NR_lstat: {
    return guest_stat(TARGET_AT_FDCWD, a1, a2, TARGET_AT_SYMLINK_NOFOLLOW);
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
