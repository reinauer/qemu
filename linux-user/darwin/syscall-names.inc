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
#ifdef TARGET_NR_ioctl
case TARGET_NR_ioctl:
return "ioctl";
#endif







#ifdef TARGET_NR_statx
case TARGET_NR_statx:
return "statx";
#endif
