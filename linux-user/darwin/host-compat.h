/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_DARWIN_LINUX_HOST_COMPAT_H
#define QEMU_DARWIN_LINUX_HOST_COMPAT_H
/* The inode64 directory syscall is exported by Darwin's libSystem. */
ssize_t __getdirentries64(int fd, void *buf, size_t size, off_t *base);
#endif
