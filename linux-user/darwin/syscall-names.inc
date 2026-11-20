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
#ifdef TARGET_NR_arch_prctl
case TARGET_NR_arch_prctl:
return "arch_prctl";
#endif
#ifdef TARGET_NR_uname
case TARGET_NR_uname:
return "uname";
#endif







