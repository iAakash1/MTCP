/**
 * @file SocketUtils.h
 * @brief Production-grade TCP socket utility functions.
 *
 * These utilities fix the three most common TCP socket bugs in student projects:
 *
 *   1. Partial sends:  send() may write fewer bytes than requested.
 *                      sendAll() loops until all bytes are delivered or errors.
 *
 *   2. SIGPIPE crashes: Writing to a closed socket raises SIGPIPE by default,
 *                        killing the process.  MSG_NOSIGNAL suppresses it.
 *
 *   3. No framing:     TCP is a byte stream.  recv() may return part of a
 *                       message or multiple messages concatenated.  recvLine()
 *                       reads exactly one newline-delimited line at a time.
 *
 * Additional helpers:
 *   - setRecvTimeout / setSendTimeout — prevent worker thread starvation by
 *     slow or zombie clients (SO_RCVTIMEO / SO_SNDTIMEO).
 *   - setKeepAlive — transport-layer dead connection detection (SO_KEEPALIVE).
 *   - getPeerAddress — human-readable "ip:port" string via inet_ntop.
 *
 * All functions use the namespace 'net' to avoid collisions with POSIX names.
 */

#ifndef SOCKETUTILS_H
#define SOCKETUTILS_H

#include <sys/types.h>
#include <string>

namespace net {

/**
 * sendAll() — guaranteed complete transmission.
 *
 * Loops calling send() until all 'len' bytes have been written or a
 * non-recoverable error occurs.  Uses MSG_NOSIGNAL on every send() call
 * to suppress SIGPIPE if the peer has closed the connection.
 *
 * @param fd   Connected socket file descriptor.
 * @param buf  Data to send.
 * @param len  Number of bytes to send.
 * @return     Total bytes sent (== len on success), or -1 on error.
 *             errno is set by the failing send() call.
 *
 * Why this matters:
 *   The kernel's send buffer may be full.  A single send() call may deliver
 *   only part of the data (returns n < len).  Without a retry loop, the
 *   client silently receives a truncated message — a latent correctness bug
 *   that surfaces only under load.
 */
ssize_t sendAll(int fd, const void* buf, size_t len);

/**
 * recvLine() — read one newline-delimited line from a TCP stream.
 *
 * Reads one byte at a time until '\n' (or '\r\n') is found, EOF, or
 * the buffer is full.  Strips the newline delimiter.  Null-terminates result.
 *
 * @param fd      Connected socket file descriptor.
 * @param buf     Output buffer.
 * @param maxLen  Maximum characters to write (including null terminator).
 * @return        Number of characters in the line (>= 0), or -1 on error.
 *                Returns 0 on EOF (peer closed connection).
 *
 * Why this matters:
 *   TCP is a byte stream.  A single recv() may return:
 *     - Half a line (sender split across packets)
 *     - Multiple lines concatenated (Nagle coalescing or send batching)
 *   recvLine() enforces application-level framing, which is the foundation
 *   of every text protocol: HTTP/1.1, SMTP, Redis RESP, FTP.
 */
ssize_t recvLine(int fd, char* buf, size_t maxLen);

/**
 * setRecvTimeout() — limit how long recv() will block.
 *
 * After 'seconds' with no data received, recv() returns -1 with
 * errno == EAGAIN or EWOULDBLOCK.
 *
 * Why this matters:
 *   A client that connects but never sends data holds a worker thread
 *   indefinitely.  With a 4-thread pool, 4 zombie clients starve all
 *   other connections.  A receive timeout is a fundamental DoS defence.
 */
bool setRecvTimeout(int fd, int seconds);

/**
 * setSendTimeout() — limit how long send() will block.
 *
 * Prevents a worker from blocking forever when the kernel send buffer
 * is full (e.g., a slow client with a small receive window).
 */
bool setSendTimeout(int fd, int seconds);

/**
 * setKeepAlive() — enable TCP keepalive probes.
 *
 * The kernel sends keepalive probes after 'idleSecs' of inactivity.
 * If no ACK is received after several probes, the connection is declared
 * dead and recv()/send() will return an error.
 *
 * Transport-layer keepalive is complementary to application-level timeouts:
 *   - SO_RCVTIMEO fires if no DATA arrives in N seconds.
 *   - TCP keepalive fires if the connection is IDLE (no bytes in either dir).
 */
bool setKeepAlive(int fd, int idleSecs = 60);

/**
 * getPeerAddress() — return "ip:port" string for a connected socket.
 *
 * Uses getpeername() + inet_ntop() (not the deprecated inet_ntoa()).
 * inet_ntop() is thread-safe and supports both IPv4 and IPv6.
 *
 * @return "192.168.1.1:54321" or "<unknown>" on failure.
 */
std::string getPeerAddress(int fd);

} // namespace net

#endif // SOCKETUTILS_H
