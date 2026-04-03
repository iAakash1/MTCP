/**
 * @file ThreadPool.cpp
 * @brief Implementation of the pthread-based thread pool.
 *
 * ════════════════════════════════════════════════════════════════════
 *  PRODUCER-CONSUMER PATTERN IMPLEMENTATION
 * ════════════════════════════════════════════════════════════════════
 *
 *  This file implements the classic Producer-Consumer concurrency
 *  pattern using POSIX threads (pthreads).
 *
 *  ┌──────────┐    enqueue()     ┌────────────┐    dequeue()    ┌──────────┐
 *  │ PRODUCER  │ ──────────────► │  SHARED    │ ─────────────► │ CONSUMER │
 *  │ (main    │                  │  QUEUE     │                │ (worker  │
 *  │  thread) │                  │ queue<int> │                │  threads)│
 *  └──────────┘                  └────────────┘                └──────────┘
 *                                     ▲
 *                            pthread_mutex_t (lock)
 *                            pthread_cond_t  (signal)
 *
 *  Why Producer-Consumer?
 *  ──────────────────────
 *  - Decouples ACCEPT (I/O-bound) from PROCESSING (CPU-bound)
 *  - Main thread never blocks on client handling
 *  - Workers sleep when idle (no busy-waiting = no CPU waste)
 *  - Bounded number of threads prevents resource exhaustion
 *
 *  Interview Key Points:
 *  ─────────────────────
 *  Q: "Why not just create a new thread per client?"
 *  A:  Thread creation is expensive (~1MB stack per thread).
 *      With 10,000 clients, you'd exhaust memory.
 *      A thread pool reuses a fixed set of threads.
 *
 *  Q: "Why mutex + condition variable instead of busy-waiting?"
 *  A:  Busy-waiting (spinning in a loop checking queue) wastes CPU.
 *      pthread_cond_wait() puts the thread to SLEEP until signaled,
 *      using zero CPU while waiting.
 *
 *  Q: "Why pthread_cond_broadcast on shutdown instead of signal?"
 *  A:  pthread_cond_signal wakes ONE thread.
 *      On shutdown, we need ALL workers to wake up and exit.
 *      broadcast wakes every thread waiting on the condition.
 */

#include "ThreadPool.h"

#include <iostream>
#include <cstring>    // strerror
#include <cerrno>     // errno
#include <unistd.h>   // close()

// ─────────────────────────────────────────────────────────────────────────────
// Constructor — Initialize synchronization primitives and spawn workers
// ─────────────────────────────────────────────────────────────────────────────
ThreadPool::ThreadPool(int numThreads, std::function<void(int)> handler)
    : numThreads_(numThreads),
      threads_(new pthread_t[numThreads]),
      stop_(false),
      handler_(std::move(handler))
{
    // ── Initialize mutex ─────────────────────────────────────────────
    //   The mutex protects the shared taskQueue_ and the stop_ flag.
    //   Any thread accessing these MUST hold the lock.
    if (pthread_mutex_init(&mutex_, nullptr) != 0) {
        throw std::runtime_error("pthread_mutex_init failed");
    }

    // ── Initialize condition variable ────────────────────────────────
    //   Workers call pthread_cond_wait() on this when the queue is empty.
    //   The producer calls pthread_cond_signal() after enqueueing work.
    if (pthread_cond_init(&condition_, nullptr) != 0) {
        pthread_mutex_destroy(&mutex_);
        throw std::runtime_error("pthread_cond_init failed");
    }

    // ── Spawn worker threads (CONSUMERS) ─────────────────────────────
    //   Each thread starts executing workerFunction() immediately.
    //   They will block inside workerFunction on the condition variable
    //   until work arrives.
    for (int i = 0; i < numThreads_; ++i) {
        int rc = pthread_create(&threads_[i], nullptr, workerFunction, this);
        if (rc != 0) {
            std::cerr << "[ThreadPool] pthread_create failed for thread "
                      << i << ": " << strerror(rc) << "\n";
            // Shut down already-created threads
            stop_ = true;
            pthread_cond_broadcast(&condition_);
            for (int j = 0; j < i; ++j) {
                pthread_join(threads_[j], nullptr);
            }
            delete[] threads_;
            pthread_mutex_destroy(&mutex_);
            pthread_cond_destroy(&condition_);
            throw std::runtime_error("Failed to create worker thread " + std::to_string(i));
        }
    }

    std::cout << "[ThreadPool] Started " << numThreads_ << " worker threads.\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Destructor
// ─────────────────────────────────────────────────────────────────────────────
ThreadPool::~ThreadPool()
{
    shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// enqueue() — PRODUCER OPERATION
// ─────────────────────────────────────────────────────────────────────────────
//
//   Called by the MAIN THREAD (producer) after accept().
//
//   Steps:
//     1. Acquire the mutex (enter critical section)
//     2. Push the client fd onto the shared queue
//     3. Signal ONE waiting consumer thread
//     4. Release the mutex (leave critical section)
//
//   The signal wakes up exactly one worker that is sleeping in
//   pthread_cond_wait(). If all workers are busy, the task stays
//   in the queue until a worker becomes free.
//
void ThreadPool::enqueue(int clientFd)
{
    // ── CRITICAL SECTION START ───────────────────────────────────────
    pthread_mutex_lock(&mutex_);

    if (stop_) {
        pthread_mutex_unlock(&mutex_);
        ::close(clientFd);  // Don't leak the fd if we're shutting down
        return;
    }

    // ── PRODUCER: add work to shared buffer ──────────────────────────
    taskQueue_.push(clientFd);

    // ── Signal one sleeping CONSUMER ─────────────────────────────────
    //   "Hey, there's work in the queue — wake up!"
    pthread_cond_signal(&condition_);

    // ── CRITICAL SECTION END ─────────────────────────────────────────
    pthread_mutex_unlock(&mutex_);
}

// ─────────────────────────────────────────────────────────────────────────────
// workerFunction() — CONSUMER OPERATION (runs in each worker thread)
// ─────────────────────────────────────────────────────────────────────────────
//
//   This is the function each worker thread executes.
//   It runs an INFINITE LOOP:
//
//   while (true) {
//       lock mutex
//       while (queue empty AND not stopping)
//           wait on condition variable    ← BLOCKS here, releases mutex
//       if (stopping AND queue empty)
//           break                         ← exit the loop
//       dequeue task                      ← CONSUMER: take work from buffer
//       unlock mutex
//       process task                      ← call handler(clientFd)
//   }
//
//   KEY INSIGHT: pthread_cond_wait() ATOMICALLY:
//     1. Releases the mutex
//     2. Puts the thread to sleep
//     3. When signaled: re-acquires the mutex before returning
//
//   This prevents the "lost wakeup" problem:
//     If we released the mutex, then checked the condition, then slept,
//     a signal could arrive between check and sleep, and we'd miss it.
//
void* ThreadPool::workerFunction(void* arg)
{
    // Cast void* back to ThreadPool* (required by pthread_create API)
    ThreadPool* pool = static_cast<ThreadPool*>(arg);

    while (true) {
        int clientFd = -1;

        // ── CRITICAL SECTION: dequeue a task ─────────────────────────
        {
            pthread_mutex_lock(&pool->mutex_);

            // ── CONSUMER WAIT LOOP ───────────────────────────────────
            //   "While there's nothing to consume AND we're not stopping,
            //    go to sleep."
            //
            //   Why a WHILE loop instead of IF?
            //   → Spurious wakeups! The OS can wake a thread without
            //     a signal. The while loop re-checks the condition.
            while (pool->taskQueue_.empty() && !pool->stop_) {
                pthread_cond_wait(&pool->condition_, &pool->mutex_);
                // Upon return: mutex is re-acquired, re-check condition
            }

            // ── Check shutdown condition ─────────────────────────────
            //   If stopping AND no more tasks, exit the thread.
            //   We drain remaining tasks before exiting (graceful).
            if (pool->stop_ && pool->taskQueue_.empty()) {
                pthread_mutex_unlock(&pool->mutex_);
                break;  // Exit the infinite loop → thread terminates
            }

            // ── CONSUMER: take work from the shared buffer ───────────
            clientFd = pool->taskQueue_.front();
            pool->taskQueue_.pop();

            pthread_mutex_unlock(&pool->mutex_);
        }
        // ── END CRITICAL SECTION ─────────────────────────────────────

        // ── Process the task OUTSIDE the lock ────────────────────────
        //   We don't hold the mutex during processing.
        //   This is crucial: if we held the lock, only one worker
        //   could process at a time, defeating the purpose of a pool.
        if (clientFd >= 0) {
            pool->handler_(clientFd);
        }
    }

    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// shutdown() — Graceful termination
// ─────────────────────────────────────────────────────────────────────────────
//
//   Steps:
//     1. Set stop_ = true (under lock, so workers see it)
//     2. Broadcast on condition variable (wake ALL sleeping workers)
//     3. Join all threads (wait for them to finish current tasks)
//     4. Destroy synchronization primitives
//     5. Free the thread array
//
//   Why broadcast instead of signal?
//     signal() wakes ONE thread.  We need ALL threads to see stop_=true
//     and exit.  broadcast() wakes every thread waiting on the condvar.
//
void ThreadPool::shutdown()
{
    // ── Set the stop flag ────────────────────────────────────────────
    pthread_mutex_lock(&mutex_);
    if (stop_) {
        // Already shut down — avoid double-join
        pthread_mutex_unlock(&mutex_);
        return;
    }
    stop_ = true;
    pthread_mutex_unlock(&mutex_);

    // ── Wake ALL sleeping workers ────────────────────────────────────
    //   "Everyone wake up!  We're shutting down!"
    pthread_cond_broadcast(&condition_);

    // ── Join all worker threads ──────────────────────────────────────
    //   pthread_join() blocks until the target thread terminates.
    //   This ensures all workers finish their current task before
    //   we destroy shared resources.
    for (int i = 0; i < numThreads_; ++i) {
        pthread_join(threads_[i], nullptr);
    }

    std::cout << "[ThreadPool] All " << numThreads_ << " threads joined.\n";

    // ── Cleanup ──────────────────────────────────────────────────────
    pthread_mutex_destroy(&mutex_);
    pthread_cond_destroy(&condition_);
    delete[] threads_;
    threads_ = nullptr;
}
