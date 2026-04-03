/**
 * @file main.cpp
 * @brief Entry point — integrates TcpServer + ThreadPool + SIGINT handling.
 *
 * Architecture:
 *   ┌────────────┐     accept()     ┌─────────────┐    enqueue()    ┌────────────┐
 *   │   Client   │ ──────────────► │  TcpServer   │ ─────────────► │ ThreadPool │
 *   │ (external) │                 │ (listener)   │                │ (workers)  │
 *   └────────────┘                 └─────────────┘                └────────────┘
 *                                        │                              │
 *                                        │         clientHandler()      │
 *                                        │  ◄───────────────────────────│
 *                                        │         send/recv/close      │
 *
 * Signal Handling (SIGINT / Ctrl+C):
 *   - A signal handler sets a global flag (volatile sig_atomic_t).
 *   - The accept loop checks this flag each iteration.
 *   - On SIGINT: break accept loop → shutdown thread pool → close server.
 *
 *   Why volatile sig_atomic_t?
 *   ──────────────────────────
 *   - volatile: tells compiler "don't optimize reads of this variable;
 *     it can change at any time (from a signal handler)."
 *   - sig_atomic_t: guaranteed to be read/written atomically by the CPU,
 *     so no torn reads between the main thread and the signal handler.
 *
 *   Why is graceful shutdown important in production?
 *   ─────────────────────────────────────────────────
 *   1. Resource leaks: unclosed sockets exhaust file descriptors.
 *   2. Port exhaustion: TIME_WAIT sockets pile up.
 *   3. Data corruption: clients mid-transfer get broken pipes.
 *   4. Zombie threads: unjoined threads leak resources.
 *   5. Audit/compliance: graceful shutdown is a production requirement.
 */

#include "TcpServer.h"
#include "ThreadPool.h"

#include <iostream>
#include <cstring>       // strlen, memset
#include <unistd.h>      // close, read, write
#include <signal.h>      // signal, SIGINT
#include <pthread.h>     // pthread_self

// ═════════════════════════════════════════════════════════════════════════════
// Configuration
// ═════════════════════════════════════════════════════════════════════════════
static constexpr int    SERVER_PORT    = 8080;
static constexpr int    THREAD_COUNT   = 4;
static constexpr int    BUFFER_SIZE    = 4096;

// ═════════════════════════════════════════════════════════════════════════════
// Global shutdown flag (signal-safe)
// ═════════════════════════════════════════════════════════════════════════════
static volatile sig_atomic_t g_shutdown = 0;

/**
 * SIGINT handler — sets the shutdown flag.
 * Note: only async-signal-safe operations are allowed here.
 * Setting a sig_atomic_t variable IS safe.
 * std::cout / printf are NOT safe (but we avoid them).
 */
static void signalHandler(int signum)
{
    (void)signum;  // unused
    g_shutdown = 1;
    // write() is async-signal-safe, unlike printf/cout
    const char msg[] = "\n[Signal] SIGINT received — initiating shutdown...\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// Client Handler — runs inside a WORKER THREAD (consumer)
// ═════════════════════════════════════════════════════════════════════════════
/**
 * Handles a single client connection:
 *   1. Send a greeting
 *   2. Receive a message from the client
 *   3. Echo the message back
 *   4. Close the socket
 *
 * Thread-safe: each invocation operates on its own clientFd.
 * No shared state is accessed (no need for locks here).
 */
static void clientHandler(int clientFd)
{
    // ── Thread-safe logging ──────────────────────────────────────────
    //   pthread_self() returns the calling thread's ID.
    //   Each worker prints which client it's handling.
    pthread_t tid = pthread_self();
    std::cout << "[Thread " << tid << "] Handling client fd=" << clientFd << "\n";

    // ── Step 1: Send greeting ────────────────────────────────────────
    const char* greeting = "Hello from server! Send me a message:\n";
    ssize_t sent = ::send(clientFd, greeting, strlen(greeting), 0);
    if (sent < 0) {
        perror("[Handler] send greeting failed");
        ::close(clientFd);
        return;
    }

    // ── Step 2: Receive message from client ──────────────────────────
    char buffer[BUFFER_SIZE];
    std::memset(buffer, 0, sizeof(buffer));

    ssize_t bytesRead = ::recv(clientFd, buffer, sizeof(buffer) - 1, 0);
    if (bytesRead <= 0) {
        if (bytesRead == 0) {
            std::cout << "[Thread " << tid << "] Client fd=" << clientFd
                      << " disconnected.\n";
        } else {
            perror("[Handler] recv failed");
        }
        ::close(clientFd);
        return;
    }

    buffer[bytesRead] = '\0';
    std::cout << "[Thread " << tid << "] Received from fd=" << clientFd
              << ": " << buffer;

    // ── Step 3: Echo message back ────────────────────────────────────
    std::string echo = "Echo: " + std::string(buffer);
    ::send(clientFd, echo.c_str(), echo.size(), 0);

    // ── Step 4: Close the client socket ──────────────────────────────
    //   Each worker is responsible for closing its own client fd.
    //   Forgetting this would leak file descriptors.
    ::close(clientFd);
    std::cout << "[Thread " << tid << "] Closed client fd=" << clientFd << "\n";
}

// ═════════════════════════════════════════════════════════════════════════════
// Main — PRODUCER (accept loop)
// ═════════════════════════════════════════════════════════════════════════════
int main()
{
    // ── Register SIGINT handler ──────────────────────────────────────
    //   Overrides default behavior (which is to terminate immediately).
    //   Now Ctrl+C sets g_shutdown=1 instead.
    struct sigaction sa{};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // No SA_RESTART: we WANT accept() to be interrupted
    if (sigaction(SIGINT, &sa, nullptr) < 0) {
        perror("sigaction");
        return 1;
    }

    std::cout << "═══════════════════════════════════════════════════\n"
              << "  Multithreaded TCP Server (POSIX)                \n"
              << "  Port: " << SERVER_PORT << "  |  Workers: " << THREAD_COUNT << "\n"
              << "  Press Ctrl+C to shut down gracefully            \n"
              << "═══════════════════════════════════════════════════\n\n";

    // ── Create and start TCP server ──────────────────────────────────
    TcpServer server(SERVER_PORT);
    try {
        server.start();
    } catch (const std::runtime_error& e) {
        std::cerr << "[FATAL] " << e.what() << "\n";
        return 1;
    }

    // ── Create thread pool ───────────────────────────────────────────
    //   The handler function is passed to each worker thread.
    //   Workers will call clientHandler(fd) for each dequeued task.
    ThreadPool pool(THREAD_COUNT, clientHandler);

    // ════════════════════════════════════════════════════════════════
    // PRODUCER LOOP (main thread)
    // ════════════════════════════════════════════════════════════════
    //
    //   The main thread is the PRODUCER in the producer-consumer model.
    //   It accepts new client connections and pushes their socket fds
    //   into the thread pool's shared queue.
    //
    //   The CONSUMER worker threads pick up these fds and process them.
    //
    std::cout << "[Main] Waiting for connections...\n\n";

    while (!g_shutdown) {
        // ── accept() blocks until a client connects ──────────────────
        //   When SIGINT arrives, accept() returns -1 with errno=EINTR.
        //   We check g_shutdown and break the loop.
        int clientFd = server.acceptConnection();

        if (clientFd < 0) {
            if (g_shutdown) {
                std::cout << "[Main] Shutdown flag set — exiting accept loop.\n";
                break;
            }
            // Transient error (e.g., EINTR without shutdown) — retry
            continue;
        }

        // ── PRODUCER: push client socket into the shared queue ───────
        //   This is the "produce" step: we're adding work for consumers.
        pool.enqueue(clientFd);
    }

    // ════════════════════════════════════════════════════════════════
    // GRACEFUL SHUTDOWN
    // ════════════════════════════════════════════════════════════════
    std::cout << "\n[Main] Initiating graceful shutdown...\n";

    // 1. Shut down thread pool (signals all workers, joins threads)
    pool.shutdown();

    // 2. Close the listening socket
    server.close();

    std::cout << "[Main] Server shut down cleanly. Goodbye!\n";
    return 0;
}
