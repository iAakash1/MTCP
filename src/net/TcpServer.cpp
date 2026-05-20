/**
 * @file TcpServer.cpp
 * @brief POSIX TCP server socket lifecycle implementation.
 */

#include "net/TcpServer.h"
#include "util/Logger.h"

#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cerrno>

TcpServer::TcpServer(int port, int backlog)
    : port_(port), backlog_(backlog), server_fd_(-1)
{
    std::memset(&address_, 0, sizeof(address_));
    address_.sin_family      = AF_INET;
    address_.sin_addr.s_addr = INADDR_ANY;  // bind all interfaces
    address_.sin_port        = htons(port_); // host-to-network byte order
}

TcpServer::~TcpServer() {
    close();
}

void TcpServer::start() {
    // ── socket() ─────────────────────────────────────────────────────────────
    //   AF_INET     = IPv4
    //   SOCK_STREAM = TCP (reliable, ordered, connection-oriented)
    //   0           = OS picks protocol (TCP for SOCK_STREAM)
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0)
        throw std::runtime_error("socket() failed: " + std::string(strerror(errno)));

    // ── SO_REUSEADDR ──────────────────────────────────────────────────────────
    //   Without this, restarting the server within ~60s of the previous run
    //   gives "Address already in use" because the port is in TCP TIME_WAIT.
    //   TIME_WAIT lasts 2×MSL (Maximum Segment Lifetime, typically 60s) to
    //   ensure delayed duplicate packets from the old connection are discarded.
    int opt = 1;
    if (::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        throw std::runtime_error("setsockopt(SO_REUSEADDR) failed: " + std::string(strerror(errno)));

    // ── SO_REUSEPORT ──────────────────────────────────────────────────────────
    //   Linux 3.9+: multiple sockets can bind the same port. Each gets its own
    //   accept queue. Useful for multi-process setups and eliminates the
    //   thundering herd on a single accept() call.
    //   Guarded by ifdef for portability (not available on older kernels / macOS).
#ifdef SO_REUSEPORT
    if (::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
        Logger::get().warn("[TcpServer] SO_REUSEPORT not available: " +
                           std::string(strerror(errno)));
#endif

    // ── bind() ────────────────────────────────────────────────────────────────
    //   Associates the socket with the local address (INADDR_ANY:port).
    //   INADDR_ANY means: accept connections on any network interface.
    if (::bind(server_fd_,
               reinterpret_cast<struct sockaddr*>(&address_),
               sizeof(address_)) < 0)
        throw std::runtime_error("bind() failed on port " +
                                 std::to_string(port_) + ": " +
                                 std::string(strerror(errno)));

    // ── listen() ──────────────────────────────────────────────────────────────
    //   Transitions socket to passive mode.  backlog_ sets the kernel's SYN
    //   queue depth: the number of fully-established connections waiting to be
    //   accept()-ed.  Connections beyond this are silently dropped by the kernel
    //   (or RST'd, depending on /proc/sys/net/ipv4/tcp_abort_on_overflow).
    if (::listen(server_fd_, backlog_) < 0)
        throw std::runtime_error("listen() failed: " + std::string(strerror(errno)));

    Logger::get().info("[TcpServer] Listening on 0.0.0.0:" +
                       std::to_string(port_) +
                       " (backlog=" + std::to_string(backlog_) + ")");
}

int TcpServer::acceptConnection() {
    struct sockaddr_in client_addr{};
    socklen_t          client_len = sizeof(client_addr);

    // accept() blocks until a connection is fully established (SYN+SYN-ACK+ACK).
    // Returns a NEW socket fd for this specific connection.
    // server_fd_ continues listening for further connections.
    // Returns -1 with errno=EINTR when interrupted by SIGINT — caller checks
    // g_shutdown and decides whether to retry or exit the accept loop.
    int client_fd = ::accept(server_fd_,
                             reinterpret_cast<struct sockaddr*>(&client_addr),
                             &client_len);
    return client_fd;
}

void TcpServer::close() {
    if (server_fd_ >= 0) {
        ::close(server_fd_);
        server_fd_ = -1;
        Logger::get().info("[TcpServer] Listening socket closed");
    }
}
