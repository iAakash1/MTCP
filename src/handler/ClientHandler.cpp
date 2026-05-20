/**
 * @file ClientHandler.cpp
 * @brief Production-grade client connection handler.
 *
 * Replaces the original single-recv/close echo handler with a full
 * persistent-connection, framed, metrics-instrumented request loop.
 *
 * Connection lifecycle:
 *
 *   1. Socket hardening: set recv/send timeouts, TCP keepalive.
 *   2. Assign a unique monotonic connection ID (atomic counter).
 *   3. Log CONNECTED with peer IP:port.
 *   4. Send greeting line.
 *   5. Enter recvLine() loop:
 *        a. Recv one line (framed, no partial-read issues)
 *        b. Handle timeout → log + close
 *        c. Handle EOF → log + close
 *        d. Handle "quit" command → send goodbye + close
 *        e. Echo the line back with sendAll() (no partial-write issues)
 *        f. Update per-connection byte counters
 *   6. Update global Metrics atomics.
 *   7. Close socket.
 *   8. Log CLOSED with: peer, request count, bytes rx/tx, duration ms.
 *
 * This is the access-log pattern: one log line per connection at close time,
 * carrying all relevant stats.  nginx and HAProxy do the same.
 */

#include "handler/ClientHandler.h"
#include "net/SocketUtils.h"
#include "util/Logger.h"
#include "util/Metrics.h"

#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <string>
#include <atomic>

// ── Module-level static state (set once at startup, read-only after) ─────────
static ServerConfig g_cfg;

// ── Monotonic connection ID (atomic, lock-free) ──────────────────────────────
static std::atomic<uint64_t> g_connCounter{0};

void initClientHandler(const ServerConfig& cfg) {
    g_cfg = cfg;
}

// ── handleClient ─────────────────────────────────────────────────────────────
void handleClient(int clientFd, int workerIdx) {
    // ── Connection ID & identification ───────────────────────────────────────
    const uint64_t    connId    = g_connCounter.fetch_add(1, std::memory_order_relaxed);
    const std::string workerTag = "[worker-" + std::to_string(workerIdx) + "]";
    const std::string connTag   = "[conn#"   + std::to_string(connId)    + "]";
    const std::string peer      = net::getPeerAddress(clientFd);

    Logger::get().info(workerTag + " " + connTag +
                       " CONNECTED fd=" + std::to_string(clientFd) +
                       " from " + peer);

    // ── Update global metrics ─────────────────────────────────────────────────
    Metrics::get().totalConnections .fetch_add(1, std::memory_order_relaxed);
    Metrics::get().activeConnections.fetch_add(1, std::memory_order_relaxed);

    // ── Apply socket options ──────────────────────────────────────────────────
    //   These are applied per-connection (not on the listening socket) so they
    //   affect only data transfer, not the accept() path.
    if (g_cfg.recvTimeout > 0 && !net::setRecvTimeout(clientFd, g_cfg.recvTimeout))
        Logger::get().warn(workerTag + " " + connTag + " setRecvTimeout failed");

    if (g_cfg.sendTimeout > 0 && !net::setSendTimeout(clientFd, g_cfg.sendTimeout))
        Logger::get().warn(workerTag + " " + connTag + " setSendTimeout failed");

    if (g_cfg.keepAliveIdle > 0 && !net::setKeepAlive(clientFd, g_cfg.keepAliveIdle))
        Logger::get().warn(workerTag + " " + connTag + " setKeepAlive failed");

    // ── Connection start time (monotonic for duration measurement) ────────────
    struct timespec startTs{};
    clock_gettime(CLOCK_MONOTONIC, &startTs);

    // ── Per-connection stats ──────────────────────────────────────────────────
    uint64_t bytesRx  = 0;
    uint64_t bytesTx  = 0;
    int      requests = 0;
    bool     hadError = false;

    // ── Send greeting ─────────────────────────────────────────────────────────
    {
        const std::string greeting =
            "MTCP/" + std::to_string(connId) +
            " ready — send lines, 'quit' to disconnect\r\n";
        ssize_t s = net::sendAll(clientFd, greeting.c_str(), greeting.size());
        if (s < 0) {
            Logger::get().error(workerTag + " " + connTag +
                                " send greeting failed: " + strerror(errno));
            Metrics::get().totalErrors.fetch_add(1, std::memory_order_relaxed);
            hadError = true;
            goto close_connection;
        }
        bytesTx += static_cast<uint64_t>(s);
    }

    // ── Request-response loop (persistent connection) ─────────────────────────
    {
        char lineBuf[4096];

        while (true) {
            ssize_t lineLen = net::recvLine(clientFd, lineBuf, sizeof(lineBuf));

            if (lineLen < 0) {
                // Distinguish timeout from real errors
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    Logger::get().warn(workerTag + " " + connTag +
                                       " recv timeout (" +
                                       std::to_string(g_cfg.recvTimeout) +
                                       "s) — closing idle connection");
                } else if (errno == ECONNRESET) {
                    Logger::get().info(workerTag + " " + connTag +
                                       " connection reset by peer");
                } else {
                    Logger::get().error(workerTag + " " + connTag +
                                        " recv error: " + strerror(errno));
                    Metrics::get().totalErrors.fetch_add(1, std::memory_order_relaxed);
                    hadError = true;
                }
                break;
            }

            if (lineLen == 0) {
                // EOF: client closed connection cleanly
                Logger::get().info(workerTag + " " + connTag +
                                   " EOF — client disconnected");
                break;
            }

            bytesRx += static_cast<uint64_t>(lineLen);
            ++requests;

            const std::string line(lineBuf);

            if (g_cfg.verbose)
                Logger::get().debug(workerTag + " " + connTag +
                                    " RX[" + std::to_string(requests) + "]: " + line);

            // ── Quit command ──────────────────────────────────────────────────
            if (line == "quit" || line == "QUIT" ||
                line == "exit" || line == "EXIT") {
                const std::string bye = "Goodbye!\r\n";
                net::sendAll(clientFd, bye.c_str(), bye.size());
                break;
            }

            // ── Echo response with request number ─────────────────────────────
            std::string response =
                "Echo[" + std::to_string(requests) + "]: " + line + "\r\n";
            ssize_t s = net::sendAll(clientFd, response.c_str(), response.size());
            if (s < 0) {
                if (errno == EPIPE || errno == ECONNRESET) {
                    Logger::get().info(workerTag + " " + connTag +
                                       " client disconnected mid-send");
                } else {
                    Logger::get().error(workerTag + " " + connTag +
                                        " send error: " + strerror(errno));
                    Metrics::get().totalErrors.fetch_add(1, std::memory_order_relaxed);
                    hadError = true;
                }
                break;
            }
            bytesTx += static_cast<uint64_t>(s);
        }
    }

close_connection:
    // ── Calculate connection duration ─────────────────────────────────────────
    struct timespec endTs{};
    clock_gettime(CLOCK_MONOTONIC, &endTs);
    const double durMs =
        (endTs.tv_sec  - startTs.tv_sec)  * 1000.0 +
        (endTs.tv_nsec - startTs.tv_nsec) / 1.0e6;

    // ── Flush metrics atomics ─────────────────────────────────────────────────
    Metrics::get().totalBytesRx    .fetch_add(bytesRx, std::memory_order_relaxed);
    Metrics::get().totalBytesTx    .fetch_add(bytesTx, std::memory_order_relaxed);
    Metrics::get().activeConnections.fetch_sub(1,       std::memory_order_relaxed);

    // ── Close socket ──────────────────────────────────────────────────────────
    ::close(clientFd);

    // ── Access log (nginx-style: one line per connection at close) ────────────
    Logger::get().info(
        workerTag + " " + connTag +
        " CLOSED" +
        " fd="       + std::to_string(clientFd) +
        " peer="     + peer +
        " reqs="     + std::to_string(requests) +
        " rx="       + std::to_string(bytesRx) + "B" +
        " tx="       + std::to_string(bytesTx) + "B" +
        " dur="      + std::to_string(static_cast<int>(durMs)) + "ms" +
        (hadError ? " [error]" : "")
    );
}
