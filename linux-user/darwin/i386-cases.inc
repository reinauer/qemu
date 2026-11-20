/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef TARGET_ABI32
case TARGET_NR_clock_gettime64:
case TARGET_NR_clock_getres_time64: {
    struct timespec out;
    int clock = host_clock(a1);
    if (clock < 0) {
        return -TARGET_EINVAL;
    }
    if (!a2 && num == TARGET_NR_clock_gettime64) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(num == TARGET_NR_clock_gettime64
                    ? clock_gettime(clock, &out) : clock_getres(clock, &out));
    return ret ? ret : (a2 ? put_timespec64(a2, &out) : 0);
}
case TARGET_NR_clock_nanosleep_time64: {
    struct timespec req, rem, now;
    int clock = host_clock(a1);
    if (clock < 0 || (a2 & ~1)) {
        return -TARGET_EINVAL;
    }
    ret = get_timespec64(&req, a3);
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
    if (ret == -TARGET_EINTR && !(a2 & 1) && a4 && put_timespec64(a4, &rem)) {
        return -TARGET_EFAULT;
    }
    return ret;
}
case TARGET_NR_futex_time64:
    return guest_futex(a1, a2, a3, a4, a6, true);
case TARGET_NR_set_thread_area:
    return do_set_thread_area(env, a1);
case TARGET_NR_get_thread_area:
    return do_get_thread_area(env, a1);
case TARGET_NR_sigreturn:
    return do_sigreturn(env);
case TARGET_NR_getuid32:
    return getuid();
case TARGET_NR_geteuid32:
    return geteuid();
case TARGET_NR_getgid32:
    return getgid();
case TARGET_NR_getegid32:
    return getegid();
case TARGET_NR_fchown32:
    return get_errno(fchown(a1, a2, a3));
case TARGET_NR_fcntl64:
    return guest_fcntl(a1, a2, a3);
case TARGET_NR_ugetrlimit:
    return guest_rlimit(a1, 0, a2, false, false);
case TARGET_NR_stat64:
    return guest_stat(TARGET_AT_FDCWD, a1, a2, 0, true);
case TARGET_NR_lstat64:
    return guest_stat(TARGET_AT_FDCWD, a1, a2, TARGET_AT_SYMLINK_NOFOLLOW, true);
case TARGET_NR_fstat64:
    return guest_stat(a1, 0, a2, -1, true);
case TARGET_NR__llseek: {
    off_t offset = (uint64_t)(uint32_t)a3 | ((uint64_t)(uint32_t)a2 << 32);
    offset = lseek(a1, offset, a5);
    if (offset == -1) {
        return get_errno(-1);
    }
    return put_user_u64(offset, a4);
}
case TARGET_NR_ftruncate64:
    return get_errno(ftruncate(a1, (uint64_t)(uint32_t)a2 |
                             ((uint64_t)(uint32_t)a3 << 32)));
case TARGET_NR_truncate64: {
    p = lock_user_string(a1);
    if (!p) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(truncate(path(p), (uint64_t)(uint32_t)a2 |
                            ((uint64_t)(uint32_t)a3 << 32)));
    unlock_user(p, a1, 0);
    return ret;
}
case TARGET_NR_mmap2: {
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
    return get_errno(target_mmap((abi_ulong)a1, (abi_ulong)a2, a3, flags, a5,
                                (uint64_t)(uint32_t)a6 << 12));
}
case TARGET_NR_waitpid:
    return guest_syscall(env, TARGET_NR_wait4, a1, a2, a3, 0, 0, 0);
case TARGET_NR_socketcall: {
    static const int numbers[] = {
        0, TARGET_NR_socket, TARGET_NR_bind, TARGET_NR_connect,
        TARGET_NR_listen, TARGET_NR_accept4, TARGET_NR_getsockname,
        TARGET_NR_getpeername, TARGET_NR_socketpair, TARGET_NR_sendto,
        TARGET_NR_recvfrom, TARGET_NR_sendto, TARGET_NR_recvfrom,
        TARGET_NR_shutdown, TARGET_NR_setsockopt, TARGET_NR_getsockopt,
        TARGET_NR_sendmsg, TARGET_NR_recvmsg, TARGET_NR_accept4,
    };
    static const unsigned counts[] = {
        0, 3, 3, 3, 2, 3, 3, 3, 4, 4, 4, 6, 6, 2, 5, 5, 3, 3, 4,
    };
    abi_ulong args[6] = {0};
    if (a1 <= 0 || a1 >= ARRAY_SIZE(numbers)) {
        return -TARGET_EINVAL;
    }
    if (copy_from_user(args, a2, counts[a1] * sizeof(abi_ulong))) {
        return -TARGET_EFAULT;
    }
    return guest_syscall(env, numbers[a1], tswapal(args[0]), tswapal(args[1]),
                         tswapal(args[2]), tswapal(args[3]), tswapal(args[4]),
                         tswapal(args[5]));
}
#endif
