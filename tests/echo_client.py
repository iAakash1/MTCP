#!/usr/bin/env python3
"""
echo_client.py — Single-client interactive/functional test.

Tests the persistent connection and request-response loop.
Can be used interactively (no arguments) or programmatically.

Usage:
    python3 tests/echo_client.py             # default port 8080
    python3 tests/echo_client.py 9000        # custom port
    python3 tests/echo_client.py 8080 auto   # run automated test sequence
"""

import socket
import sys
import time

HOST = "localhost"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
MODE = sys.argv[2] if len(sys.argv) > 2 else "interactive"


def recv_line(sock: socket.socket) -> str:
    """Read until newline from socket."""
    data = b""
    while not data.endswith(b"\n"):
        chunk = sock.recv(1)
        if not chunk:
            break
        data += chunk
    return data.decode("utf-8", errors="replace").strip()


def run_automated():
    """Run a scripted sequence of requests and verify responses."""
    print(f"[echo_client] Connecting to {HOST}:{PORT} (automated mode)")

    test_messages = [
        "Hello, MTCP!",
        "Testing persistent connection",
        "Message number three",
        "Unicode test: こんにちは",
        "Empty-ish: .",
    ]

    passed = 0
    failed = 0

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(5)
        sock.connect((HOST, PORT))

        greeting = recv_line(sock)
        print(f"  Server greeting: {greeting}")

        for i, msg in enumerate(test_messages):
            sock.sendall((msg + "\n").encode("utf-8"))
            response = recv_line(sock)
            expected_fragment = msg

            if expected_fragment in response:
                print(f"  \033[32m✅ [{i+1}] OK: {response}\033[0m")
                passed += 1
            else:
                print(f"  \033[31m❌ [{i+1}] FAIL: sent={msg!r} got={response!r}\033[0m")
                failed += 1

        # Graceful close
        sock.sendall(b"quit\n")
        bye = recv_line(sock)
        print(f"  Server: {bye}")

    print(f"\n  Results: {passed} passed, {failed} failed")
    sys.exit(0 if failed == 0 else 1)


def run_interactive():
    """Interactive REPL — type messages, see echo."""
    print(f"[echo_client] Connecting to {HOST}:{PORT}")
    print("  Type messages and press Enter.  Type 'quit' to exit.\n")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(10)
        sock.connect((HOST, PORT))

        greeting = recv_line(sock)
        print(f"Server: {greeting}\n")

        while True:
            try:
                line = input("You: ").strip()
            except (EOFError, KeyboardInterrupt):
                print()
                break

            if not line:
                continue

            sock.sendall((line + "\n").encode("utf-8"))

            if line.lower() in ("quit", "exit"):
                response = recv_line(sock)
                print(f"Server: {response}")
                break

            response = recv_line(sock)
            print(f"Server: {response}")

    print("[echo_client] Connection closed.")


if __name__ == "__main__":
    if MODE == "auto":
        run_automated()
    else:
        run_interactive()
