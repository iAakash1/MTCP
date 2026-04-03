# Multithreaded TCP Server

A production-quality, multithreaded TCP server built from scratch in **C++17** using raw **POSIX APIs** — no frameworks, no abstractions, just systems programming.

---

## Architecture
```
                ┌─────────────────────────────────────────────────────────┐
                │                    SERVER PROCESS                       │
                │                                                         │
  Client ──►    │   ┌──────────┐   accept()    ┌───────────────────┐     │
  Client ──►    │   │ TcpServer│ ─────────────►│    Task Queue     │     │
  Client ──►    │   │ (listen) │               │  queue<int>       │     │
                │   └──────────┘               │  (mutex + condvar)│     │
                │        ▲                     └────────┬──────────┘     │
                │        │ SIGINT                       │                 │
                │        │ handler                      ▼                 │
                │        │                     ┌───────────────────┐     │
                │   Graceful                   │   Thread Pool     │     │
                │   Shutdown                   │  ┌─────┐ ┌─────┐ │     │
                │                              │  │ W-0 │ │ W-1 │ │     │
                │                              │  └─────┘ └─────┘ │     │
                │                              │  ┌─────┐ ┌─────┐ │     │
                │                              │  │ W-2 │ │ W-3 │ │     │
                │                              │  └─────┘ └─────┘ │     │
                │                              └───────────────────┘     │
                └─────────────────────────────────────────────────────────┘

  Producer:  Main thread calls accept() → pushes client fd into queue
  Consumer:  Worker threads dequeue fds → handle client I/O → close
```

The main thread acts as the **producer** — it runs an `accept()` loop and pushes incoming client file descriptors into a thread-safe queue. A fixed pool of **worker threads** act as **consumers**, blocking on a condition variable when idle and waking up to handle connections as they arrive. This decouples connection acceptance from request handling, so the main thread is never stalled by slow clients.

---

## Features

| Feature | Implementation |
|---|---|
| **POSIX Sockets** | Raw `socket()`, `bind()`, `listen()`, `accept()` |
| **Thread Pool** | `pthread_create`, `pthread_join`, configurable worker count |
| **Mutex** | `pthread_mutex_t` — protects shared task queue |
| **Condition Variable** | `pthread_cond_t` — workers sleep when idle, no busy-waiting |
| **Graceful Shutdown** | `SIGINT` handler drains queue, joins threads, closes sockets |
| **RAII** | Destructors handle socket and thread cleanup automatically |
| **Thread-Safe Logging** | Per-thread prefixed output: `[Thread <id>] handling client <fd>` |
| **SO_REUSEADDR** | Instant restarts without `Address already in use` errors |

---

## Project Structure
```
Multithreaded-TCP-Server/
├── src/
│   ├── main.cpp           # Entry point, accept loop (producer), SIGINT handler
│   ├── TcpServer.cpp      # POSIX socket lifecycle
│   └── ThreadPool.cpp     # Thread pool with producer-consumer queue
├── include/
│   ├── TcpServer.h
│   └── ThreadPool.h
├── tests/
│   └── stress_test.py     # 100-client concurrent stress test
├── Makefile               # C++17, -Wall -Wextra -Wpedantic
└── README.md
```

---

## Build & Run

### Prerequisites

- C++17 compiler (`g++` or `clang++`)
- POSIX-compliant OS (Linux / macOS)
- Python 3 (for stress test only)

### Build
```bash
make
```

### Run
```bash
./server
```

### Connect with netcat
```bash
nc localhost 8080
# Type a message and see it echoed back
```

### Stress test — 100 concurrent clients
```bash
python3 tests/stress_test.py
```

### Clean
```bash
make clean
```

### Graceful shutdown

Press `Ctrl+C` while the server is running:
```
[Signal] SIGINT received — initiating shutdown...
[Main] Shutdown flag set — exiting accept loop.
[ThreadPool] All 4 threads joined.
[TcpServer] Socket closed.
[Main] Server shut down cleanly. Goodbye!
```

---

## Performance

| Metric | Result |
|---|---|
| Concurrent clients | 100+ |
| Worker threads | 4 (configurable) |
| Avg response time | < 5ms per client |
| Memory per thread | ~1MB stack (default) |
| Shutdown | Clean, no resource leaks |

---

## Concepts

### Operating Systems

- Thread lifecycle management with `pthread_create` and `pthread_join`
- Mutual exclusion via `pthread_mutex_t`
- Condition variables: `pthread_cond_wait` / `pthread_cond_signal`
- Producer-consumer synchronization pattern
- Signal handling with `SIGINT`, `sigaction`, and `volatile sig_atomic_t`
- RAII-based resource management

### Networking

- TCP socket programming (connection-oriented, reliable)
- Full socket API usage: `socket()`, `bind()`, `listen()`, `accept()`
- Client communication with `send()` / `recv()`
- `SO_REUSEADDR` for rapid server restarts
- IPv4 addressing with `sockaddr_in` and `INADDR_ANY`

### Software Engineering

- Modular C++ design with clean header/source separation
- Structured error handling using exceptions and `perror()`
- Makefile build system with proper dependency tracking
- Automated concurrent stress testing