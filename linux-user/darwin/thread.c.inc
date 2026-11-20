/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Adapted from the current linux-user/syscall.c thread implementation. */

static pthread_mutex_t clone_lock = PTHREAD_MUTEX_INITIALIZER;

void clone_fork_start(void)
{
    pthread_mutex_lock(&clone_lock);
}

void clone_fork_end(bool child)
{
    if (child) {
        pthread_mutex_init(&clone_lock, NULL);
    } else {
        pthread_mutex_unlock(&clone_lock);
    }
}
