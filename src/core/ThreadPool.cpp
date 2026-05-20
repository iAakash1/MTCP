/**
 * @file ThreadPool.cpp
 * @brief Upgraded pthread thread pool implementation.
 *
 * Key additions over v1:
 *
 *   Bounded queue backpressure:
 *     enqueue() checks taskQueue_.size() against maxQueueDepth_ before pushing.
 *     If at capacity: close fd, increment dropped-connections counter, return false.
 *     This is admission control — the server sheds load cleanly rather than
 *     accumulating unbounded state until OOM.
 *
 *   Thread stack size via pthread_attr_setstacksize():
 *     Default stack is 8MB on Linux.  We configure 2MB — still more than
 *     enough for our frame depth (main → workerFunction → handler → recvLine).
 *     With 64 workers, this saves 384MB of committed virtual memory.
 *
 *   Thread naming via prctl(PR_SET_NAME):
 *     Each worker calls prctl to set its kernel thread name to "worker-N".
 *     The name appears in: /proc/<tid>/comm, htop, ps -T, gdb "info threads",
 *     perf record, and strace output.  This is how you debug threading issues
 *     in production — not by reading opaque pthread_t hex values.
 *
 *   Sequential worker index:
 *     WorkerArg bundles {pool*, index}.  workerArgs_ is sized before any
 *     pthread_create call so the vector never reallocates, keeping pointers
 *     stable.  A raw pointer into a reallocating vector is a use-after-free.
 */

#include "core/ThreadPool.h"
#include "util/Logger.h"
#include "util/Metrics.h"

#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <unistd.h>

#ifdef __linux__
#  include <sys/prctl.h>
#endif

// ── Constructor ──────────────────────────────────────────────────────────────
ThreadPool::ThreadPool(int numThreads, int maxQueueDepth,
                       std::function<void(int,int)> handler)
    : numThreads_   (numThreads),
      maxQueueDepth_(maxQueueDepth),
      stop_         (false),
      handler_      (std::move(handler))
{
    // Pre-size vectors before creating threads.
    // CRITICAL: workerArgs_ must not reallocate after pthread_create, because
    // each thread holds a raw pointer into this vector.
    threads_   .resize(numThreads_);
    workerArgs_.resize(numThreads_);

    // ── Mutex and condvar init ───────────────────────────────────────────────
    if (pthread_mutex_init(&mutex_, nullptr) != 0)
        throw std::runtime_error("pthread_mutex_init failed");

    if (pthread_cond_init(&condition_, nullptr) != 0) {
        pthread_mutex_destroy(&mutex_);
        throw std::runtime_error("pthread_cond_init failed");
    }

    // ── Thread attributes: reduced stack size ────────────────────────────────
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    // 2MB stack — sufficient for our handler call depth, uses less VM than 8MB default
    pthread_attr_setstacksize(&attr, 2UL * 1024 * 1024);

    // ── Spawn workers ────────────────────────────────────────────────────────
    for (int i = 0; i < numThreads_; ++i) {
        workerArgs_[i] = {this, i};

        int rc = pthread_create(&threads_[i], &attr, workerFunction, &workerArgs_[i]);
        if (rc != 0) {
            // Partial-creation cleanup: signal already-running threads to stop
            stop_ = true;
            pthread_cond_broadcast(&condition_);
            for (int j = 0; j < i; ++j)
                pthread_join(threads_[j], nullptr);
            pthread_attr_destroy(&attr);
            pthread_mutex_destroy(&mutex_);
            pthread_cond_destroy(&condition_);
            throw std::runtime_error(
                "pthread_create failed for worker-" + std::to_string(i) +
                ": " + strerror(rc));
        }
    }

    pthread_attr_destroy(&attr);

    Logger::get().info("[ThreadPool] Started " + std::to_string(numThreads_) +
                       " workers | queue cap=" + std::to_string(maxQueueDepth_) +
                       " | stack=2MB/thread");
}

// ── Destructor ───────────────────────────────────────────────────────────────
ThreadPool::~ThreadPool() {
    shutdown();
}

// ── enqueue (PRODUCER OPERATION) ─────────────────────────────────────────────
bool ThreadPool::enqueue(int clientFd) {
    pthread_mutex_lock(&mutex_);

    // Reject immediately if shutting down
    if (stop_) {
        pthread_mutex_unlock(&mutex_);
        ::close(clientFd);
        return false;
    }

    // ── BACKPRESSURE: bounded queue enforcement ──────────────────────────────
    if (static_cast<int>(taskQueue_.size()) >= maxQueueDepth_) {
        pthread_mutex_unlock(&mutex_);
        ::close(clientFd);
        Metrics::get().droppedConnections.fetch_add(1, std::memory_order_relaxed);
        Logger::get().warn("[ThreadPool] Queue at capacity (" +
                           std::to_string(maxQueueDepth_) +
                           ") — dropped fd=" + std::to_string(clientFd));
        return false;
    }

    taskQueue_.push(clientFd);

    // Update queue high-water mark for observability
    Metrics::get().updateQueueHighWater(taskQueue_.size());

    // Wake exactly one sleeping worker
    pthread_cond_signal(&condition_);
    pthread_mutex_unlock(&mutex_);
    return true;
}

// ── queueDepth ───────────────────────────────────────────────────────────────
size_t ThreadPool::queueDepth() const {
    pthread_mutex_lock(const_cast<pthread_mutex_t*>(&mutex_));
    size_t d = taskQueue_.size();
    pthread_mutex_unlock(const_cast<pthread_mutex_t*>(&mutex_));
    return d;
}

// ── workerFunction (CONSUMER OPERATION) ──────────────────────────────────────
void* ThreadPool::workerFunction(void* arg) {
    WorkerArg*  wa   = static_cast<WorkerArg*>(arg);
    ThreadPool* pool = wa->pool;
    int         idx  = wa->index;

    // ── Name this thread for debugger/profiler visibility ────────────────────
    //   After this call: htop, gdb "info threads", perf, strace all show
    //   "worker-N" instead of the process name or an opaque pthread ID.
#ifdef __linux__
    char tname[16];
    snprintf(tname, sizeof(tname), "worker-%d", idx);
    prctl(PR_SET_NAME, tname, 0, 0, 0);
#endif

    Logger::get().info("[worker-" + std::to_string(idx) + "] Thread started");

    // ── Main worker loop ─────────────────────────────────────────────────────
    while (true) {
        int clientFd = -1;

        // ── CRITICAL SECTION: dequeue one task ──────────────────────────────
        {
            pthread_mutex_lock(&pool->mutex_);

            // ── CONSUMER WAIT LOOP ───────────────────────────────────────────
            //   MUST be a while loop, not if.  POSIX allows spurious wakeups:
            //   pthread_cond_wait() can return even if no signal was sent.
            //   The while loop re-checks the condition and goes back to sleep.
            while (pool->taskQueue_.empty() && !pool->stop_)
                pthread_cond_wait(&pool->condition_, &pool->mutex_);
            //   NOTE: pthread_cond_wait atomically releases the mutex and
            //   sleeps.  On wakeup, it atomically re-acquires the mutex.
            //   This prevents the "lost wakeup" race.

            // Shutdown: exit only when queue is fully drained
            if (pool->stop_ && pool->taskQueue_.empty()) {
                pthread_mutex_unlock(&pool->mutex_);
                break;
            }

            clientFd = pool->taskQueue_.front();
            pool->taskQueue_.pop();
            pthread_mutex_unlock(&pool->mutex_);
        }
        // ── END CRITICAL SECTION ─────────────────────────────────────────────
        //   The handler is called OUTSIDE the lock.
        //   Holding the lock during client I/O would serialize all workers —
        //   defeating the entire purpose of a thread pool.

        if (clientFd >= 0)
            pool->handler_(clientFd, idx);
    }

    Logger::get().info("[worker-" + std::to_string(idx) + "] Thread exiting");
    return nullptr;
}

// ── shutdown ─────────────────────────────────────────────────────────────────
void ThreadPool::shutdown() {
    pthread_mutex_lock(&mutex_);
    if (stop_) {
        pthread_mutex_unlock(&mutex_);
        return;  // idempotent — safe to call multiple times
    }
    stop_ = true;
    pthread_mutex_unlock(&mutex_);

    // Wake ALL sleeping workers.
    // pthread_cond_signal wakes ONE.  We need every thread to see stop_=true.
    // broadcast() is the correct primitive for collective wakeup on shutdown.
    pthread_cond_broadcast(&condition_);

    // Join all threads.  pthread_join blocks until the thread exits.
    // This guarantees: no thread is still executing handler() when we return.
    // Without join, destroying the mutex/condvar while threads hold them is UB.
    for (int i = 0; i < numThreads_; ++i)
        pthread_join(threads_[i], nullptr);

    Logger::get().info("[ThreadPool] All " + std::to_string(numThreads_) +
                       " threads joined — pool stopped");

    pthread_mutex_destroy(&mutex_);
    pthread_cond_destroy(&condition_);
}
