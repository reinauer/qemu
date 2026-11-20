/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Adapted from the current linux-user/syscall.c thread implementation. */
#define NEW_STACK_SIZE 0x40000

static pthread_mutex_t clone_lock = PTHREAD_MUTEX_INITIALIZER;
typedef struct {
    CPUArchState *env;
    QemuSemaphore sem;
    pthread_t thread;
    uint32_t tid;
    abi_ulong child_tidptr;
    abi_ulong parent_tidptr;
    sigset_t sigmask;
} new_thread_info;

static void *clone_func(void *arg)
{
    new_thread_info *info = arg;
    CPUArchState *env;
    CPUState *cpu;
    TaskState *ts;

    rcu_register_thread();
    tcg_register_thread();
    env = info->env;
    cpu = env_cpu(env);
    thread_cpu = cpu;
    ts = get_task_state(cpu);
    task_settid(ts);
    info->tid = ts->ts_tid;
    if (info->child_tidptr) {
        put_user_u32(info->tid, info->child_tidptr);
    }
    if (info->parent_tidptr) {
        put_user_u32(info->tid, info->parent_tidptr);
    }
    qemu_guest_random_seed_thread_part2(cpu->random_seed);
    /* Enable signals.  */
    pthread_sigmask(SIG_SETMASK, &info->sigmask, NULL);
    /* Signal to the parent that we're ready.  */
    qemu_sem_post(&info->sem);
    /* Wait until the parent has finished initializing the tls state.  */
    pthread_mutex_lock(&clone_lock);
    pthread_mutex_unlock(&clone_lock);
    cpu_loop(env);
    /* never exits */
    return NULL;
}

void clone_fork_start(void)
{
    pthread_mutex_lock(&clone_lock);
    pthread_mutex_lock(&futex_lock);
}

void clone_fork_end(bool child)
{
    if (child) {
        pthread_mutex_init(&futex_lock, NULL);
        QLIST_INIT(&futex_waiters);
        pthread_mutex_init(&clone_lock, NULL);
    } else {
        pthread_mutex_unlock(&futex_lock);
        pthread_mutex_unlock(&clone_lock);
    }
}

/* do_fork() Must return host values and target errnos (unlike most
   do_*() functions). */
static int do_fork(CPUArchState *env, unsigned int flags, abi_ulong newsp,
                   abi_ulong parent_tidptr, target_ulong newtls,
                   abi_ulong child_tidptr)
{
    CPUState *cpu = env_cpu(env);
    int ret;
    TaskState *ts;
    CPUState *new_cpu;
    CPUArchState *new_env;
    sigset_t sigmask;

    flags &= ~CLONE_IGNORED_FLAGS;

    /* Emulate vfork() with fork() */
    if (flags & CLONE_VFORK) {
        flags &= ~(CLONE_VFORK | CLONE_VM);
    }

    if (flags & CLONE_VM) {
        TaskState *parent_ts = get_task_state(cpu);
        new_thread_info info;
        pthread_attr_t attr;

        if (((flags & CLONE_THREAD_FLAGS) != CLONE_THREAD_FLAGS) ||
            (flags & CLONE_INVALID_THREAD_FLAGS)) {
            return -TARGET_EINVAL;
        }

        ts = g_new0(TaskState, 1);
        init_task_state(ts);

#ifdef TARGET_AARCH64
        /*
         * If GCS is enabled in the parent thread, it is also enabled
         * in the child thread, but with a newly allocated stack.
         */
        abi_long new_gcspr = 0;
        if (env->cp15.gcscr_el[0] & GCSCR_PCRSEL) {
            new_gcspr = gcs_new_stack(ts);
            if (new_gcspr == -1) {
                g_free(ts);
                return -TARGET_ENOMEM;
            }
        }
#endif

        /* Grab a mutex so that thread setup appears atomic.  */
        pthread_mutex_lock(&clone_lock);

        /*
         * If this is our first additional thread, we need to ensure we
         * generate code for parallel execution and flush old translations.
         * Do this now so that the copy gets CF_PARALLEL too.
         */
        begin_parallel_context(cpu);

        /* we create a new CPU instance. */
        new_env = cpu_copy(env);
        /* Init regs that differ from the parent.  */
        cpu_clone_regs_child(new_env, newsp, flags);
        cpu_clone_regs_parent(env, flags);
        new_cpu = env_cpu(new_env);
        new_cpu->opaque = ts;
        ts->bprm = parent_ts->bprm;
        ts->info = parent_ts->info;
        ts->signal_mask = parent_ts->signal_mask;

#ifdef TARGET_AARCH64
        ts->gcs_el0_locked = parent_ts->gcs_el0_locked;
        new_env->cp15.gcspr_el[0] = new_gcspr;
#endif

        if (flags & CLONE_CHILD_CLEARTID) {
            ts->child_tidptr = child_tidptr;
        }

        if (flags & CLONE_SETTLS) {
            cpu_set_tls(new_env, newtls);
        }

        memset(&info, 0, sizeof(info));
        qemu_sem_init(&info.sem, 0);
        info.env = new_env;
        if (flags & CLONE_CHILD_SETTID) {
            info.child_tidptr = child_tidptr;
        }
        if (flags & CLONE_PARENT_SETTID) {
            info.parent_tidptr = parent_tidptr;
        }

        ret = pthread_attr_init(&attr);
        ret = pthread_attr_setstacksize(&attr, NEW_STACK_SIZE);
        ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        /* It is not safe to deliver signals until the child has finished
           initializing, so temporarily block all signals.  */
        sigfillset(&sigmask);
        pthread_sigmask(SIG_BLOCK, &sigmask, &info.sigmask);
        cpu->random_seed = qemu_guest_random_seed_thread_part1();

        ret = pthread_create(&info.thread, &attr, clone_func, &info);

        pthread_sigmask(SIG_SETMASK, &info.sigmask, NULL);
        pthread_attr_destroy(&attr);
        if (ret == 0) {
            /* Wait for the child to initialize.  */
            qemu_sem_wait(&info.sem);
            ret = info.tid;
        } else {
            errno = ret;
            ret = -1;
            object_unparent(OBJECT(new_cpu));
            object_unref(OBJECT(new_cpu));
#ifdef TARGET_AARCH64
            if (ts->gcs_base) {
                target_munmap(ts->gcs_base, ts->gcs_size);
            }
#endif
            g_free(ts);
        }
        qemu_sem_destroy(&info.sem);
        pthread_mutex_unlock(&clone_lock);
    } else {
        /* if no CLONE_VM, we consider it is a fork */
        if (flags & CLONE_INVALID_FORK_FLAGS) {
            return -TARGET_EINVAL;
        }

        /* We can't support custom termination signals */
        if ((flags & CSIGNAL) != TARGET_SIGCHLD) {
            return -TARGET_EINVAL;
        }

#if !defined(__NR_pidfd_open) || !defined(TARGET_NR_pidfd_open)
        if (flags & CLONE_PIDFD) {
            return -TARGET_EINVAL;
        }
#endif

        /* Can not allow CLONE_PIDFD with CLONE_PARENT_SETTID */
        if ((flags & CLONE_PIDFD) && (flags & CLONE_PARENT_SETTID)) {
            return -TARGET_EINVAL;
        }

        if (block_signals()) {
            return -QEMU_ERESTARTSYS;
        }

        fork_start();
        ret = fork();
        if (ret == 0) {
            /* Child Process.  */
            cpu_clone_regs_child(env, newsp, flags);
            fork_end(ret);
            /* There is a race condition here.  The parent process could
               theoretically read the TID in the child process before the child
               tid is set.  This would require using either ptrace
               (not implemented) or having *_tidptr to point at a shared memory
               mapping.  We can't repeat the spinlock hack used above because
               the child process gets its own copy of the lock.  */
            if (flags & CLONE_CHILD_SETTID) {
                put_user_u32(getpid(), child_tidptr);
            }
            ts = get_task_state(cpu);
            if (flags & CLONE_SETTLS) {
                cpu_set_tls(env, newtls);
            }
            if (flags & CLONE_CHILD_CLEARTID) {
                ts->child_tidptr = child_tidptr;
            }
        } else {
            cpu_clone_regs_parent(env, flags);
            if (flags & CLONE_PARENT_SETTID) {
                put_user_u32(ret, parent_tidptr);
            }
            if (flags & CLONE_PIDFD) {
                int pid_fd = 0;
#if defined(__NR_pidfd_open) && defined(TARGET_NR_pidfd_open)
                int pid_child = ret;
                pid_fd = pidfd_open(pid_child, 0);
                if (pid_fd >= 0) {
                    qemu_set_cloexec(pid_fd);
                } else {
                    pid_fd = 0;
                }
#endif
                put_user_u32(pid_fd, parent_tidptr);
            }
            fork_end(ret);
        }
        g_assert(!cpu_in_exclusive_context(cpu));
    }
    return ret;
}
