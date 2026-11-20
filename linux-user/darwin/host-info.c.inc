/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <sys/sysctl.h>
#include <mach/mach_host.h>

static abi_long guest_sysinfo(abi_ulong addr)
{
    struct target_sysinfo out = { 0 };
    uint64_t memory;
    size_t size = sizeof(memory);
    struct timespec uptime;
    double load[3] = { 0 };
    vm_statistics64_data_t vm;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    mach_port_t host = mach_host_self();
    kern_return_t kr;
    unsigned processes = 0;
    CPUState *cpu;

    if (sysctlbyname("hw.memsize", &memory, &size, NULL, 0) ||
        clock_gettime(CLOCK_MONOTONIC, &uptime)) {
        mach_port_deallocate(mach_task_self(), host);
        return get_errno(-1);
    }
    out.uptime = tswapal(uptime.tv_sec);
    uint64_t unit = sizeof(abi_ulong) == 4 ? 4096 : 1;
    out.totalram = tswapal(memory / unit);
    out.mem_unit = tswap32(unit);
    getloadavg(load, ARRAY_SIZE(load));
    for (int i = 0; i < ARRAY_SIZE(load); i++) {
        out.loads[i] = tswapal((abi_ulong)(load[i] * 65536));
    }
    kr = host_statistics64(host, HOST_VM_INFO64,
                            (host_info64_t)&vm, &count);
    mach_port_deallocate(mach_task_self(), host);
    if (kr == KERN_SUCCESS) {
        uint64_t free = (uint64_t)vm.free_count * qemu_real_host_page_size();
        out.freeram = tswapal(MIN(memory, free) / unit);
    }
    struct xsw_usage swap;
    size = sizeof(swap);
    if (!sysctlbyname("vm.swapusage", &swap, &size, NULL, 0)) {
        out.totalswap = tswapal(swap.xsu_total / unit);
        out.freeswap = tswapal(swap.xsu_avail / unit);
    }
    /* Darwin has no Linux process namespace; count local emulated tasks. */
    cpu_list_lock();
    CPU_FOREACH(cpu) {
        processes++;
    }
    cpu_list_unlock();
    out.procs = tswap16(MIN(processes, UINT16_MAX));
    return copy_to_user(addr, &out, sizeof(out)) ? -TARGET_EFAULT : 0;
}

static abi_long guest_getaffinity(pid_t pid, abi_ulong capacity, abi_ulong addr)
{
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    bool found = pid == 0 || pid == getpid();
    CPUState *cpu;
    size_t bytes;
    abi_ulong *mask;

    if (!found) {
        cpu_list_lock();
        CPU_FOREACH(cpu) {
            TaskState *ts = get_task_state(cpu);
            if (ts && ts->ts_tid == pid) {
                found = true;
                break;
            }
        }
        cpu_list_unlock();
    }
    if (!found) {
        return -TARGET_ESRCH;
    }
    if (cpus < 1 || cpus > 65536) {
        return -TARGET_EINVAL;
    }
    bytes = DIV_ROUND_UP(cpus, TARGET_ABI_BITS) * sizeof(abi_ulong);
    if (capacity < bytes || capacity % sizeof(abi_ulong)) {
        return -TARGET_EINVAL;
    }
    mask = lock_user(VERIFY_WRITE, addr, bytes, 0);
    if (!mask) {
        return -TARGET_EFAULT;
    }
    memset(mask, 0, bytes);
    for (long i = 0; i < cpus; i++) {
        mask[i / TARGET_ABI_BITS] |= (abi_ulong)1 << (i % TARGET_ABI_BITS);
    }
    for (size_t i = 0; i < bytes / sizeof(*mask); i++) {
        mask[i] = tswapal(mask[i]);
    }
    unlock_user(mask, addr, bytes);
    return bytes;
}
