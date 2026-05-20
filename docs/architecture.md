# MTCP Architecture Deep-Dive

## Component Responsibilities

| Component | File(s) | Responsibility |
|---|---|---|
| `TcpServer` | `net/TcpServer.*` | Socket lifecycle: socket→bind→listen→accept |
| `ThreadPool` | `core/ThreadPool.*` | Worker management, bounded queue, shutdown |
| `ClientHandler` | `handler/ClientHandler.*` | Per-connection I/O, framing, lifecycle log |
| `SocketUtils` | `net/SocketUtils.*` | sendAll, recvLine, timeout, keepalive helpers |
| `Logger` | `util/Logger.*` | Thread-safe output, severity filtering |
| `Metrics` | `util/Metrics.*` | Lock-free atomic counters, periodic dump |
| `Config` | `core/Config.*` | CLI parsing via getopt_long, validation |
| `main.cpp` | `src/main.cpp` | Wiring, accept loop, SIGINT, stats thread |

## Design Decisions

### Why pthreads and not std::thread?

`std::thread` wraps pthreads on Linux, adding overhead and hiding POSIX details.
Using `pthread_*` directly demonstrates explicit knowledge of the OS thread API —
`pthread_attr_setstacksize`, `pthread_cond_wait`, `pthread_cond_broadcast`, `prctl` —
which is what systems interviewers at Nvidia/Semtech/Qualcomm ask about.

### Why a fixed pool and not dynamic resizing?

Dynamic resizing requires tracking per-worker state, deciding when to shrink
(ABA problem), and handling resize under load. The fixed pool is the correct
default: choose N ≈ 2×CPU cores for I/O-bound workloads. Mention dynamic
resizing as a future extension in interviews — shows awareness without over-engineering.

### Why `recvLine()` reads one byte at a time?

For a framed protocol, one-byte-at-a-time is correct and simple to audit.
The performance cost is negligible at the scale of TCP connections (the syscall
is already incurred on connection establishment; one extra read syscall per byte
is minimal vs. the RTT). The alternative — bulk `recv()` into a ring buffer with
a state machine — is correct but complex. Mention both approaches in interviews.

### Why `std::atomic` for metrics instead of a mutex?

On x86, `std::atomic<uint64_t>::fetch_add(1, relaxed)` compiles to `LOCK XADD`.
This is a single instruction, ~3ns on a modern CPU, and requires no syscall.
A `pthread_mutex_lock/unlock` pair costs ~10ns uncontended and hundreds of ns
contended (futex syscall). For a counter updated on every connection, the
difference is measurable under high load.

### Why `memory_order_relaxed` for metrics?

Metrics counters don't need to be consistent with each other at any specific
point in time. We only need each individual counter to eventually reach its
correct value. `relaxed` skips memory fences — on x86 this makes no difference
(x86 is TSO), but on ARM it avoids costly `dmb` barrier instructions.

### Why `SO_RCVTIMEO` for timeouts instead of `select()`/`poll()`?

`SO_RCVTIMEO` is simpler: set once on the socket, automatically applies to all
`recv()` calls. `select()`/`poll()` would require restructuring the handler loop
and adds complexity. For this design, `SO_RCVTIMEO` is the right tool.
Mention `poll()`-based multiplexing as the basis for event-loop servers (nginx,
libuv) in interviews to show architectural breadth.

## Memory Layout (4 workers, 2MB stack each)

```
Process virtual memory:
  Code + BSS + heap:    ~1MB
  Logger mutex + queue: <1KB
  Metrics struct:       ~64B (8 × 8-byte atomics)
  Task queue (1024 int): ~4KB
  Worker stacks:        4 × 2MB = 8MB
  Total committed:      ~9MB

vs. 4 workers with default 8MB stacks:
  Total:                ~33MB
  Savings:              ~24MB (73% reduction)
```

## Shutdown Sequence (timing)

```
t=0ms    Ctrl+C pressed
t=0ms    SIGINT delivered, onSigInt() runs
t=0ms    g_shutdown=1 written
t=0ms    accept() returns EINTR
t=0ms    Main loop breaks
t=0ms    pool.shutdown() called:
           pthread_mutex_lock
           stop_=true
           pthread_mutex_unlock
           pthread_cond_broadcast   ← ALL workers wake
t=~1ms   Workers finish current task (handleClient returns)
t=~1ms   Workers see stop_=true && queue.empty() → break
t=~1ms   pthread_join completes for all workers
t=~1ms   server.close() → ::close(server_fd_)
t=~1001ms stats thread wakes from 1s sleep → sees g_shutdown=1 → exits
t=~1001ms pthread_join(statsTid)
t=~1001ms Metrics::get().dump() — final snapshot
t=~1001ms Logger: "Clean shutdown complete"
```

Total shutdown latency: max(task_completion_time, 1s stats_sleep) ≈ 1s worst case.
To reduce: set stats_interval to 0 (disables stats thread entirely).
