/**
 * @file ThreadPool.h
 * @brief Fixed-size thread pool using pthreads with producer-consumer pattern.
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │                   PRODUCER-CONSUMER MODEL                      │
 * │                                                                │
 * │  PRODUCER (main thread)                                        │
 * │      │                                                         │
 * │      │  enqueue(clientFd)                                      │
 * │      ▼                                                         │
 * │  ┌──────────────────────┐                                      │
 * │  │   Shared Task Queue  │  ◄── protected by pthread_mutex_t    │
 * │  │   queue<int>         │                                      │
 * │  └──────────┬───────────┘                                      │
 * │             │  pthread_cond_signal                              │
 * │             ▼                                                   │
 * │  CONSUMERS (worker threads)                                    │
 * │      Worker 0  │  Worker 1  │  Worker 2  │  Worker 3           │
 * │      (blocks if queue empty via pthread_cond_wait)             │
 * └─────────────────────────────────────────────────────────────────┘
 *
 * Synchronization:
 *   - pthread_mutex_t : mutual exclusion on the shared queue
 *   - pthread_cond_t  : workers sleep when queue is empty,
 *                        wake when producer enqueues work
 *
 * Graceful Shutdown:
 *   1. Set stop_ flag
 *   2. Broadcast on condition variable (wake ALL sleeping workers)
 *   3. Join all threads
 */

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <queue>
#include <functional>

class ThreadPool {
public:
    /**
     * Construct thread pool and spawn worker threads immediately.
     * @param numThreads Number of worker threads to create.
     * @param handler    Function each worker calls with a client fd.
     */
    ThreadPool(int numThreads, std::function<void(int)> handler);

    /**
     * Destructor — calls shutdown() if not already stopped.
     */
    ~ThreadPool();

    // ── Delete copy (threads are non-copyable resources) ─────────────
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * PRODUCER OPERATION:
     * Push a client socket fd into the shared task queue.
     * Signals one sleeping worker thread to wake up.
     *
     * @param clientFd The file descriptor of the accepted client.
     */
    void enqueue(int clientFd);

    /**
     * Graceful shutdown:
     *   1. Set stop flag (under lock)
     *   2. Broadcast condition variable (wake all workers)
     *   3. Join all worker threads
     */
    void shutdown();

private:
    /**
     * CONSUMER OPERATION (static — required by pthread_create):
     * Each worker thread runs this function in an infinite loop:
     *   1. Lock mutex
     *   2. Wait on condition variable while queue is empty AND not stopping
     *   3. Dequeue a task (client fd)
     *   4. Unlock mutex
     *   5. Process the task (call handler)
     *
     * @param arg Pointer to the ThreadPool instance (cast from void*).
     * @return nullptr (never returns until shutdown).
     */
    static void* workerFunction(void* arg);

    // ── Thread management ────────────────────────────────────────────
    int                     numThreads_;
    pthread_t*              threads_;       // dynamically allocated array

    // ── Shared task queue (the "buffer" in producer-consumer) ────────
    std::queue<int>         taskQueue_;

    // ── Synchronization primitives ───────────────────────────────────
    pthread_mutex_t         mutex_;         // protects taskQueue_ and stop_
    pthread_cond_t          condition_;     // workers wait here when idle

    // ── Shutdown flag ────────────────────────────────────────────────
    bool                    stop_;

    // ── Client handler function ──────────────────────────────────────
    std::function<void(int)> handler_;
};

#endif // THREADPOOL_H
