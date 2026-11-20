#ifndef UNAME_H
#define UNAME_H

#include <sys/utsname.h>
#ifdef __APPLE__
struct new_utsname {
    char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65];
};
#else
#include <linux/utsname.h>
#endif

const char *cpu_to_uname_machine(CPUArchState *cpu_env);
int sys_uname(struct new_utsname *buf);

#endif /* UNAME_H */
