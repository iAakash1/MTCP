/**
 * @file Metrics.cpp
 * @brief Atomic metrics implementation.
 */

#include "util/Metrics.h"
#include "util/Logger.h"

#include <cstdio>
#include <string>

Metrics& Metrics::get() {
    static Metrics instance;
    return instance;
}

Metrics::Metrics() {
    clock_gettime(CLOCK_MONOTONIC, &startTime);
}

void Metrics::updateQueueHighWater(uint64_t depth) {
    // Lock-free max update via compare_exchange_weak loop.
    // Relaxed ordering: we only need eventual consistency on this counter.
    uint64_t current = queueHighWater.load(std::memory_order_relaxed);
    while (depth > current) {
        if (queueHighWater.compare_exchange_weak(
                current, depth,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            break;
        }
        // current was updated by compare_exchange — loop re-checks
    }
}

void Metrics::dump() const {
    // ── Snapshot all counters ────────────────────────────────────────────────
    //   We take each load individually (not an atomic snapshot across all),
    //   but for a metrics dump this level of consistency is acceptable.
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t uptimeSecs = static_cast<uint64_t>(now.tv_sec - startTime.tv_sec);

    uint64_t rxB = totalBytesRx.load(std::memory_order_relaxed);
    uint64_t txB = totalBytesTx.load(std::memory_order_relaxed);

    // Format bytes human-readably
    auto humanBytes = [](uint64_t b) -> std::string {
        char buf[32];
        if (b >= 1024ULL * 1024 * 1024)
            snprintf(buf, sizeof(buf), "%.2f GB", b / (1024.0 * 1024 * 1024));
        else if (b >= 1024ULL * 1024)
            snprintf(buf, sizeof(buf), "%.2f MB", b / (1024.0 * 1024));
        else if (b >= 1024ULL)
            snprintf(buf, sizeof(buf), "%.2f KB", b / 1024.0);
        else
            snprintf(buf, sizeof(buf), "%lu B", (unsigned long)b);
        return buf;
    };

    // Format uptime human-readably
    auto humanUptime = [](uint64_t s) -> std::string {
        char buf[32];
        if (s >= 3600)
            snprintf(buf, sizeof(buf), "%luh %lum %lus",
                     (unsigned long)(s/3600),
                     (unsigned long)((s%3600)/60),
                     (unsigned long)(s%60));
        else if (s >= 60)
            snprintf(buf, sizeof(buf), "%lum %lus",
                     (unsigned long)(s/60),
                     (unsigned long)(s%60));
        else
            snprintf(buf, sizeof(buf), "%lus", (unsigned long)s);
        return buf;
    };

    std::string msg =
        "\n┌────────────────────────────────────────────┐\n"
        "│         SERVER METRICS SNAPSHOT            │\n"
        "├────────────────────────────────────────────┤\n"
        "│  Uptime            : " + humanUptime(uptimeSecs) + "\n" +
        "│  Total connections : " + std::to_string(totalConnections.load(std::memory_order_relaxed)) + "\n" +
        "│  Active now        : " + std::to_string(activeConnections.load(std::memory_order_relaxed)) + "\n" +
        "│  Dropped (full Q)  : " + std::to_string(droppedConnections.load(std::memory_order_relaxed)) + "\n" +
        "│  Bytes received    : " + humanBytes(rxB) + "\n" +
        "│  Bytes sent        : " + humanBytes(txB) + "\n" +
        "│  Errors            : " + std::to_string(totalErrors.load(std::memory_order_relaxed)) + "\n" +
        "│  Queue high-water  : " + std::to_string(queueHighWater.load(std::memory_order_relaxed)) + "\n" +
        "└────────────────────────────────────────────┘";

    Logger::get().info(msg);
}
