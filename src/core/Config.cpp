/**
 * @file Config.cpp
 * @brief CLI argument parsing via getopt_long and config display.
 */

#include "core/Config.h"

#include <getopt.h>
#include <cstdlib>
#include <cstdio>

static void printUsage(const char* prog) {
    fprintf(stderr,
        "\nUsage: %s [OPTIONS]\n\n"
        "  -p, --port            PORT    Port to listen on            (default: 8080)\n"
        "  -t, --threads         COUNT   Worker thread count          (default: 4)\n"
        "  -b, --backlog         DEPTH   listen() SYN queue depth     (default: 128)\n"
        "  -q, --queue-depth     DEPTH   Max pending task queue size  (default: 1024)\n"
        "  -r, --recv-timeout    SECS    Per-client recv timeout       (default: 10, 0=off)\n"
        "  -s, --send-timeout    SECS    Per-client send timeout       (default: 10, 0=off)\n"
        "  -m, --stats-interval  SECS    Metrics dump interval         (default: 10, 0=off)\n"
        "  -k, --keepalive-idle  SECS    TCP keepalive idle timeout    (default: 60, 0=off)\n"
        "  -v, --verbose                 Enable DEBUG-level logging\n"
        "  -h, --help                    Show this help and exit\n\n"
        "Examples:\n"
        "  %s                            # run with all defaults\n"
        "  %s --port 9000 --threads 8    # custom port and thread count\n"
        "  %s --verbose --stats-interval 5\n\n",
        prog, prog, prog, prog
    );
    exit(1);
}

ServerConfig parseArgs(int argc, char** argv) {
    ServerConfig cfg;  // initialized with defaults

    static const struct option long_opts[] = {
        {"port",           required_argument, nullptr, 'p'},
        {"threads",        required_argument, nullptr, 't'},
        {"backlog",        required_argument, nullptr, 'b'},
        {"queue-depth",    required_argument, nullptr, 'q'},
        {"recv-timeout",   required_argument, nullptr, 'r'},
        {"send-timeout",   required_argument, nullptr, 's'},
        {"stats-interval", required_argument, nullptr, 'm'},
        {"keepalive-idle", required_argument, nullptr, 'k'},
        {"verbose",        no_argument,       nullptr, 'v'},
        {"help",           no_argument,       nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "p:t:b:q:r:s:m:k:vh",
                              long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'p': cfg.port          = std::atoi(optarg); break;
            case 't': cfg.threads       = std::atoi(optarg); break;
            case 'b': cfg.backlog       = std::atoi(optarg); break;
            case 'q': cfg.queueDepth    = std::atoi(optarg); break;
            case 'r': cfg.recvTimeout   = std::atoi(optarg); break;
            case 's': cfg.sendTimeout   = std::atoi(optarg); break;
            case 'm': cfg.statsInterval = std::atoi(optarg); break;
            case 'k': cfg.keepAliveIdle = std::atoi(optarg); break;
            case 'v': cfg.verbose       = true;              break;
            case 'h': printUsage(argv[0]);                   break;
            default:  printUsage(argv[0]);                   break;
        }
    }

    // ── Validation ───────────────────────────────────────────────────────────
    if (cfg.port < 1 || cfg.port > 65535) {
        fprintf(stderr, "[Config] Error: --port must be 1–65535, got %d\n", cfg.port);
        exit(1);
    }
    if (cfg.threads < 1 || cfg.threads > 512) {
        fprintf(stderr, "[Config] Error: --threads must be 1–512, got %d\n", cfg.threads);
        exit(1);
    }
    if (cfg.backlog < 1) {
        fprintf(stderr, "[Config] Error: --backlog must be >= 1, got %d\n", cfg.backlog);
        exit(1);
    }
    if (cfg.queueDepth < 1) {
        fprintf(stderr, "[Config] Error: --queue-depth must be >= 1, got %d\n", cfg.queueDepth);
        exit(1);
    }
    if (cfg.recvTimeout < 0 || cfg.sendTimeout < 0) {
        fprintf(stderr, "[Config] Error: timeouts must be >= 0\n");
        exit(1);
    }

    return cfg;
}

void printConfig(const ServerConfig& cfg) {
    fprintf(stderr,
        "\n"
        "╔═══════════════════════════════════════════════╗\n"
        "║         MTCP Server  —  Configuration         ║\n"
        "╠═══════════════════════════════════════════════╣\n"
        "║  Port              : %-5d                    ║\n"
        "║  Worker threads    : %-5d                    ║\n"
        "║  listen() backlog  : %-5d                    ║\n"
        "║  Queue depth limit : %-5d                    ║\n"
        "║  Recv timeout      : %-5d s                  ║\n"
        "║  Send timeout      : %-5d s                  ║\n"
        "║  Stats interval    : %-5d s                  ║\n"
        "║  Keepalive idle    : %-5d s                  ║\n"
        "║  Verbose logging   : %-5s                    ║\n"
        "╚═══════════════════════════════════════════════╝\n\n",
        cfg.port, cfg.threads, cfg.backlog, cfg.queueDepth,
        cfg.recvTimeout, cfg.sendTimeout, cfg.statsInterval,
        cfg.keepAliveIdle, cfg.verbose ? "yes" : "no"
    );
}
