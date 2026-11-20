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
    if (len > SSIZE_MAX) {
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
