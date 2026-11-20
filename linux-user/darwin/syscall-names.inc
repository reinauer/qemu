/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef TARGET_NR_exit
case TARGET_NR_exit:
return "exit";
#endif
#ifdef TARGET_NR_exit_group
case TARGET_NR_exit_group:
return "exit_group";
#endif
#ifdef TARGET_NR_brk
case TARGET_NR_brk:
return "brk";
#endif
#ifdef TARGET_NR_mmap
case TARGET_NR_mmap:
return "mmap";
#endif
#ifdef TARGET_NR_munmap
case TARGET_NR_munmap:
return "munmap";
#endif
#ifdef TARGET_NR_mprotect
case TARGET_NR_mprotect:
return "mprotect";
#endif
#ifdef TARGET_NR_mremap
case TARGET_NR_mremap:
return "mremap";
#endif
#ifdef TARGET_NR_madvise
case TARGET_NR_madvise:
return "madvise";
#endif
#ifdef TARGET_NR_msync
case TARGET_NR_msync:
return "msync";
#endif
#ifdef TARGET_NR_arch_prctl
case TARGET_NR_arch_prctl:
return "arch_prctl";
#endif
#ifdef TARGET_NR_uname
case TARGET_NR_uname:
return "uname";
#endif







