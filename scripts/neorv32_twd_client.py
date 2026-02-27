#!/usr/bin/env python3
"""Tiny client for QEMU i2c-master-chardev socket.

Usage examples:
  python3 scripts/neorv32_twd_client.py --socket /tmp/twd-i2c.sock
  python3 scripts/neorv32_twd_client.py --socket /tmp/twd-i2c.sock --cmd "W 0x52 03 11 22 33"
  python3 scripts/neorv32_twd_client.py --socket /tmp/twd-i2c.sock --cmd "R 0x52 04"
"""

from __future__ import annotations

import argparse
import socket
import sys
from typing import Optional


def recv_line(sock: socket.socket) -> str:
    data = bytearray()
    while True:
        chunk = sock.recv(1)
        if not chunk:
            if data:
                return data.decode("utf-8", errors="replace")
            raise ConnectionError("socket closed by peer")
        if chunk == b"\n":
            return data.decode("utf-8", errors="replace")
        if chunk != b"\r":
            data.extend(chunk)


def send_cmd(sock: socket.socket, cmd: str) -> str:
    payload = cmd.strip() + "\n"
    sock.sendall(payload.encode("utf-8"))
    return recv_line(sock)


def interactive_loop(sock: socket.socket) -> int:
    print("Connected. Enter commands like:")
    print("  W 0x52 03 11 22 33")
    print("  R 0x52 04")
    print("Type 'quit' or 'exit' to leave.")

    while True:
        try:
            line = input("twd> ").strip()
        except EOFError:
            print()
            return 0
        except KeyboardInterrupt:
            print()
            return 0

        if not line:
            continue
        if line.lower() in {"quit", "exit"}:
            return 0

        try:
            reply = send_cmd(sock, line)
            print(reply)
        except Exception as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 1


def run(socket_path: str, one_shot_cmd: Optional[str]) -> int:
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.connect(socket_path)

        if one_shot_cmd is not None:
            reply = send_cmd(sock, one_shot_cmd)
            print(reply)
            return 0

        return interactive_loop(sock)


def main() -> int:
    parser = argparse.ArgumentParser(description="NEORV32 TWD host client")
    parser.add_argument(
        "--socket",
        default="/tmp/twd-i2c.sock",
        help="UNIX socket path used by -chardev socket,path=...",
    )
    parser.add_argument(
        "--cmd",
        default=None,
        help="send a single command and exit",
    )

    args = parser.parse_args()

    try:
        return run(args.socket, args.cmd)
    except FileNotFoundError:
        print(f"ERROR: socket not found: {args.socket}", file=sys.stderr)
        return 2
    except ConnectionRefusedError:
        print(f"ERROR: cannot connect to socket: {args.socket}", file=sys.stderr)
        return 2
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
