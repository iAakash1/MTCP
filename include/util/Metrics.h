/**
 * @file Metrics.h
 * @brief Lock-free atomic metrics counters for server observability.
 *
 * Design:
 *   - std::atomic<uint64_t> for all counters — no lock needed on the hot path.
 *   - std::memory_order_relaxed for updates: we don't need inter-thread ordering
 *     guarantees on individual counters; eventual consistency in the dump is fine.
 *   - queue high-water mark uses compare_exchange_weak for lock-free max tracking.
 *   - dump() takes a snapshot and writes to Logger — called by a background thread.
 *
 * Interview talking points:
 *   - Why atomic and not mutex?  Atomic increments are a single CPU instruction
 *     (LOCK XADD on x86).  A mutex costs a syscall (futex) when contended.
 *     For counters updated on every connection, this matters.
 *   - Why memory_order_relaxed?  We only need the counter to eventually be
 *     consistent, not sequentially consistent with other operations.  Relaxed
 *     avoids memory fences on the critical path.
 *   - Why high-water mark via CAS loop?  There is no atomic_max in C++17.
 *     The CAS loop is lock-free and wait-free in the uncontended case.
 */

#ifndef METRICS_H
#define METRICS_H

#include <atomic>
#include <cstdint>
#include <ctime>

struct Metrics {
    // ── Connection counters ──────────────────────────────────────────────────
    std::atomic<uint64_t> totalConnections   {0};  // ever accepted
    std::atomic<uint64_t> activeConnections  {0};  // currently being handled
    std::atomic<uint64_t> droppedConnections {0};  // rejected due to full queue

    // ── Throughput counters ──────────────────────────────────────────────────
    std::atomic<uint64_t> totalBytesRx       {0};  // bytes received from clients
    std::atomic<uint64_t> totalBytesTx       {0};  // bytes sent to clients

    // ── Error counters ───────────────────────────────────────────────────────
    std::atomic<uint64_t> totalErrors        {0};  // recv/send errors

    // ── Queue observability ──────────────────────────────────────────────────
    std::atomic<uint64_t> queueHighWater     {0};  // peak queue depth

    // ── Start time for uptime reporting ─────────────────────────────────────
    struct timespec startTime;

    // ── Singleton access ─────────────────────────────────────────────────────
    static Metrics& get();

    /**
     * Dump a formatted metrics snapshot to the logger.
     * Safe to call from any thread (uses Logger's mutex internally).
     */
    void dump() const;

    /**
     * CAS-loop to atomically track the maximum queue depth seen.
     * Lock-free, wait-free under low contention.
     */
    void updateQueueHighWater(uint64_t currentDepth);

    // Non-copyable
    Metrics(const Metrics&)            = delete;
    Metrics& operator=(const Metrics&) = delete;

private:
    Metrics();
};

#endif // METRICS_H
