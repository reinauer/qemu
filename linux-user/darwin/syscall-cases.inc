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
