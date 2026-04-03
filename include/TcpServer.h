/**
 * @file TcpServer.h
 * @brief RAII-wrapped POSIX TCP Server class.
 *
 * Encapsulates the full lifecycle of a TCP server socket:
 *   socket() → bind() → listen() → accept()
 *
 * Design:
 *   - Single-responsibility: only manages the listening socket.
 *   - Thread safety: accept() is reentrant; each call returns a NEW fd.
 *   - RAII: destructor closes the socket to prevent fd leaks.
 */

#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <string>
#include <netinet/in.h>  // struct sockaddr_in

class TcpServer {
public:
    /**
     * Construct a TCP server bound to the given port.
     * @param port    Port number to listen on (e.g. 8080).
     * @param backlog Maximum length of the pending-connection queue.
     */
    TcpServer(int port, int backlog = 128);

    /**
     * Destructor — closes the listening socket if still open.
     */
    ~TcpServer();

    // ── Delete copy (socket fd is a unique resource) ──────────────────
    TcpServer(const TcpServer&)            = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    /**
     * Start listening for incoming connections.
     * Calls socket(), setsockopt(), bind(), listen() in sequence.
     * @throws std::runtime_error on any POSIX error.
     */
    void start();

    /**
     * Block until a client connects, then return the client socket fd.
     * @return client socket file descriptor (>= 0), or -1 on error.
     */
    int acceptConnection();

    /**
     * Explicitly close the listening socket.
     * Safe to call multiple times.
     */
    void close();

    /** @return the listening socket file descriptor. */
    int getFd() const { return server_fd_; }

private:
    int                port_;
    int                backlog_;
    int                server_fd_;
    struct sockaddr_in address_;
};

#endif // TCPSERVER_H
