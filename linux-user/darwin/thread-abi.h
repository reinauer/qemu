/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Linux clone ABI flag values, independent of the Darwin host. */
#define CLONE_VM 0x100
#define CLONE_FS 0x200
#define CLONE_FILES 0x400
#define CLONE_SIGHAND 0x800
#define CLONE_PIDFD 0x1000
#define CLONE_VFORK 0x4000
#define CLONE_PARENT 0x8000
#define CLONE_THREAD 0x10000
#define CLONE_SYSVSEM 0x40000
#define CLONE_SETTLS 0x80000
#define CLONE_PARENT_SETTID 0x100000
#define CLONE_CHILD_CLEARTID 0x200000
#define CLONE_DETACHED 0x400000
#define CLONE_CHILD_SETTID 0x1000000
#define CLONE_IO 0x80000000
#define CSIGNAL 0xff
#define CLONE_THREAD_FLAGS                                                     \
    (CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD |        \
     CLONE_SYSVSEM)
#define CLONE_IGNORED_FLAGS (CLONE_DETACHED | CLONE_IO)
#define CLONE_OPTIONAL_FORK_FLAGS                                              \
    (CLONE_SETTLS | CLONE_PARENT_SETTID | CLONE_PIDFD | CLONE_CHILD_CLEARTID | \
     CLONE_CHILD_SETTID)
#define CLONE_OPTIONAL_THREAD_FLAGS                                            \
    (CLONE_SETTLS | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID |               \
     CLONE_CHILD_SETTID | CLONE_PARENT)
#define CLONE_INVALID_FORK_FLAGS                                               \
    (~(CSIGNAL | CLONE_OPTIONAL_FORK_FLAGS | CLONE_IGNORED_FLAGS))
#define CLONE_INVALID_THREAD_FLAGS                                             \
    (~(CSIGNAL | CLONE_THREAD_FLAGS | CLONE_OPTIONAL_THREAD_FLAGS |            \
       CLONE_IGNORED_FLAGS))
