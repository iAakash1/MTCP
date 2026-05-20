/**
 * @file main.cpp
 * @brief MTCP Server — entry point. Wires all components, runs accept loop.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  ARCHITECTURE
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  ┌────────┐  accept()  ┌─────────────┐  enqueue(fd)  ┌─────────────────┐
 *  │ Client │ ─────────► │  TcpServer  │ ────────────► │   ThreadPool    │
 *  │        │            │  (listen)   │               │  worker-0 .. N  │
 *  └────────┘            └─────────────┘               └────────┬────────┘
 *                              │                                │
 *                          SIGINT                        handleClient(fd, idx)
 *                              │                         recvLine() loop
 *                              │                         sendAll() echo
 *                              ▼                                │
 *                        g_shutdown=1                    Metrics update
 *                              │
 *                              ▼
 *                   accept() → EINTR → loop exits
 *                              │
 *                        pool.shutdown()
 *                    (broadcast + drain + join)
 *                              │
 *                        server.close()
 *                              │
 *                       final metrics dump
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  SIGNAL HANDLING
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  SIGINT  (Ctrl+C):
 *    sigaction() — sets g_shutdown=1, write() is async-signal-safe.
 *    SA_RESTART deliberately NOT set: accept() returns EINTR on signal,
 *    allowing the accept loop to check g_shutdown and break cleanly.
 *
 *  SIGPIPE (broken pipe):
 *    signal(SIGPIPE, SIG_IGN) — process-wide ignore.
 *    sendAll() uses MSG_NOSIGNAL per-call as belt-and-suspenders.
 *    Without this, a single client disconnect during send() kills the server.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  STATS THREAD
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  A background pthread dumps Metrics every cfg.statsInterval seconds.
 *  Uses a 1s sleep loop (not a single long sleep) so it exits quickly
 *  when g_shutdown is set, without needing a condvar or pipe.
 */

#include "net/TcpServer.h"
#include "core/ThreadPool.h"
#include "core/Config.h"
#include "util/Logger.h"
#include "util/Metrics.h"
#include "handler/ClientHandler.h"

#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <cstring>
#include <stdexcept>
#include <string>

// ── Global shutdown flag (written by signal handler, read by main loop) ───────
static volatile sig_atomic_t g_shutdown = 0;

// ── SIGINT handler ────────────────────────────────────────────────────────────
//   Only async-signal-safe operations are permitted here:
//     - Setting a sig_atomic_t variable: ✅ safe
//     - write() to a file descriptor:    ✅ safe (POSIX guaranteed)
//     - printf / std::cout:              ❌ NOT safe (locks, buffering)
static void onSigInt(int /*signum*/) {
    g_shutdown = 1;
    const char msg[] = "\n[Signal] SIGINT — initiating graceful shutdown...\n";
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
#pragma GCC diagnostic pop
}

// ── Metrics stats thread ──────────────────────────────────────────────────────
struct StatsThreadArg { int intervalSecs; };

static void* statsThreadFn(void* arg) {
    const StatsThreadArg* sa = static_cast<StatsThreadArg*>(arg);
    int ticksSinceLastDump   = 0;

    // 1-second tick loop — allows fast exit when g_shutdown is set
    // without blocking in a long sleep that would delay final join
    while (!g_shutdown) {
        sleep(1);
        ++ticksSinceLastDump;
        if (!g_shutdown && ticksSinceLastDump >= sa->intervalSecs) {
            Metrics::get().dump();
            ticksSinceLastDump = 0;
        }
    }
    return nullptr;
}

// ── main ─────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    // ── Parse CLI configuration ───────────────────────────────────────────────
    ServerConfig cfg = parseArgs(argc, argv);

    // ── Logger setup (before any other component logs anything) ──────────────
    if (cfg.verbose)
        Logger::get().setLevel(LogLevel::DEBUG);

    // ── Print effective configuration ─────────────────────────────────────────
    printConfig(cfg);

    // ── SIGPIPE: ignore globally ──────────────────────────────────────────────
    //   sendAll() uses MSG_NOSIGNAL per-call, but SIG_IGN is belt-and-suspenders.
    //   Without both, a race between SIGPIPE delivery and MSG_NOSIGNAL could
    //   slip through on some kernel versions.
    signal(SIGPIPE, SIG_IGN);

    // ── SIGINT: graceful shutdown handler ─────────────────────────────────────
    //   sigaction() is preferred over signal() — it has defined semantics on
    //   all POSIX platforms and supports fine-grained flag control.
    struct sigaction sa{};
    sa.sa_handler = onSigInt;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;   // NO SA_RESTART — accept() must return EINTR on signal
    if (sigaction(SIGINT, &sa, nullptr) < 0) {
        Logger::get().error("[Main] sigaction(SIGINT) failed: " +
                            std::string(strerror(errno)));
        return 1;
    }

    // ── Initialize client handler with config ─────────────────────────────────
    initClientHandler(cfg);

    // ── Start TCP server ──────────────────────────────────────────────────────
    TcpServer server(cfg.port, cfg.backlog);
    try {
        server.start();
    } catch (const std::runtime_error& ex) {
        Logger::get().error("[Main] FATAL — " + std::string(ex.what()));
        return 1;
    }

    // ── Start thread pool ─────────────────────────────────────────────────────
    ThreadPool pool(cfg.threads, cfg.queueDepth, handleClient);

    // ── Start metrics dump thread ─────────────────────────────────────────────
    pthread_t    statsTid{};
    StatsThreadArg statsArg{cfg.statsInterval};
    if (cfg.statsInterval > 0) {
        pthread_create(&statsTid, nullptr, statsThreadFn, &statsArg);
        Logger::get().info("[Main] Stats thread started (interval=" +
                           std::to_string(cfg.statsInterval) + "s)");
    }

    Logger::get().info("[Main] Server ready — press Ctrl+C to stop\n");

    // ══════════════════════════════════════════════════════════════════════════
    // PRODUCER LOOP (main thread)
    // ══════════════════════════════════════════════════════════════════════════
    //
    //   The main thread is the PRODUCER in the producer-consumer pattern.
    //   It calls accept() in a tight loop, pushing each accepted fd into the
    //   ThreadPool's bounded queue.
    //
    //   accept() blocks until a client connects.  When SIGINT arrives:
    //     1. The OS delivers the signal — calls onSigInt()
    //     2. onSigInt() sets g_shutdown=1
    //     3. accept() is interrupted, returns -1 with errno=EINTR
    //     4. The loop checks g_shutdown and breaks
    //
    while (!g_shutdown) {
        int clientFd = server.acceptConnection();

        if (clientFd < 0) {
            if (g_shutdown) break;                    // SIGINT — expected
            if (errno == EINTR) continue;             // other signal — retry
            Logger::get().warn("[Main] accept() transient error: " +
                               std::string(strerror(errno)));
            continue;
        }

        // PRODUCER: push fd into shared queue
        // enqueue() handles backpressure: returns false + closes fd if full
        pool.enqueue(clientFd);
    }

    // ══════════════════════════════════════════════════════════════════════════
    // GRACEFUL SHUTDOWN
    // ══════════════════════════════════════════════════════════════════════════
    Logger::get().info("[Main] Shutdown: draining thread pool...");
    pool.shutdown();        // broadcast → workers drain queue → join all threads

    Logger::get().info("[Main] Shutdown: closing listening socket...");
    server.close();

    // Join stats thread (it will see g_shutdown and exit within 1 second)
    if (cfg.statsInterval > 0 && statsTid != 0)
        pthread_join(statsTid, nullptr);

    // Final metrics snapshot
    Metrics::get().dump();

    Logger::get().info("[Main] Clean shutdown complete. Goodbye!");
    return 0;
}
