/**
 * @file Logger.cpp
 * @brief Thread-safe logger implementation.
 *
 * Key implementation decisions:
 *   - pthread_mutex_lock/unlock wraps the single fprintf+fflush call.
 *     fflush is essential: without it, output may stay in the C stdio buffer
 *     and never reach the terminal, especially if the process is killed.
 *   - clock_gettime(CLOCK_REALTIME) for wall-clock timestamps.
 *     CLOCK_MONOTONIC would give uptime; REALTIME gives human-readable time.
 *   - gettid() (Linux syscall 186) returns the kernel thread ID (TID).
 *     Unlike pthread_t, TID is a plain integer visible in ps, htop, gdb,
 *     strace, and perf. Much more useful for debugging.
 */

#include "util/Logger.h"
#include <sstream>
#include <ctime>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <pthread.h>

#ifdef __linux__
#  include <sys/syscall.h>
#endif

// ── Singleton ────────────────────────────────────────────────────────────────
Logger& Logger::get() {
    static Logger instance;
    return instance;
}

Logger::Logger()
    : minLevel_(LogLevel::INFO),
      output_(stderr)
{
    pthread_mutex_init(&mutex_, nullptr);
}

Logger::~Logger() {
    pthread_mutex_destroy(&mutex_);
}

// ── Configuration ────────────────────────────────────────────────────────────
void Logger::setLevel(LogLevel level) {
    pthread_mutex_lock(&mutex_);
    minLevel_ = level;
    pthread_mutex_unlock(&mutex_);
}

void Logger::setOutput(FILE* fp) {
    pthread_mutex_lock(&mutex_);
    output_ = fp;
    pthread_mutex_unlock(&mutex_);
}

// ── Logging API ──────────────────────────────────────────────────────────────
void Logger::debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
void Logger::info (const std::string& msg) { log(LogLevel::INFO,  msg); }
void Logger::warn (const std::string& msg) { log(LogLevel::WARN,  msg); }
void Logger::error(const std::string& msg) { log(LogLevel::ERROR, msg); }

void Logger::log(LogLevel level, const std::string& msg) {
    // Fast-path filter: avoid lock if message is below threshold
    if (level < minLevel_) return;

    const char* col = levelColor(level);
    const char* lvl = levelStr(level);
    std::string ts  = timestamp();
    std::string tid = threadId();

    // ── CRITICAL SECTION ────────────────────────────────────────────────
    //   Single fprintf+fflush is atomic at the C-level inside the lock.
    //   This prevents interleaved output from concurrent worker threads.
    pthread_mutex_lock(&mutex_);
    fprintf(output_, "%s[%s][%s][tid:%-6s] %s\033[0m\n",
            col, ts.c_str(), lvl, tid.c_str(), msg.c_str());
    fflush(output_);
    pthread_mutex_unlock(&mutex_);
    // ── END CRITICAL SECTION ─────────────────────────────────────────────
}

// ── Helpers ──────────────────────────────────────────────────────────────────
const char* Logger::levelStr(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        default:              return "?????";
    }
}

const char* Logger::levelColor(LogLevel level) {
    // ANSI escape codes
    switch (level) {
        case LogLevel::DEBUG: return "\033[37m";    // white
        case LogLevel::INFO:  return "\033[32m";    // green
        case LogLevel::WARN:  return "\033[33m";    // yellow
        case LogLevel::ERROR: return "\033[31m";    // red/bold
        default:              return "\033[0m";
    }
}

std::string Logger::timestamp() const {
    // Wall-clock time with millisecond precision
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm_info;
    localtime_r(&ts.tv_sec, &tm_info);

    char date[24];
    strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &tm_info);

    char ms[8];
    snprintf(ms, sizeof(ms), ".%03d", static_cast<int>(ts.tv_nsec / 1000000L));

    return std::string(date) + ms;
}

std::string Logger::threadId() const {
    // Linux: gettid() gives the kernel thread ID visible in htop/gdb/perf.
    // Other POSIX: fall back to lower 16 bits of pthread_t (still unique enough).
#ifdef __linux__
    pid_t tid = static_cast<pid_t>(syscall(SYS_gettid));
    return std::to_string(tid);
#else
    std::ostringstream oss;
    oss << pthread_self();
    return oss.str();
#endif
}
