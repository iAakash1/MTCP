# 🔧 Multithreaded TCP Server

A production-quality, multithreaded TCP server built from scratch in **C++17** using **POSIX APIs** — no frameworks, no abstractions, just raw systems programming.

---

## 📐 Architecture

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

---

## ✨ Features

| Feature | Implementation |
|---|---|
| **POSIX Sockets** | Raw `socket()`, `bind()`, `listen()`, `accept()` |
| **pthreads** | `pthread_create`, `pthread_join`, `pthread_self` |
| **Mutex** | `pthread_mutex_t` — protects shared task queue |
| **Condition Variable** | `pthread_cond_t` — workers sleep when idle (no busy-wait) |
| **Producer-Consumer** | Main thread produces; worker threads consume |
| **RAII** | Destructors handle socket + thread cleanup |
| **Graceful Shutdown** | `SIGINT` handler → drain queue → join threads → close sockets |
| **Thread-Safe Logging** | `[Thread <id>] handling client <fd>` |
| **SO_REUSEADDR** | Instant restart without "Address already in use" |

---

## 📂 Project Structure

```
Multithreaded-TCP-Server/
├── src/
│   ├── main.cpp           # Entry point, accept loop (producer), SIGINT handler
│   ├── TcpServer.cpp      # POSIX socket lifecycle
│   └── ThreadPool.cpp     # Thread pool with producer-consumer pattern
├── include/
│   ├── TcpServer.h        # TcpServer class declaration
│   └── ThreadPool.h       # ThreadPool class declaration
├── tests/
│   └── stress_test.py     # 100-client concurrent stress test
├── Makefile               # Build system (C++17, -Wall -Wextra -Wpedantic)
└── README.md
```

---

## 🚀 Build & Run

### Prerequisites
- C++17 compiler (`g++` or `clang++`)
- POSIX-compliant OS (Linux / macOS)
- Python 3 (for stress test)

### Build
```bash
make
```

### Run
```bash
./server
```

### Test with netcat
```bash
# In another terminal:
nc localhost 8080
# Type a message → see the echo
```

### Stress Test (100 concurrent clients)
```bash
python3 tests/stress_test.py
```

### Clean
```bash
make clean
```

### Graceful Shutdown
```bash
# Press Ctrl+C while server is running
# Output:
#   [Signal] SIGINT received — initiating shutdown...
#   [Main] Shutdown flag set — exiting accept loop.
#   [ThreadPool] All 4 threads joined.
#   [TcpServer] Socket closed.
#   [Main] Server shut down cleanly. Goodbye!
```

---

## 🧪 Performance

| Metric | Result |
|---|---|
| Concurrent clients handled | **100+** |
| Worker threads | 4 (configurable) |
| Avg response time | < 5ms per client |
| Memory per thread | ~1MB stack (default) |
| Shutdown | Clean, no resource leaks |

---

## 🧠 Concepts Covered

### Operating Systems
- Thread creation and lifecycle (`pthread_create`, `pthread_join`)
- Mutual exclusion (`pthread_mutex_t`)
- Condition variables (`pthread_cond_wait`, `pthread_cond_signal`)
- Producer-Consumer synchronization pattern
- Signal handling (`SIGINT`, `sigaction`, `sig_atomic_t`)
- RAII resource management

### Networking
- TCP socket programming (connection-oriented)
- Socket API: `socket()`, `bind()`, `listen()`, `accept()`
- Client-server communication: `send()`, `recv()`
- `SO_REUSEADDR` for rapid server restarts
- IPv4 addressing (`sockaddr_in`, `INADDR_ANY`)

### Software Engineering
- Modular C++ design (separate header/source files)
- Clean error handling with exceptions and `perror()`
- Makefile build system with proper dependency management
- Automated stress testing

---

## 🔑 Interview Talking Points

### "How does your thread pool work?"
> The main thread runs an accept loop, acting as the **producer** — it pushes accepted client file descriptors into a thread-safe queue. Worker threads are the **consumers** — they block on a condition variable when the queue is empty, wake up when signaled, dequeue a client fd, and handle the connection. The queue is protected by a **pthread mutex**, and **pthread_cond_signal** is used to notify workers of new work.

### "Why producer-consumer?"
> It **decouples** I/O-bound work (accepting connections) from CPU-bound work (processing requests). The main thread never blocks on client handling, and workers sleep when idle — no busy-waiting, no wasted CPU.

### "Why not create a thread per client?"
> Thread creation is expensive (~1MB stack per thread). With 10,000 concurrent clients, you'd exhaust memory. A fixed-size thread pool reuses threads, bounding resource usage.

### "How do you handle shutdown?"
> A `SIGINT` handler sets a `volatile sig_atomic_t` flag. The accept loop checks this flag and breaks. Then we call `shutdown()` on the thread pool, which sets a stop flag, broadcasts on the condition variable to wake all workers, and joins all threads. Finally, the server socket is closed. This prevents resource leaks and ensures clean termination.

---

## 📜 License

This project is open-source and available for educational use.
