High-Performance Multithreaded TCP ServerA robust, low-level TCP server implementation written in C++17 utilizing the POSIX Socket API. This project serves as a demonstration of core systems programming, featuring a custom thread pool, manual synchronization primitives, and a producer-consumer architecture.Architecture OverviewThe server utilizes a Producer-Consumer model to decouple connection acceptance from request processing.Plaintext                ┌─────────────────────────────────────────────────────────┐
                │                    SERVER PROCESS                       │
                │                                                         │
  Client ──►    │   ┌──────────┐   accept()    ┌───────────────────┐     │
  Client ──►    │   │ TcpServer│ ─────────────►│    Task Queue     │     │
  Client ──►    │   │ (listen) │               │  std::queue<int>  │     │
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
Producer (Main Thread): Executes the accept() loop and pushes client file descriptors into a synchronized queue.Consumer (Worker Threads): Dequeue descriptors, manage client I/O, and execute the request-response cycle.Technical SpecificationsComponentImplementation DetailsNetworkingPOSIX Sockets (socket, bind, listen, accept)Concurrencypthread library for thread lifecycle managementSynchronizationpthread_mutex_t and pthread_cond_t for thread-safe queue operationsResource ManagementRAII (Resource Acquisition Is Initialization) for automated socket/thread cleanupSignal Handlingsig_atomic_t flags and custom SIGINT handlers for graceful terminationNetwork ConfigurationSO_REUSEADDR enabled for immediate port recovery upon restartProject StructurePlaintext.
├── src/
│   ├── main.cpp           # Entry point and signal handling logic
│   ├── TcpServer.cpp      # Socket lifecycle and listener implementation
│   └── ThreadPool.cpp     # Worker thread management and task synchronization
├── include/
│   ├── TcpServer.h        # Server class definitions
│   └── ThreadPool.h       # ThreadPool and TaskQueue definitions
├── tests/
│   └── stress_test.py     # Automated concurrency testing script
├── Makefile               # Standardized build system (C++17)
└── README.md
Installation and UsagePrerequisitesPOSIX-compliant operating system (Linux or macOS)C++17 compatible compiler (GCC or Clang)Python 3.x (for automated testing)BuildTo compile the project with optimized flags:Bashmake
ExecutionStart the server on the default port (8080):Bash./server
TestingTo verify the server's stability under load, run the concurrent stress test:Bashpython3 tests/stress_test.py
Performance MetricsThe following metrics were observed during local testing on a standard Unix environment:Concurrency: Successfully handled 100+ simultaneous persistent connections.Latency: Average response time < 5ms per echo request.Stability: Zero memory leaks or resource deadlocks identified during long-running stress tests.Shutdown: Verified clean exit of all threads and release of the port within < 1 second of SIGINT.Technical Design DecisionsWhy a fixed-size Thread Pool?Creating a new thread per connection introduces significant overhead and risks memory exhaustion under high traffic. By using a pre-allocated pool, we bound system resource usage and minimize context-switching overhead.How is the Producer-Consumer pattern implemented?The TaskQueue uses a monitor-style synchronization approach. When the queue is empty, worker threads are put to sleep via pthread_cond_wait, consuming zero CPU cycles. The main thread wakes exactly one worker via pthread_cond_signal whenever a new connection is accepted.How is Graceful Shutdown ensured?Upon receiving SIGINT, the server breaks the accept loop, sets a shutdown flag in the ThreadPool, and broadcasts a signal to all condition variables. This ensures that even idle workers wake up, finalize their state, and are properly joined before the process terminates.AuthorAakash Jawle Computer Engineering Student | Systems & Machine Learning Intern