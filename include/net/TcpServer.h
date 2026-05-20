/**
 * @file TcpServer.h
 * @brief RAII-wrapped POSIX TCP listening socket.
 *
 * Single responsibility: manage the lifecycle of one server socket.
 *   socket() → setsockopt(SO_REUSEADDR, SO_REUSEPORT) → bind() → listen() → accept()
 *
 * Design decisions:
 *   - Copy constructor and copy assignment are deleted: a socket fd is a
 *     unique OS resource — it cannot be meaningfully copied.
 *   - The destructor calls close() automatically (RAII): the socket is
 *     reclaimed even if start() throws.
 *   - acceptConnection() returns -1 on EINTR (signal interrupt) — the caller
 *     checks g_shutdown and decides whether to retry or exit.
 *   - SO_REUSEPORT is set if available (Linux 3.9+): allows fast restart
 *     without waiting for TIME_WAIT to clear, and enables future multi-
 *     process deployments where multiple processes share the same port.
 */

#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <netinet/in.h>

class TcpServer {
public:
    /**
     * @param port    Port to listen on.
     * @param backlog listen() SYN queue depth.
     */
    explicit TcpServer(int port, int backlog = 128);
    ~TcpServer();

    // Deleted: file descriptor is a non-copyable OS resource
    TcpServer(const TcpServer&)            = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    /** socket() → setsockopt() → bind() → listen(). Throws on any failure. */
    void start();

    /**
     * Block until a client connects; returns new socket fd.
     * Returns -1 on error (including EINTR from signal — caller handles).
     */
    int acceptConnection();

    /** Close the listening socket. Idempotent; safe to call multiple times. */
    void close();

    int getFd() const { return server_fd_; }

private:
    int                port_;
    int                backlog_;
    int                server_fd_;
    struct sockaddr_in address_;
};

#endif // TCPSERVER_H
