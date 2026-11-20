if not have_linux_user
   subdir_done()
endif

if host_os == 'darwin'
  if host_arch != 'aarch64' or target_dirs != ['x86_64-linux-user']
    error('Experimental Darwin linux-user requires an ARM64 host and x86_64-linux-user only')
  endif
endif

linux_user_ss = ss.source_set()

common_user_inc += include_directories('include/host/' / host_arch)
common_user_inc += include_directories('include')

linux_user_ss.add(files(
  'elfload.c',
  'exit.c',
  'linuxload.c',
  'main.c',
  'mmap.c',
  'signal.c',
  'thunk.c',
  'uaccess.c',
  'uname.c',
))
if host_os == 'darwin'
  linux_user_ss.add(files('darwin/syscall.c', 'darwin/strace.c'))
else
  linux_user_ss.add(files('syscall.c', 'strace.c', 'fd-trans.c'))
endif
linux_user_ss.add(rt)
linux_user_ss.add(libdw)

linux_user_ss.add(when: 'TARGET_HAS_BFLT', if_true: files('flatload.c'))
linux_user_ss.add(when: 'TARGET_I386', if_true: files('vm86.c'))
linux_user_ss.add(when: 'CONFIG_ARM_COMPATIBLE_SEMIHOSTING', if_true: files('semihost.c'))

if get_option('plugins')
  linux_user_ss.add(files('plugin-api.c'))
endif

syscall_nr_generators = {}

gen_vdso_exe = executable('gen-vdso', 'gen-vdso.c',
                          native: true, build_by_default: false)
gen_vdso = generator(gen_vdso_exe, output: '@BASENAME@.c.inc',
                     arguments: ['-o', '@OUTPUT@', '@EXTRA_ARGS@', '@INPUT@'])

subdir('aarch64')
subdir('alpha')
subdir('arm')
subdir('hexagon')
subdir('hppa')
subdir('i386')
subdir('loongarch64')
subdir('m68k')
subdir('microblaze')
subdir('mips64')
subdir('mips')
subdir('or1k')
subdir('ppc')
subdir('riscv')
subdir('s390x')
subdir('sh4')
subdir('sparc')
subdir('x86_64')
subdir('xtensa')

specific_ss.add_all(when: 'CONFIG_LINUX_USER', if_true: linux_user_ss)
