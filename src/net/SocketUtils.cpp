/**
 * @file SocketUtils.cpp
 * @brief Implementation of production TCP socket helpers.
 */

#include "net/SocketUtils.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace net {

// ── sendAll ──────────────────────────────────────────────────────────────────
ssize_t sendAll(int fd, const void* buf, size_t len) {
    const char* ptr   = static_cast<const char*>(buf);
    size_t      total = 0;

    while (total < len) {
        // MSG_NOSIGNAL: if the peer has closed the connection, send() returns
        // -1 with errno=EPIPE instead of raising SIGPIPE (which kills process).
        ssize_t n = ::send(fd, ptr + total, len - total, MSG_NOSIGNAL);

        if (n < 0) {
            if (errno == EINTR) continue;   // interrupted by signal — retry
            return -1;                       // real error (EPIPE, ECONNRESET, …)
        }
        if (n == 0) break;                   // connection closed mid-send

        total += static_cast<size_t>(n);
    }

    return static_cast<ssize_t>(total);
}

// ── recvLine ─────────────────────────────────────────────────────────────────
ssize_t recvLine(int fd, char* buf, size_t maxLen) {
    if (maxLen == 0) return -1;

    size_t i = 0;
    char   c;

    while (i < maxLen - 1) {
        ssize_t n = ::recv(fd, &c, 1, 0);

        if (n < 0) {
            if (errno == EINTR) continue;   // signal interrupted — retry
            return -1;                       // error (EAGAIN on timeout, etc.)
        }
        if (n == 0) break;                   // EOF: peer closed connection

        if (c == '\n') break;               // end of line
        if (c == '\r') continue;            // skip CR in CRLF pairs

        buf[i++] = c;
    }

    buf[i] = '\0';
    return static_cast<ssize_t>(i);
}

// ── setRecvTimeout ───────────────────────────────────────────────────────────
bool setRecvTimeout(int fd, int seconds) {
    struct timeval tv{};
    tv.tv_sec  = seconds;
    tv.tv_usec = 0;
    return ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                        &tv, sizeof(tv)) == 0;
}

// ── setSendTimeout ───────────────────────────────────────────────────────────
bool setSendTimeout(int fd, int seconds) {
    struct timeval tv{};
    tv.tv_sec  = seconds;
    tv.tv_usec = 0;
    return ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                        &tv, sizeof(tv)) == 0;
}

// ── setKeepAlive ─────────────────────────────────────────────────────────────
bool setKeepAlive(int fd, int idleSecs) {
    int enable = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
                     &enable, sizeof(enable)) != 0)
        return false;

#ifdef TCP_KEEPIDLE
    // Linux-specific: tune keepalive timing
    // TCP_KEEPIDLE  : seconds idle before first probe
    // TCP_KEEPINTVL : seconds between subsequent probes
    // TCP_KEEPCNT   : number of probes before declaring dead
    int interval = 10;
    int count    = 5;
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE,  &idleSecs, sizeof(idleSecs));
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT,   &count,    sizeof(count));
#else
    (void)idleSecs;
#endif

    return true;
}

// ── getPeerAddress ────────────────────────────────────────────────────────────
std::string getPeerAddress(int fd) {
    struct sockaddr_in addr{};
    socklen_t len = sizeof(addr);

    if (::getpeername(fd, reinterpret_cast<struct sockaddr*>(&addr), &len) != 0)
        return "<unknown>";

    char ip[INET_ADDRSTRLEN];
    if (!::inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip)))
        return "<unknown>";

    return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
}

} // namespace net
