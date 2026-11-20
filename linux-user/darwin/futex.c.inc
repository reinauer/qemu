/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Process-local Linux futex waits.  Process-shared futexes are unsupported. */
typedef struct FutexWaiter {
    abi_ulong addr;
    uint32_t mask;
    bool woken;
    pthread_cond_t cond;
    QLIST_ENTRY(FutexWaiter) entry;
} FutexWaiter;
static QLIST_HEAD(, FutexWaiter)
    futex_waiters = QLIST_HEAD_INITIALIZER(futex_waiters);
static pthread_mutex_t futex_lock = PTHREAD_MUTEX_INITIALIZER;

static int futex_wake(abi_ulong addr, unsigned count, uint32_t mask)
{
    FutexWaiter *w;
    int woke = 0;
    pthread_mutex_lock(&futex_lock);
    QLIST_FOREACH(w, &futex_waiters, entry)
    {
        if ((unsigned)woke >= count) {
            break;
        }
        if (!w->woken && w->addr == addr && (w->mask & mask)) {
            w->woken = true;
            woke++;
            pthread_cond_signal(&w->cond);
        }
    }
    pthread_mutex_unlock(&futex_lock);
    return woke;
}

static abi_long guest_futex(abi_ulong addr, int op, uint32_t val,
                            abi_ulong timeout_addr, uint32_t mask)
{
    int command = op & 0x7f;
    struct timespec deadline = {0}, rel, now;
    bool timed = timeout_addr != 0;
    int clock = command == 9 && (op & 0x100) ? CLOCK_REALTIME : CLOCK_MONOTONIC;
    abi_long ret = 0;
    uint32_t *word;
    if (addr & 3) {
        return -TARGET_EINVAL;
    }
    if (op & ~0x1ff) {
        return -TARGET_ENOSYS;
    }
    if (command != 9 && command != 10) {
        mask = UINT32_MAX;
    }
    if (!mask) {
        return -TARGET_EINVAL;
    }
    if (command == 1 || command == 10) {
        return futex_wake(addr, val, mask);
    }
    if (command != 0 && command != 9) {
        return -TARGET_ENOSYS;
    }
    if (timed) {
        ret = get_timespec(&deadline, timeout_addr);
        if (ret) {
            return ret;
        }
        if (command == 0) {
            clock_gettime(clock, &now);
            deadline.tv_sec += now.tv_sec;
            deadline.tv_nsec += now.tv_nsec;
            if (deadline.tv_nsec >= 1000000000) {
                deadline.tv_sec++;
                deadline.tv_nsec -= 1000000000;
            }
        }
    }
    word = lock_user(VERIFY_READ, addr, 4, 1);
    if (!word) {
        return -TARGET_EFAULT;
    }
    FutexWaiter waiter = {.addr = addr, .mask = mask};
    pthread_cond_init(&waiter.cond, NULL);
    pthread_mutex_lock(&futex_lock);
    if (qatomic_read(word) != tswap32(val)) {
        ret = -TARGET_EAGAIN;
        goto done;
    }
    QLIST_INSERT_HEAD(&futex_waiters, &waiter, entry);
    while (!waiter.woken) {
        if (qatomic_read(&get_task_state(thread_cpu)->signal_pending)) {
            ret = -QEMU_ERESTARTSYS;
            break;
        }
        clock_gettime(clock, &now);
        rel = (struct timespec){.tv_sec = 0, .tv_nsec = 20000000};
        if (timed) {
            time_t sec = deadline.tv_sec - now.tv_sec;
            long nsec = deadline.tv_nsec - now.tv_nsec;
            if (nsec < 0) {
                sec--;
                nsec += 1000000000;
            }
            if (sec < 0 || (!sec && !nsec)) {
                ret = -TARGET_ETIMEDOUT;
                break;
            }
            if (!sec && nsec < rel.tv_nsec) {
                rel.tv_nsec = nsec;
            }
        }
        /* Poll pending guest signals as condvar waits are not EINTR-able. */
        pthread_cond_timedwait_relative_np(&waiter.cond, &futex_lock, &rel);
    }
    QLIST_REMOVE(&waiter, entry);
done:
    pthread_mutex_unlock(&futex_lock);
    pthread_cond_destroy(&waiter.cond);
    unlock_user(word, addr, 0);
    return ret;
}
