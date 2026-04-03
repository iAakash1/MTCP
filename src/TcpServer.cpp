/**
 * @file TcpServer.cpp
 * @brief Implementation of the RAII POSIX TCP Server.
 */

#include "TcpServer.h"

#include <iostream>
#include <cstring>       // memset
#include <stdexcept>     // std::runtime_error
#include <unistd.h>      // close()
#include <sys/socket.h>  // socket(), bind(), listen(), accept()
#include <arpa/inet.h>   // htons(), INADDR_ANY

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
TcpServer::TcpServer(int port, int backlog)
    : port_(port), backlog_(backlog), server_fd_(-1)
{
    // Zero out the address structure
    std::memset(&address_, 0, sizeof(address_));
    address_.sin_family      = AF_INET;         // IPv4
    address_.sin_addr.s_addr = INADDR_ANY;      // Bind to all interfaces
    address_.sin_port        = htons(port_);     // Host-to-network byte order
}

// ─────────────────────────────────────────────────────────────────────────────
// Destructor — RAII cleanup
// ─────────────────────────────────────────────────────────────────────────────
TcpServer::~TcpServer()
{
    close();
}

// ─────────────────────────────────────────────────────────────────────────────
// start() — socket → setsockopt → bind → listen
// ─────────────────────────────────────────────────────────────────────────────
void TcpServer::start()
{
    // ── Step 1: Create socket ────────────────────────────────────────────
    //   AF_INET     = IPv4
    //   SOCK_STREAM = TCP (reliable, connection-oriented)
    //   0           = let OS pick protocol (TCP for SOCK_STREAM)
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        throw std::runtime_error("socket() failed: " + std::string(strerror(errno)));
    }

    // ── Step 2: Set SO_REUSEADDR ─────────────────────────────────────────
    //   Why? After closing the server, the OS keeps the port in TIME_WAIT
    //   for ~60 seconds.  Without this, restarting immediately gives
    //   "Address already in use".  Critical for development & restarts.
    int opt = 1;
    if (::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        throw std::runtime_error("setsockopt(SO_REUSEADDR) failed: " + std::string(strerror(errno)));
    }

    // ── Step 3: Bind socket to address ───────────────────────────────────
    //   Associates the socket with the IP + port defined in constructor.
    if (::bind(server_fd_, reinterpret_cast<struct sockaddr*>(&address_), sizeof(address_)) < 0) {
        throw std::runtime_error("bind() failed on port " + std::to_string(port_) + ": " + std::string(strerror(errno)));
    }

    // ── Step 4: Mark socket as passive (listening) ───────────────────────
    //   backlog_ = max queued connections before OS starts refusing.
    if (::listen(server_fd_, backlog_) < 0) {
        throw std::runtime_error("listen() failed: " + std::string(strerror(errno)));
    }

    std::cout << "[TcpServer] Listening on port " << port_
              << " (backlog=" << backlog_ << ")\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// acceptConnection() — blocks until a client connects
// ─────────────────────────────────────────────────────────────────────────────
int TcpServer::acceptConnection()
{
    struct sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);

    // accept() blocks here until a client connects.
    // Returns a NEW file descriptor for the client connection.
    // The original server_fd_ continues listening.
    int client_fd = ::accept(server_fd_,
                             reinterpret_cast<struct sockaddr*>(&client_addr),
                             &client_len);

    if (client_fd < 0) {
        // Don't throw — accept can fail transiently (e.g., EINTR from signal).
        // Caller decides whether to retry or exit.
        perror("[TcpServer] accept() failed");
    }

    return client_fd;
}

// ─────────────────────────────────────────────────────────────────────────────
// close() — idempotent socket cleanup
// ─────────────────────────────────────────────────────────────────────────────
void TcpServer::close()
{
    if (server_fd_ >= 0) {
        ::close(server_fd_);
        server_fd_ = -1;
        std::cout << "[TcpServer] Socket closed.\n";
    }
}
