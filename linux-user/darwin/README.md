<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Linux user mode on macOS

This directory implements the linux-user syscall layer for Darwin hosts,
so that `qemu-x86_64` and `qemu-i386` on an ARM64 macOS host run x86-64
and i386 Linux executables. The CPU translator, ELF loader, Linux guest
ABI structures and signal frames are shared with Linux hosts; the code
here replaces `linux-user/syscall.c`. Darwin errno values, open flags,
resource selectors, clock IDs and socket structures differ from Linux
and are translated explicitly, so that no Darwin value reaches the guest
as Linux ABI.

## Running programs

```sh
qemu-x86_64 ./static-linux-program argument
qemu-x86_64 -L /path/to/x86_64-linux-rootfs ./linux-program argument
qemu-i386 -L /path/to/i386-linux-rootfs ./linux-program32 argument
```

Dynamic executables need a matching Linux ELF interpreter and libraries
under the directory passed to `-L`; macOS libraries cannot replace Linux
glibc or musl. The `-L` setting survives a guest `execve` of another ELF
program, and shebang scripts find their interpreter through the same
path mapping. `-strace` prints guest syscalls with their translated
return values.

When a guest program executes a program of the other architecture, the
emulator starts the other emulator binary, which must be in the same
directory. The `QEMU_LD_PREFIX_I386` and `QEMU_LD_PREFIX_X86_64`
environment variables give the `-L` prefix for such children.

Files are the host's: `-L` only substitutes paths for Linux runtime
files, and does not create an isolated filesystem. The guest reports
Linux 6.6.0 as its kernel version, which does not mean that every
Linux 6.6 interface is implemented.

## Darwin host specifics

The x86-64 guest address space is a sparse 1 TiB reservation, which
keeps guest pointers in the canonical lower address range although
Darwin ignores mmap() address hints. It reserves address space, not
memory. `-R` adjusts its size; `-R 0` disables it. Legacy vsyscall
entry points above the window are emulated on instruction-fetch faults.
i386 guests use the existing 32-bit reservation.

Fixed mappings replace parts of a Mach reservation with `mmap`, which
preserves no-replace semantics. A guard at `guest_base` keeps the host
allocator from returning a mapping that wraps around guest address zero.
Mach VM region queries replace `/proc/self/maps` for host address-space
discovery.

On ARM64 Darwin, protection faults can arrive as `SIGBUS` with
`BUS_ADRALN`. The handler decodes the data abort syndrome to tell them
from alignment faults, so that stores to pages with translated code
invalidate it and retry. A protected page without a resident host
translation reports a translation fault instead; for those, the handler
consults QEMU's page flags. Accessible pages are never treated as
protection faults, because invalid file backing would otherwise retry
forever.

Host pages are 16 KiB, so a partial host page can already map the file
being loaded, and the kernel stalls reading a file into a mapping of
itself. File loading and guest file I/O therefore go through temporary
buffers. Older ELF loaders map file-backed alignment padding beyond EOF;
before a partial replacement, a checked Mach read detects invalid
backing, which is replaced with an anonymous page.

Guest threads are host pthreads. Futexes are process-local waits on
condition variables.

## Tests

`tests/` holds Linux guest programs for the paths above, such as code
writes after `fork`, partial pages beyond EOF, `/proc/self` aliases and
i386 time64 calls. Build them as Linux programs for the guest
architecture and run them under the emulator with a matching runtime.

## Limitations

- Not every Linux syscall is implemented. Unsupported operations return
  `ENOSYS`, `EINVAL` or the appropriate error.
- No process-shared or robust futexes, and only wait/wake and
  wait/wake-bitset futex operations.
- No realtime signals, epoll, eventfd, signalfd, timerfd, io_uring,
  netlink, namespaces, Linux device ioctls or complete terminal ioctls.
- `mremap`, many extended-attribute and filesystem calls, advanced
  socket operations and most `prctl` operations are unsupported.
- i386 `modify_ldt` and vm86 are unsupported.
- `sched_setaffinity` is unsupported, and the `sysinfo` process count
  includes only emulated tasks.
- AppImages cannot mount themselves, because there is no FUSE; run them
  extracted.
- Blocking calls go through Darwin's libc, not safe-syscall, so races
  between signals and syscall restart are not handled.
- Applications that need more than 1 TiB of guest address space need a
  larger `-R`.
- Only ARM64 hosts and x86 guests have been tested.
