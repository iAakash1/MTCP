#!/usr/bin/env python3
"""
stress_test.py — Concurrent stress test for the Multithreaded TCP Server.

Creates 100 concurrent clients, each:
  1. Connects to localhost:8080
  2. Receives the server greeting
  3. Sends a unique message
  4. Receives the echo
  5. Verifies the echo matches
  6. Closes the connection

Usage:
    python3 tests/stress_test.py

Expected output:
    ✅ Client 0: OK (echo verified)
    ✅ Client 1: OK (echo verified)
    ...
    ════════════════════════════════════════
    STRESS TEST RESULTS
    ────────────────────────────────────────
    Total clients : 100
    Successes     : 100
    Failures      : 0
    Duration      : 0.42s
    ════════════════════════════════════════
"""

import socket
import threading
import time
import sys

# ── Configuration ─────────────────────────────────────────────────────────────
HOST = "localhost"
PORT = 8080
NUM_CLIENTS = 100
TIMEOUT = 5  # seconds per client

# ── Shared counters (protected by lock) ───────────────────────────────────────
lock = threading.Lock()
successes = 0
failures = 0
errors = []


def client_task(client_id: int) -> None:
    """Simulate a single client connection."""
    global successes, failures

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(TIMEOUT)
            sock.connect((HOST, PORT))

            # Receive server greeting
            greeting = sock.recv(4096).decode("utf-8", errors="replace")
            if not greeting:
                raise ConnectionError("No greeting received")

            # Send a unique message
            message = f"Hello from client {client_id}\n"
            sock.sendall(message.encode("utf-8"))

            # Receive echo
            response = sock.recv(4096).decode("utf-8", errors="replace")
            if not response:
                raise ConnectionError("No echo received")

            # Verify echo contains our message
            expected = f"Echo: Hello from client {client_id}"
            if expected in response:
                with lock:
                    successes += 1
                print(f"  ✅ Client {client_id:3d}: OK (echo verified)")
            else:
                with lock:
                    failures += 1
                    errors.append(f"Client {client_id}: unexpected response: {response!r}")
                print(f"  ❌ Client {client_id:3d}: MISMATCH")

    except Exception as e:
        with lock:
            failures += 1
            errors.append(f"Client {client_id}: {type(e).__name__}: {e}")
        print(f"  ❌ Client {client_id:3d}: {type(e).__name__}: {e}")


def main() -> None:
    print("═" * 50)
    print("  STRESS TEST — Multithreaded TCP Server")
    print(f"  Target : {HOST}:{PORT}")
    print(f"  Clients: {NUM_CLIENTS}")
    print("═" * 50)
    print()

    start_time = time.time()

    # Launch all clients concurrently
    threads = []
    for i in range(NUM_CLIENTS):
        t = threading.Thread(target=client_task, args=(i,), daemon=True)
        threads.append(t)
        t.start()

    # Wait for all to finish
    for t in threads:
        t.join(timeout=TIMEOUT + 2)

    elapsed = time.time() - start_time

    # Print results
    print()
    print("═" * 50)
    print("  STRESS TEST RESULTS")
    print("─" * 50)
    print(f"  Total clients : {NUM_CLIENTS}")
    print(f"  Successes     : {successes}")
    print(f"  Failures      : {failures}")
    print(f"  Duration      : {elapsed:.2f}s")
    print("═" * 50)

    if errors:
        print("\n  Errors:")
        for err in errors[:10]:  # Show first 10
            print(f"    • {err}")
        if len(errors) > 10:
            print(f"    ... and {len(errors) - 10} more")

    # Exit code for CI/CD
    sys.exit(0 if failures == 0 else 1)


if __name__ == "__main__":
    main()
