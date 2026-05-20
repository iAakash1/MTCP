/**
 * @file ClientHandler.h
 * @brief Per-connection handler extracted from main.cpp into its own module.
 *
 * Handles the full lifecycle of one client connection:
 *   connect → apply socket options → greeting → request-response loop → close
 *
 * Features over the original single-recv echo:
 *
 *   Persistent connections:
 *     The handler loops on recvLine() until the client sends "quit", closes
 *     the connection, or a timeout occurs.  Multiple requests per connection
 *     demonstrates understanding of TCP as a persistent byte stream.
 *
 *   Proper framing:
 *     Uses net::recvLine() — reads exactly one newline-delimited line per
 *     iteration.  The original recv() approach could split or merge messages.
 *
 *   Correct send:
 *     Uses net::sendAll() with MSG_NOSIGNAL — handles partial writes, suppresses
 *     SIGPIPE.  The original send() silently truncated under load.
 *
 *   Connection lifecycle logging:
 *     Logs CONNECTED (with IP:port), every request in verbose mode, and
 *     CLOSED (with bytes rx/tx, request count, duration in ms).  This is the
 *     access log pattern used by nginx, Apache, and HAProxy.
 *
 *   Monotonic connection IDs:
 *     std::atomic<uint64_t> counter — each connection gets a unique sequential
 *     ID for log correlation.  Cross-referencing logs by conn# is standard
 *     operational practice.
 */

#ifndef CLIENTHANDLER_H
#define CLIENTHANDLER_H

#include "core/Config.h"

/**
 * Initialize the client handler with server config.
 * Call once before any handleClient() invocations.
 * Sets static configuration (timeouts, verbosity, keepalive).
 */
void initClientHandler(const ServerConfig& cfg);

/**
 * Handle a single accepted client connection.
 * Runs inside a worker thread — called by ThreadPool.
 *
 * @param clientFd   Accepted client socket (this function owns and closes it).
 * @param workerIdx  Sequential worker index (0..N-1) for structured logging.
 */
void handleClient(int clientFd, int workerIdx);

#endif // CLIENTHANDLER_H
