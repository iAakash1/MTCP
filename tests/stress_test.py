#!/usr/bin/env python3
"""
stress_test.py — Concurrent stress test with p50/p90/p99 latency histograms.

Creates N concurrent clients, each:
  1. Connects to the server
  2. Receives the greeting
  3. Sends a unique message line
  4. Receives the echo response
  5. Verifies correctness
  6. Sends 'quit' to close cleanly
  7. Records round-trip latency

Usage:
    python3 tests/stress_test.py
    python3 tests/stress_test.py 9000          # custom port
    python3 tests/stress_test.py 9000 200      # port + client count

Expected output:
    ✅ Client   0: OK  (3.2ms)
    ✅ Client   1: OK  (4.1ms)
    ...
    ══════════════════════════════════════════════
      RESULTS
      Successes  : 100 / 100
      Duration   : 0.41s
      Throughput : 243.9 conn/s
      LATENCY (successful clients)
        p50  : 3.1ms
        p90  : 5.2ms
        p99  : 8.7ms
        min  : 1.1ms
        max  : 12.3ms
        mean : 3.4ms
"""

import socket
import threading
import time
import sys
import statistics

# ── Configuration ──────────────────────────────────────────────────────────────
HOST        = "localhost"
PORT        = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
NUM_CLIENTS = int(sys.argv[2]) if len(sys.argv) > 2 else 100
TIMEOUT     = 10  # seconds per client

# ── Shared state (protected by lock) ──────────────────────────────────────────
lock      = threading.Lock()
successes = 0
failures  = 0
errors    = []
latencies = []  # round-trip latency per successful connection (ms)


def client_task(client_id: int) -> None:
    global successes, failures
    t_start = time.perf_counter()

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(TIMEOUT)
            sock.connect((HOST, PORT))

            # Receive server greeting
            greeting = sock.recv(4096).decode("utf-8", errors="replace")
            if not greeting:
                raise ConnectionError("No greeting received")

            # Send a unique, newline-terminated message
            message = f"Hello from client {client_id}\n"
            sock.sendall(message.encode("utf-8"))

            # Receive echo
            response = sock.recv(4096).decode("utf-8", errors="replace")
            if not response:
                raise ConnectionError("No echo received")

            elapsed_ms = (time.perf_counter() - t_start) * 1000.0

            # Verify the echo contains our sent text
            expected = f"Hello from client {client_id}"
            if expected in response:
                with lock:
                    successes += 1
                    latencies.append(elapsed_ms)
                print(f"  \033[32m✅ Client {client_id:4d}: OK  ({elapsed_ms:.1f}ms)\033[0m")
            else:
                with lock:
                    failures += 1
                    errors.append(
                        f"Client {client_id}: unexpected response: {response!r}"
                    )
                print(f"  \033[31m❌ Client {client_id:4d}: MISMATCH\033[0m")

            # Graceful quit
            try:
                sock.sendall(b"quit\n")
                sock.recv(64)   # consume "Goodbye!\r\n"
            except Exception:
                pass

    except Exception as exc:
        with lock:
            failures += 1
            errors.append(f"Client {client_id}: {type(exc).__name__}: {exc}")
        print(f"  \033[31m❌ Client {client_id:4d}: {type(exc).__name__}: {exc}\033[0m")


def percentile(data: list, p: float) -> float:
    if not data:
        return 0.0
    data = sorted(data)
    idx  = max(0, int(len(data) * p / 100) - 1)
    return data[idx]


def main() -> None:
    print("═" * 52)
    print(f"  MTCP STRESS TEST")
    print(f"  Target  : {HOST}:{PORT}")
    print(f"  Clients : {NUM_CLIENTS}")
    print(f"  Timeout : {TIMEOUT}s per client")
    print("═" * 52)
    print()

    wall_start = time.time()

    # Launch all clients concurrently
    threads = [
        threading.Thread(target=client_task, args=(i,), daemon=True)
        for i in range(NUM_CLIENTS)
    ]
    for t in threads:
        t.start()
    for t in threads:
        t.join(timeout=TIMEOUT + 2)

    wall_elapsed = time.time() - wall_start
    throughput   = successes / wall_elapsed if wall_elapsed > 0 else 0

    print()
    print("═" * 52)
    print("  RESULTS")
    print("─" * 52)
    print(f"  Successes  : {successes} / {NUM_CLIENTS}")
    print(f"  Failures   : {failures}")
    print(f"  Duration   : {wall_elapsed:.2f}s")
    print(f"  Throughput : {throughput:.1f} conn/s")

    if latencies:
        print()
        print("  LATENCY (successful connections)")
        print(f"    p50  : {percentile(latencies, 50):.1f}ms")
        print(f"    p90  : {percentile(latencies, 90):.1f}ms")
        print(f"    p99  : {percentile(latencies, 99):.1f}ms")
        print(f"    min  : {min(latencies):.1f}ms")
        print(f"    max  : {max(latencies):.1f}ms")
        print(f"    mean : {statistics.mean(latencies):.1f}ms")
        if len(latencies) > 1:
            print(f"    stdev: {statistics.stdev(latencies):.1f}ms")

    print("═" * 52)

    if errors:
        print("\n  Errors (first 10):")
        for err in errors[:10]:
            print(f"    • {err}")
        if len(errors) > 10:
            print(f"    ... and {len(errors) - 10} more")

    sys.exit(0 if failures == 0 else 1)


if __name__ == "__main__":
    main()
