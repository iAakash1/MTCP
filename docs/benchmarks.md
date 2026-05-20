# MTCP Benchmark Results

## How to Run

```bash
# Quick: 100 clients
python3 tests/stress_test.py

# Full benchmark suite across client counts and thread configs
./tests/benchmark.sh

# High-concurrency (raise fd limit first)
ulimit -n 65535
python3 tests/stress_test.py 8080 500
```

## Interpreting Latency Percentiles

| Percentile | Meaning |
|---|---|
| **p50** | Half of connections complete faster than this |
| **p90** | 90% of connections complete faster than this |
| **p99** | 99% complete faster — this is "worst normal case" |
| **max** | Single worst connection (often a scheduling anomaly) |

For user-facing systems, p99 is the most important: it tells you what 1 in 100 users experiences.

## Latency vs. Thread Count

Run with `./server --threads N` and `python3 tests/stress_test.py 8080 100`:

| Threads | p50   | p99   | Max    | Notes |
|---------|-------|-------|--------|-------|
| 1       | 8.4ms | 82ms  | 141ms  | Single worker, severe queuing |
| 2       | 3.2ms | 22ms  | 48ms   | Better, still queuing visible |
| 4       | 1.8ms | 6.2ms | 14ms   | Default — sweet spot |
| 8       | 1.1ms | 3.8ms | 9ms    | Better for bursty traffic |
| 16      | 1.0ms | 3.4ms | 8ms    | Diminishing returns |

## Backpressure Under Overload

With `--queue-depth 10 --threads 2` and 100 clients:

```
Dropped (full Q)  : 88
Total connections : 12
```

This shows the bounded queue correctly shedding load instead of OOM-crashing.

## Memory Usage (Valgrind / /proc/self/status)

```
4 threads,  2MB stack:  VmRSS ~ 9MB
8 threads,  2MB stack:  VmRSS ~ 13MB
16 threads, 2MB stack:  VmRSS ~ 21MB
16 threads, 8MB stack:  VmRSS ~ 69MB  (default pthread stack)
```

## CPU Profile Notes

Under load, `perf top` typically shows:

```
  35%  __pthread_mutex_lock      ← queue mutex contention
  28%  __pthread_cond_wait       ← workers sleeping (expected)
  18%  sys_recvfrom              ← per-byte recvLine
  12%  sys_sendto                ← sendAll
   7%  other
```

The mutex is the primary bottleneck at high connection rates.
To reduce: implement per-shard queues or a lock-free MPMC ring buffer.
