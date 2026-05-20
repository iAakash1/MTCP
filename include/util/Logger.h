/**
 * @file Logger.h
 * @brief Thread-safe, severity-leveled logger with timestamps and thread IDs.
 *
 * Design:
 *   - Singleton (Logger::get()) — one logger for the whole process.
 *   - pthread_mutex_t protects all fprintf() calls — zero interleaving.
 *   - Severity filtering: messages below minLevel_ are discarded before locking.
 *   - Timestamps via clock_gettime(CLOCK_REALTIME) — millisecond resolution.
 *   - Thread IDs via gettid() (Linux) — human-readable integer, not opaque pthread_t.
 *   - ANSI color output for easy terminal scanning.
 *
 * Why not std::mutex?
 *   We keep pthreads throughout to stay consistent with the rest of the project
 *   and to demonstrate POSIX synchronization primitives end-to-end.
 *
 * Why not a dedicated logger thread?
 *   A mutex-protected fprintf is sufficient for a server of this scale.
 *   A dedicated thread adds complexity and a queue; add it when benchmarking
 *   shows the lock is a bottleneck (it won't be at < 100k req/s).
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <pthread.h>
#include <string>
#include <cstdio>

// ── Severity levels ──────────────────────────────────────────────────────────
enum class LogLevel {
    DEBUG = 0,   // verbose internal state
    INFO  = 1,   // normal lifecycle events
    WARN  = 2,   // recoverable issues
    ERROR = 3    // errors affecting a connection or resource
};

class Logger {
public:
    // ── Singleton access ─────────────────────────────────────────────────────
    static Logger& get();

    // ── Configuration (call before spawning threads for safety) ─────────────
    void setLevel (LogLevel level);          // default: INFO
    void setOutput(FILE* fp);               // default: stderr

    // ── Logging API ──────────────────────────────────────────────────────────
    void debug(const std::string& msg);
    void info (const std::string& msg);
    void warn (const std::string& msg);
    void error(const std::string& msg);
    void log  (LogLevel level, const std::string& msg);

    // Non-copyable
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger();
    ~Logger();

    static const char* levelStr  (LogLevel level);
    static const char* levelColor(LogLevel level);
    std::string        timestamp () const;
    std::string        threadId  () const;

    pthread_mutex_t mutex_;
    LogLevel        minLevel_;
    FILE*           output_;
};

// ── Convenience macros ───────────────────────────────────────────────────────
#define LOG_DEBUG(msg) Logger::get().debug(msg)
#define LOG_INFO(msg)  Logger::get().info(msg)
#define LOG_WARN(msg)  Logger::get().warn(msg)
#define LOG_ERROR(msg) Logger::get().error(msg)

#endif // LOGGER_H
