/**
 * @file Config.h
 * @brief Server runtime configuration parsed from CLI via getopt_long.
 *
 * Design:
 *   - ServerConfig is a plain struct with sensible defaults.
 *   - parseArgs() owns all argv parsing — main() stays clean.
 *   - printConfig() dumps a formatted summary so operators can verify settings.
 *
 * Why getopt_long and not a custom parser?
 *   getopt_long() is POSIX standard, supports both short (-p) and long
 *   (--port) forms, and handles the argument-required vs. flag distinction
 *   correctly.  It's what every production CLI tool on Linux uses.
 *
 * Why compile-time defaults in the struct?
 *   C++11 in-class member initializers let callers omit fields they don't
 *   override.  The struct is self-documenting: look at the header to see
 *   every knob and its default value.
 */

#ifndef CONFIG_H
#define CONFIG_H

struct ServerConfig {
    int  port          = 8080;   // TCP port to bind
    int  threads       = 4;      // worker thread count
    int  backlog       = 128;    // listen() backlog (kernel SYN queue depth)
    int  queueDepth    = 1024;   // max pending tasks in ThreadPool queue
    int  recvTimeout   = 10;     // SO_RCVTIMEO per client (seconds, 0 = off)
    int  sendTimeout   = 10;     // SO_SNDTIMEO per client (seconds, 0 = off)
    int  statsInterval = 10;     // seconds between metrics dumps (0 = disable)
    int  keepAliveIdle = 60;     // TCP_KEEPIDLE (seconds, 0 = disable keepalive)
    bool verbose       = false;  // enable DEBUG-level log output
};

/**
 * Parse argc/argv using getopt_long.
 * Validates ranges.  Prints usage + exits on bad input or --help.
 */
ServerConfig parseArgs(int argc, char** argv);

/**
 * Print a formatted configuration table to stderr before starting.
 * Gives operators a clear record of what settings are in effect.
 */
void printConfig(const ServerConfig& cfg);

#endif // CONFIG_H
