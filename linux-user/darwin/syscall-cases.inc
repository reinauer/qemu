/* SPDX-License-Identifier: GPL-2.0-or-later */
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
