/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef TARGET_NR_exit
case TARGET_NR_exit:
return "exit";
#endif
#ifdef TARGET_NR_exit_group
case TARGET_NR_exit_group:
return "exit_group";
#endif
#ifdef TARGET_NR_read
case TARGET_NR_read:
return "read";
#endif
#ifdef TARGET_NR_write
case TARGET_NR_write:
return "write";
#endif
#ifdef TARGET_NR_pread64
case TARGET_NR_pread64:
return "pread64";
#endif
#ifdef TARGET_NR_pwrite64
case TARGET_NR_pwrite64:
return "pwrite64";
#endif
#ifdef TARGET_NR_readv
case TARGET_NR_readv:
return "readv";
#endif
#ifdef TARGET_NR_writev
case TARGET_NR_writev:
return "writev";
#endif
#ifdef TARGET_NR_open
case TARGET_NR_open:
return "open";
#endif
#ifdef TARGET_NR_openat
case TARGET_NR_openat:
return "openat";
#endif
#ifdef TARGET_NR_close
case TARGET_NR_close:
return "close";
#endif
#ifdef TARGET_NR_lseek
case TARGET_NR_lseek:
return "lseek";
#endif
#ifdef TARGET_NR_fcntl
case TARGET_NR_fcntl:
return "fcntl";
#endif
#ifdef TARGET_NR_dup
case TARGET_NR_dup:
return "dup";
#endif
#ifdef TARGET_NR_dup2
case TARGET_NR_dup2:
return "dup2";
#endif
#ifdef TARGET_NR_dup3
case TARGET_NR_dup3:
return "dup3";
#endif
#ifdef TARGET_NR_pipe
case TARGET_NR_pipe:
return "pipe";
#endif
#ifdef TARGET_NR_pipe2
case TARGET_NR_pipe2:
return "pipe2";
#endif
#ifdef TARGET_NR_fstat
case TARGET_NR_fstat:
return "fstat";
#endif
#ifdef TARGET_NR_newfstatat
case TARGET_NR_newfstatat:
return "newfstatat";
#endif
#ifdef TARGET_NR_fstatat64
case TARGET_NR_fstatat64:
return "fstatat64";
#endif
#ifdef TARGET_NR_stat
case TARGET_NR_stat:
return "stat";
#endif
#ifdef TARGET_NR_lstat
case TARGET_NR_lstat:
return "lstat";
#endif
#ifdef TARGET_NR_getdents64
case TARGET_NR_getdents64:
return "getdents64";
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
#ifdef TARGET_NR_getpid
case TARGET_NR_getpid:
return "getpid";
#endif
#ifdef TARGET_NR_getppid
case TARGET_NR_getppid:
return "getppid";
#endif
#ifdef TARGET_NR_getuid
case TARGET_NR_getuid:
return "getuid";
#endif
#ifdef TARGET_NR_geteuid
case TARGET_NR_geteuid:
return "geteuid";
#endif
#ifdef TARGET_NR_getgid
case TARGET_NR_getgid:
return "getgid";
#endif
#ifdef TARGET_NR_getegid
case TARGET_NR_getegid:
return "getegid";
#endif
#ifdef TARGET_NR_getpgrp
case TARGET_NR_getpgrp:
return "getpgrp";
#endif
#ifdef TARGET_NR_setsid
case TARGET_NR_setsid:
return "setsid";
#endif
#ifdef TARGET_NR_getpgid
case TARGET_NR_getpgid:
return "getpgid";
#endif
#ifdef TARGET_NR_getsid
case TARGET_NR_getsid:
return "getsid";
#endif
#ifdef TARGET_NR_setpgid
case TARGET_NR_setpgid:
return "setpgid";
#endif
#ifdef TARGET_NR_fsync
case TARGET_NR_fsync:
return "fsync";
#endif
#ifdef TARGET_NR_fdatasync
case TARGET_NR_fdatasync:
return "fdatasync";
#endif
#ifdef TARGET_NR_ftruncate
case TARGET_NR_ftruncate:
return "ftruncate";
#endif
#ifdef TARGET_NR_fchmod
case TARGET_NR_fchmod:
return "fchmod";
#endif
#ifdef TARGET_NR_fchown
case TARGET_NR_fchown:
return "fchown";
#endif
#ifdef TARGET_NR_fchdir
case TARGET_NR_fchdir:
return "fchdir";
#endif
#ifdef TARGET_NR_umask
case TARGET_NR_umask:
return "umask";
#endif
#ifdef TARGET_NR_sched_yield
case TARGET_NR_sched_yield:
return "sched_yield";
#endif
#ifdef TARGET_NR_gettid
case TARGET_NR_gettid:
return "gettid";
#endif
#ifdef TARGET_NR_set_tid_address
case TARGET_NR_set_tid_address:
return "set_tid_address";
#endif
#ifdef TARGET_NR_arch_prctl
case TARGET_NR_arch_prctl:
return "arch_prctl";
#endif
#ifdef TARGET_NR_uname
case TARGET_NR_uname:
return "uname";
#endif
#ifdef TARGET_NR_getcwd
case TARGET_NR_getcwd:
return "getcwd";
#endif
#ifdef TARGET_NR_chdir
case TARGET_NR_chdir:
return "chdir";
#endif
#ifdef TARGET_NR_unlink
case TARGET_NR_unlink:
return "unlink";
#endif
#ifdef TARGET_NR_mkdir
case TARGET_NR_mkdir:
return "mkdir";
#endif
#ifdef TARGET_NR_rmdir
case TARGET_NR_rmdir:
return "rmdir";
#endif
#ifdef TARGET_NR_chmod
case TARGET_NR_chmod:
return "chmod";
#endif
#ifdef TARGET_NR_chown
case TARGET_NR_chown:
return "chown";
#endif
#ifdef TARGET_NR_lchown
case TARGET_NR_lchown:
return "lchown";
#endif
#ifdef TARGET_NR_access
case TARGET_NR_access:
return "access";
#endif
#ifdef TARGET_NR_truncate
case TARGET_NR_truncate:
return "truncate";
#endif
#ifdef TARGET_NR_faccessat
case TARGET_NR_faccessat:
return "faccessat";
#endif
#ifdef TARGET_NR_faccessat2
case TARGET_NR_faccessat2:
return "faccessat2";
#endif
#ifdef TARGET_NR_unlinkat
case TARGET_NR_unlinkat:
return "unlinkat";
#endif
#ifdef TARGET_NR_mkdirat
case TARGET_NR_mkdirat:
return "mkdirat";
#endif
#ifdef TARGET_NR_readlink
case TARGET_NR_readlink:
return "readlink";
#endif
#ifdef TARGET_NR_readlinkat
case TARGET_NR_readlinkat:
return "readlinkat";
#endif
#ifdef TARGET_NR_clock_gettime
case TARGET_NR_clock_gettime:
return "clock_gettime";
#endif
#ifdef TARGET_NR_clock_getres
case TARGET_NR_clock_getres:
return "clock_getres";
#endif
#ifdef TARGET_NR_gettimeofday
case TARGET_NR_gettimeofday:
return "gettimeofday";
#endif
#ifdef TARGET_NR_time
case TARGET_NR_time:
return "time";
#endif
#ifdef TARGET_NR_nanosleep
case TARGET_NR_nanosleep:
return "nanosleep";
#endif
#ifdef TARGET_NR_clock_nanosleep
case TARGET_NR_clock_nanosleep:
return "clock_nanosleep";
#endif
#ifdef TARGET_NR_getrandom
case TARGET_NR_getrandom:
return "getrandom";
#endif
#ifdef TARGET_NR_rt_sigaction
case TARGET_NR_rt_sigaction:
return "rt_sigaction";
#endif
#ifdef TARGET_NR_rt_sigprocmask
case TARGET_NR_rt_sigprocmask:
return "rt_sigprocmask";
#endif
#ifdef TARGET_NR_rt_sigreturn
case TARGET_NR_rt_sigreturn:
return "rt_sigreturn";
#endif
#ifdef TARGET_NR_sigaltstack
case TARGET_NR_sigaltstack:
return "sigaltstack";
#endif
#ifdef TARGET_NR_kill
case TARGET_NR_kill:
return "kill";
#endif
#ifdef TARGET_NR_fork
case TARGET_NR_fork:
return "fork";
#endif
#ifdef TARGET_NR_vfork
case TARGET_NR_vfork:
return "vfork";
#endif
#ifdef TARGET_NR_clone
case TARGET_NR_clone:
return "clone";
#endif
#ifdef TARGET_NR_execve
case TARGET_NR_execve:
return "execve";
#endif
#ifdef TARGET_NR_wait4
case TARGET_NR_wait4:
return "wait4";
#endif
#ifdef TARGET_NR_futex
case TARGET_NR_futex:
return "futex";
#endif
#ifdef TARGET_NR_ioctl
case TARGET_NR_ioctl:
return "ioctl";
#endif
#ifdef TARGET_NR_poll
case TARGET_NR_poll:
return "poll";
#endif
#ifdef TARGET_NR_ppoll
case TARGET_NR_ppoll:
return "ppoll";
#endif
#ifdef TARGET_NR_tgkill
case TARGET_NR_tgkill:
return "tgkill";
#endif
#ifdef TARGET_NR_tkill
case TARGET_NR_tkill:
return "tkill";
#endif
#ifdef TARGET_NR_getrlimit
case TARGET_NR_getrlimit:
return "getrlimit";
#endif
#ifdef TARGET_NR_prlimit64
case TARGET_NR_prlimit64:
return "prlimit64";
#endif
#ifdef TARGET_NR_prctl
case TARGET_NR_prctl:
return "prctl";
#endif
#ifdef TARGET_NR_getrusage
case TARGET_NR_getrusage:
return "getrusage";
#endif
#ifdef TARGET_NR_socket
case TARGET_NR_socket:
return "socket";
#endif
#ifdef TARGET_NR_socketpair
case TARGET_NR_socketpair:
return "socketpair";
#endif
#ifdef TARGET_NR_listen
case TARGET_NR_listen:
return "listen";
#endif
#ifdef TARGET_NR_shutdown
case TARGET_NR_shutdown:
return "shutdown";
#endif
#ifdef TARGET_NR_sendto
case TARGET_NR_sendto:
return "sendto";
#endif
#ifdef TARGET_NR_recvfrom
case TARGET_NR_recvfrom:
return "recvfrom";
#endif
#ifdef TARGET_NR_setsockopt
case TARGET_NR_setsockopt:
return "setsockopt";
#endif
#ifdef TARGET_NR_getsockopt
case TARGET_NR_getsockopt:
return "getsockopt";
#endif
#ifdef TARGET_NR_bind
case TARGET_NR_bind:
return "bind";
#endif
#ifdef TARGET_NR_connect
case TARGET_NR_connect:
return "connect";
#endif
#ifdef TARGET_NR_getsockname
case TARGET_NR_getsockname:
return "getsockname";
#endif
#ifdef TARGET_NR_getpeername
case TARGET_NR_getpeername:
return "getpeername";
#endif
#ifdef TARGET_NR_accept
case TARGET_NR_accept:
return "accept";
#endif
#ifdef TARGET_NR_accept4
case TARGET_NR_accept4:
return "accept4";
#endif

#ifdef TARGET_NR_sendmsg
case TARGET_NR_sendmsg:
return "sendmsg";
#endif
#ifdef TARGET_NR_recvmsg
case TARGET_NR_recvmsg:
return "recvmsg";
#endif

#ifdef TARGET_NR_sysinfo
case TARGET_NR_sysinfo:
return "sysinfo";
#endif

#ifdef TARGET_NR_sched_getaffinity
case TARGET_NR_sched_getaffinity:
return "sched_getaffinity";
#endif

#ifdef TARGET_NR_symlink
case TARGET_NR_symlink:
return "symlink";
#endif

#ifdef TARGET_NR_symlinkat
case TARGET_NR_symlinkat:
return "symlinkat";
#endif

#ifdef TARGET_NR_utimensat
case TARGET_NR_utimensat:
return "utimensat";
#endif

#ifdef TARGET_NR_clock_gettime64
case TARGET_NR_clock_gettime64:
return "clock_gettime64";
#endif
#ifdef TARGET_NR_clock_getres_time64
case TARGET_NR_clock_getres_time64:
return "clock_getres_time64";
#endif
#ifdef TARGET_NR_clock_nanosleep_time64
case TARGET_NR_clock_nanosleep_time64:
return "clock_nanosleep_time64";
#endif
#ifdef TARGET_NR_futex_time64
case TARGET_NR_futex_time64:
return "futex_time64";
#endif
#ifdef TARGET_NR_utimensat_time64
case TARGET_NR_utimensat_time64:
return "utimensat_time64";
#endif
#ifdef TARGET_NR_set_thread_area
case TARGET_NR_set_thread_area:
return "set_thread_area";
#endif
#ifdef TARGET_NR_get_thread_area
case TARGET_NR_get_thread_area:
return "get_thread_area";
#endif
#ifdef TARGET_NR_sigreturn
case TARGET_NR_sigreturn:
return "sigreturn";
#endif
#ifdef TARGET_NR_getuid32
case TARGET_NR_getuid32:
return "getuid32";
#endif
#ifdef TARGET_NR_geteuid32
case TARGET_NR_geteuid32:
return "geteuid32";
#endif
#ifdef TARGET_NR_getgid32
case TARGET_NR_getgid32:
return "getgid32";
#endif
#ifdef TARGET_NR_getegid32
case TARGET_NR_getegid32:
return "getegid32";
#endif
#ifdef TARGET_NR_fchown32
case TARGET_NR_fchown32:
return "fchown32";
#endif
#ifdef TARGET_NR_fcntl64
case TARGET_NR_fcntl64:
return "fcntl64";
#endif
#ifdef TARGET_NR_ugetrlimit
case TARGET_NR_ugetrlimit:
return "ugetrlimit";
#endif
#ifdef TARGET_NR_stat64
case TARGET_NR_stat64:
return "stat64";
#endif
#ifdef TARGET_NR_lstat64
case TARGET_NR_lstat64:
return "lstat64";
#endif
#ifdef TARGET_NR_fstat64
case TARGET_NR_fstat64:
return "fstat64";
#endif
#ifdef TARGET_NR__llseek
case TARGET_NR__llseek:
return "_llseek";
#endif
#ifdef TARGET_NR_ftruncate64
case TARGET_NR_ftruncate64:
return "ftruncate64";
#endif
#ifdef TARGET_NR_truncate64
case TARGET_NR_truncate64:
return "truncate64";
#endif
#ifdef TARGET_NR_mmap2
case TARGET_NR_mmap2:
return "mmap2";
#endif
#ifdef TARGET_NR_waitpid
case TARGET_NR_waitpid:
return "waitpid";
#endif
#ifdef TARGET_NR_socketcall
case TARGET_NR_socketcall:
return "socketcall";
#endif
#ifdef TARGET_NR_statx
case TARGET_NR_statx:
return "statx";
#endif
