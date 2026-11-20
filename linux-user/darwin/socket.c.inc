/* SPDX-License-Identifier: GPL-2.0-or-later */
static int host_socket_type(int type)
{
    if (type & ~(0xf | 0x800 | 0x80000)) {
        errno = EINVAL;
        return -1;
    }
    if ((type & 0xf) < 1 || (type & 0xf) > 5) {
        errno = ESOCKTNOSUPPORT;
        return -1;
    }
    return type & 0xf;
}
static void socket_flags(int fd, int type)
{
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    if (type & 0x800) {
        fcntl(fd, F_SETFL, O_NONBLOCK);
    }
    if (type & 0x80000) {
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
}
static int host_socket_family(int family)
{
    switch (family) {
    case 1:
        return AF_UNIX;
    case 2:
        return AF_INET;
    case 10:
        return AF_INET6;
    default:
        errno = EAFNOSUPPORT;
        return -1;
    }
}
static abi_long get_sockaddr(struct sockaddr_storage *host, abi_ulong addr,
                             unsigned len)
{
    uint8_t buf[sizeof(*host)];
    if (len < 2 || len > sizeof(buf)) {
        return -TARGET_EINVAL;
    }
    if (copy_from_user(buf, addr, len)) {
        return -TARGET_EFAULT;
    }
    uint16_t target;
    memcpy(&target, buf, 2);
    int family = host_socket_family(tswap16(target));
    if (family < 0) {
        return get_errno(-1);
    }
    if ((family == AF_INET && len < sizeof(struct sockaddr_in)) ||
        (family == AF_INET6 && len < sizeof(struct sockaddr_in6))) {
        return -TARGET_EINVAL;
    }
    memset(host, 0, sizeof(*host));
    memcpy(host, buf, len);
    host->ss_len = len;
    host->ss_family = family;
    return 0;
}
static abi_long put_sockaddr(abi_ulong addr, abi_ulong len_addr,
                             struct sockaddr_storage *host, socklen_t len)
{
    uint32_t capacity;
    uint16_t family;
    if (get_user_u32(capacity, len_addr)) {
        return -TARGET_EFAULT;
    }
    switch (host->ss_family) {
    case AF_UNIX:
        family = 1;
        break;
    case AF_INET:
        family = 2;
        break;
    case AF_INET6:
        family = 10;
        break;
    default:
        return -TARGET_EAFNOSUPPORT;
    }
    family = tswap16(family);
    memcpy(host, &family, 2);
    if (copy_to_user(addr, host, MIN(capacity, len)) ||
        put_user_u32(len, len_addr)) {
        return -TARGET_EFAULT;
    }
    return 0;
}
static int host_msg_flags(int flags)
{
    int host = 0;
    if (flags & 1) {
        host |= MSG_OOB;
    }
    if (flags & 2) {
        host |= MSG_PEEK;
    }
    if (flags & 4) {
        host |= MSG_DONTROUTE;
    }
    if (flags & 0x40) {
        host |= MSG_DONTWAIT;
    }
    if (flags & 0x80) {
        host |= MSG_EOR;
    }
    if (flags & 0x100) {
        host |= MSG_WAITALL;
    }
    if (flags & ~(1 | 2 | 4 | 0x40 | 0x80 | 0x100 | 0x4000 | 0x8000)) {
        errno = EOPNOTSUPP;
        return -1;
    }
    return host;
}
static abi_long guest_sendrecv(int fd, abi_ulong addr, abi_ulong len, int flags,
                               abi_ulong peer, abi_ulong peer_len, bool sending)
{
    struct sockaddr_storage host;
    socklen_t host_len = sizeof(host);
    abi_long ret;
    int native_flags = host_msg_flags(flags);
    if (native_flags < 0) {
        return get_errno(-1);
    }
    if (len > ((abi_ulong)-1 >> 1)) {
        return -TARGET_EINVAL;
    }
    len = MIN(len, 16 * 1024 * 1024);
    void *guest =
        lock_user(sending ? VERIFY_READ : VERIFY_WRITE, addr, len, sending);
    if (!guest && len) {
        return -TARGET_EFAULT;
    }
    g_autofree void *bounce = g_try_malloc(len ?: 1);
    if (!bounce) {
        unlock_user(guest, addr, 0);
        return -TARGET_ENOMEM;
    }
    if (sending) {
        if (peer) {
            ret = get_sockaddr(&host, peer, peer_len);
            if (ret) {
                unlock_user(guest, addr, 0);
                return ret;
            }
        }
        memcpy(bounce, guest, len);
        ret = get_errno(sendto(fd, bounce, len, native_flags,
                               peer ? (struct sockaddr *)&host : NULL,
                               peer ? peer_len : 0));
    } else {
        ret = get_errno(recvfrom(fd, bounce, len, native_flags,
                                 peer ? (struct sockaddr *)&host : NULL,
                                 peer ? &host_len : NULL));
        if (ret >= 0 && peer) {
            abi_long error = put_sockaddr(peer, peer_len, &host, host_len);
            if (error) {
                ret = error;
            }
        }
        if (ret > 0) {
            memcpy(guest, bounce, ret);
        }
    }
    unlock_user(guest, addr, !sending && ret > 0 ? ret : 0);
    return ret;
}
static abi_long guest_sockopt(int fd, int level, int option, abi_ulong addr,
                              abi_ulong len_arg, bool setting)
{
    int host_option = -1;
    if (level == 1) {
        level = SOL_SOCKET;
        switch (option) {
        case 2:
            host_option = SO_REUSEADDR;
            break;
        case 3:
            host_option = SO_TYPE;
            break;
        case 4:
            host_option = SO_ERROR;
            break;
        case 5:
            host_option = SO_DONTROUTE;
            break;
        case 6:
            host_option = SO_BROADCAST;
            break;
        case 7:
            host_option = SO_SNDBUF;
            break;
        case 8:
            host_option = SO_RCVBUF;
            break;
        case 9:
            host_option = SO_KEEPALIVE;
            break;
        case 10:
            host_option = SO_OOBINLINE;
            break;
        case 15:
            host_option = SO_REUSEPORT;
            break;
        }
    } else if (level == IPPROTO_TCP && option == 1) {
        host_option = TCP_NODELAY;
    }
    if (host_option < 0) {
        return -TARGET_ENOPROTOOPT;
    }
    uint32_t value, len;
    if (setting) {
        if (len_arg < 4) {
            return -TARGET_EINVAL;
        }
        if (get_user_u32(value, addr)) {
            return -TARGET_EFAULT;
        }
        return get_errno(
            setsockopt(fd, level, host_option, &value, sizeof(value)));
    }
    if (get_user_u32(len, len_arg)) {
        return -TARGET_EFAULT;
    }
    socklen_t size = sizeof(value);
    if (getsockopt(fd, level, host_option, &value, &size)) {
        return get_errno(-1);
    }
    if (level == SOL_SOCKET && host_option == SO_ERROR) {
        value = host_to_target_errno(value);
    }
    uint32_t target = tswap32(value);
    if (copy_to_user(addr, &target, MIN(len, 4)) ||
        put_user_u32(MIN(len, 4), len_arg)) {
        return -TARGET_EFAULT;
    }
    return 0;
}

static abi_long guest_cmsg_to_host(const char *guest, size_t size,
                                   struct msghdr *host)
{
    size_t used = 0;

    for (size_t offset = 0; offset + sizeof(struct target_cmsghdr) <= size;) {
        const struct target_cmsghdr *src = (const void *)(guest + offset);
        size_t len = tswapal(src->cmsg_len);
        size_t bytes;
        struct cmsghdr *dst;

        if (len < sizeof(*src) || len > size - offset) {
            return -TARGET_EINVAL;
        }
        bytes = len - sizeof(*src);
        if (tswap32(src->cmsg_level) != 1 ||
            tswap32(src->cmsg_type) != 1 || bytes % sizeof(int)) {
            return -TARGET_EOPNOTSUPP;
        }
        if (CMSG_SPACE(bytes) > host->msg_controllen - used) {
            return -TARGET_EINVAL;
        }
        dst = (void *)((char *)host->msg_control + used);
        dst->cmsg_len = CMSG_LEN(bytes);
        dst->cmsg_level = SOL_SOCKET;
        dst->cmsg_type = SCM_RIGHTS;
        for (size_t i = 0; i < bytes; i += sizeof(int)) {
            int fd = ldl_p((const char *)(src + 1) + i);
            fd = tswap32(fd);
            memcpy((char *)CMSG_DATA(dst) + i, &fd, sizeof(fd));
        }
        used += CMSG_SPACE(bytes);
        offset += TARGET_CMSG_ALIGN(len);
    }
    host->msg_controllen = used;
    return 0;
}

static void close_message_fds(struct msghdr *host)
{
    for (struct cmsghdr *c = CMSG_FIRSTHDR(host); c;
         c = CMSG_NXTHDR(host, c)) {
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
            size_t bytes = c->cmsg_len - CMSG_LEN(0);
            for (size_t i = 0; i + sizeof(int) <= bytes; i += sizeof(int)) {
                int fd;
                memcpy(&fd, (char *)CMSG_DATA(c) + i, sizeof(fd));
                if (fd >= 0) {
                    close(fd);
                }
            }
        }
    }
}

static size_t guest_cmsg_from_host(char *guest, size_t capacity,
                                    struct msghdr *host, unsigned *flags,
                                    bool cloexec)
{
    size_t used = 0;

    for (struct cmsghdr *c = CMSG_FIRSTHDR(host); c;
         c = CMSG_NXTHDR(host, c)) {
        if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS) {
            *flags |= 8; /* Linux MSG_CTRUNC */
            continue;
        }
        size_t bytes = c->cmsg_len - CMSG_LEN(0);
        size_t count = bytes / sizeof(int);
        size_t available = capacity - used;
        size_t fit = available > sizeof(struct target_cmsghdr)
            ? MIN(count, (available - sizeof(struct target_cmsghdr)) / 4) : 0;
        struct target_cmsghdr *dst = fit ? (void *)(guest + used) : NULL;

        if (fit) {
            size_t len = sizeof(*dst) + fit * 4;
            dst->cmsg_len = tswapal(len);
            dst->cmsg_level = tswap32(1);
            dst->cmsg_type = tswap32(1);
            for (size_t i = 0; i < fit; i++) {
                int fd;
                memcpy(&fd, (char *)CMSG_DATA(c) + i * 4, sizeof(fd));
                if (cloexec) {
                    fcntl(fd, F_SETFD, FD_CLOEXEC);
                }
                stl_p((char *)(dst + 1) + i * 4, tswap32(fd));
            }
            used += MIN(TARGET_CMSG_ALIGN(len), available);
        }
        if (fit < count) {
            *flags |= 8;
            for (size_t i = fit; i < count; i++) {
                int fd, invalid = -1;
                memcpy(&fd, (char *)CMSG_DATA(c) + i * 4, sizeof(fd));
                close(fd);
                memcpy((char *)CMSG_DATA(c) + i * 4, &invalid, sizeof(invalid));
            }
        }
    }
    return used;
}

static abi_long guest_message(int fd, abi_ulong addr, unsigned flags,
                               bool sending)
{
    struct target_msghdr target;
    struct msghdr host = { 0 };
    struct sockaddr_storage peer;
    struct target_iovec *iov = NULL;
    struct iovec *native_iov = NULL;
    void **guest_iov = NULL;
    void *guest_control = NULL;
    abi_ulong iov_addr, control_addr;
    size_t count, capacity, total = 0;
    abi_long ret = -TARGET_EFAULT;
    bool received = false;
    bool cloexec = !sending && (flags & 0x40000000);
    int native_flags = host_msg_flags(flags & ~(!sending ? 0x40000000 : 0));

    if (native_flags < 0) {
        return get_errno(-1);
    }
    if (copy_from_user(&target, addr, sizeof(target)) ||
        (!sending && !access_ok(thread_cpu,
                               VERIFY_WRITE, addr, sizeof(target)))) {
        return -TARGET_EFAULT;
    }
    count = tswapal(target.msg_iovlen);
    capacity = tswapal(target.msg_controllen);
    iov_addr = tswapal(target.msg_iov);
    control_addr = tswapal(target.msg_control);
    if (count > IOV_MAX || capacity > 1024 * 1024) {
        return -TARGET_EINVAL;
    }
    iov = lock_user(VERIFY_READ, iov_addr, count * sizeof(*iov), 1);
    if (!iov && count) {
        return -TARGET_EFAULT;
    }
    native_iov = g_new0(struct iovec, count);
    guest_iov = g_new0(void *, count);
    host.msg_iov = native_iov;
    host.msg_iovlen = count;
    for (size_t i = 0; i < count; i++) {
        size_t len = tswapal(iov[i].iov_len);
        if (len > 16 * 1024 * 1024 - total) {
            ret = -TARGET_EINVAL;
            goto out;
        }
        total += len;
        guest_iov[i] = lock_user(sending ? VERIFY_READ : VERIFY_WRITE,
                                tswapal(iov[i].iov_base), len, sending);
        if (!guest_iov[i] && len) {
            goto out;
        }
        native_iov[i].iov_len = len;
        native_iov[i].iov_base = g_malloc(len ?: 1);
        if (sending && len) {
            memcpy(native_iov[i].iov_base, guest_iov[i], len);
        }
    }
    if (capacity) {
        guest_control = lock_user(sending ? VERIFY_READ : VERIFY_WRITE,
                                  control_addr, capacity, sending);
        if (!guest_control) {
            goto out;
        }
        host.msg_controllen = capacity * 2 + 64;
        host.msg_control = g_malloc0(host.msg_controllen);
        if (sending) {
            ret = guest_cmsg_to_host(guest_control, capacity, &host);
            if (ret) {
                goto out;
            }
        }
    }
    if (!sending && !capacity) {
        /*
         * Darwin does not report discarded rights with a NULL control area.
         * Receive them explicitly so Linux MSG_CTRUNC and close semantics
         * remain correct even when the guest supplies no control buffer.
         */
        host.msg_controllen = CMSG_SPACE(1024);
        host.msg_control = g_malloc0(host.msg_controllen);
    }
    if (target.msg_name) {
        host.msg_name = &peer;
        host.msg_namelen = sizeof(peer);
        if (sending) {
            ret = get_sockaddr(&peer, tswapal(target.msg_name),
                               tswap32(target.msg_namelen));
            if (ret) {
                goto out;
            }
            host.msg_namelen = tswap32(target.msg_namelen);
        }
    }
    ret = get_errno(sending ? sendmsg(fd, &host, native_flags)
                            : recvmsg(fd, &host, native_flags));
    if (!sending && ret >= 0) {
        abi_long remaining = ret;
        unsigned output_flags = 0;
        received = true;
        for (size_t i = 0; i < count; i++) {
            size_t bytes = MIN(remaining, native_iov[i].iov_len);
            if (bytes) {
                memcpy(guest_iov[i], native_iov[i].iov_base, bytes);
            }
            remaining -= bytes;
        }
        if (host.msg_flags & MSG_OOB) {
            output_flags |= 1;
        }
        if (host.msg_flags & MSG_CTRUNC) {
            output_flags |= 8;
        }
        if (host.msg_flags & MSG_TRUNC) {
            output_flags |= 0x20;
        }
        if (host.msg_flags & MSG_EOR) {
            output_flags |= 0x80;
        }
        target.msg_controllen = tswapal(guest_cmsg_from_host(guest_control,
            capacity, &host, &output_flags, cloexec));
        target.msg_flags = tswap32(output_flags);
        if (target.msg_name) {
            abi_long error = put_sockaddr(tswapal(target.msg_name),
                addr + offsetof(struct target_msghdr, msg_namelen),
                &peer, host.msg_namelen);
            if (error) {
                ret = error;
                goto out;
            }
            target.msg_namelen = tswap32(host.msg_namelen);
        }
        if (copy_to_user(addr, &target, sizeof(target))) {
            ret = -TARGET_EFAULT;
        }
    }
 out:
    if (received && ret < 0) {
        close_message_fds(&host);
    }
    if (guest_control) {
        unlock_user(guest_control, control_addr,
                    !sending && ret >= 0 ? tswapal(target.msg_controllen) : 0);
    }
    for (size_t i = 0, remaining = ret > 0 ? ret : 0; i < count; i++) {
        size_t bytes = MIN(remaining, native_iov[i].iov_len);
        if (guest_iov[i]) {
            unlock_user(guest_iov[i], tswapal(iov[i].iov_base),
                        !sending && ret >= 0 ? bytes : 0);
        }
        remaining -= bytes;
        g_free(native_iov[i].iov_base);
    }
    unlock_user(iov, iov_addr, 0);
    g_free(native_iov);
    g_free(guest_iov);
    g_free(host.msg_control);
    return ret;
}
