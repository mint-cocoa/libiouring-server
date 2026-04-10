#!/usr/bin/env python3
"""
Echo server test client for EchoServer.

Packet framing: [uint16 size][uint16 id][body]
  size = 4 (header) + len(body)

Usage:
  # 1) 기본 기능 테스트 (단일 연결, 10 패킷)
  python3 tools/echo_test.py --port 9100

  # 2) 동시 접속 테스트
  python3 tools/echo_test.py --port 9100 --clients 100 --packets 100

  # 3) 벤치마크
  python3 tools/echo_test.py --port 9100 --clients 50 --packets 500 --label EchoServer

  # 4) 페이로드 크기 변경
  python3 tools/echo_test.py --port 9100 --payload-size 1024
"""

import argparse
import socket
import struct
import threading
import time
import sys
import statistics

HEADER_FMT = "<HH"  # little-endian uint16 size, uint16 id
HEADER_SIZE = struct.calcsize(HEADER_FMT)


def make_packet(pkt_id: int, body: bytes) -> bytes:
    total = HEADER_SIZE + len(body)
    header = struct.pack(HEADER_FMT, total, pkt_id)
    return header + body


def recv_packet(sock: socket.socket) -> tuple[int, bytes]:
    """Receive one framed packet. Returns (pkt_id, body)."""
    # Read header
    hdr_buf = b""
    while len(hdr_buf) < HEADER_SIZE:
        chunk = sock.recv(HEADER_SIZE - len(hdr_buf))
        if not chunk:
            raise ConnectionError("connection closed while reading header")
        hdr_buf += chunk

    total_size, pkt_id = struct.unpack(HEADER_FMT, hdr_buf)
    body_size = total_size - HEADER_SIZE

    # Read body
    body = b""
    while len(body) < body_size:
        chunk = sock.recv(body_size - len(body))
        if not chunk:
            raise ConnectionError("connection closed while reading body")
        body += chunk

    return pkt_id, body


class ClientResult:
    def __init__(self):
        self.sent = 0
        self.received = 0
        self.errors = 0
        self.rtts_us: list[float] = []  # round-trip times in microseconds


def run_client(host: str, port: int, num_packets: int, payload_size: int,
               pkt_id: int, result: ClientResult):
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.settimeout(5.0)
        sock.connect((host, port))

        body = bytes(range(256)) * (payload_size // 256 + 1)
        body = body[:payload_size]

        for i in range(num_packets):
            pkt = make_packet(pkt_id, body)

            t0 = time.monotonic()
            sock.sendall(pkt)
            result.sent += 1

            recv_id, recv_body = recv_packet(sock)
            t1 = time.monotonic()

            result.received += 1
            result.rtts_us.append((t1 - t0) * 1_000_000)

            # Verify echo
            if recv_id != pkt_id:
                result.errors += 1
                print(f"  [ERROR] pkt_id mismatch: sent={pkt_id} recv={recv_id}")
            if recv_body != body:
                result.errors += 1
                print(f"  [ERROR] body mismatch at packet {i}")

        sock.close()
    except Exception as e:
        result.errors += 1
        print(f"  [ERROR] client exception: {e}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description="Echo server test client")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9100)
    parser.add_argument("--clients", type=int, default=1,
                        help="Number of concurrent clients")
    parser.add_argument("--packets", type=int, default=10,
                        help="Packets per client")
    parser.add_argument("--payload-size", type=int, default=64,
                        help="Payload body size in bytes")
    parser.add_argument("--pkt-id", type=int, default=1,
                        help="Packet type ID to use")
    parser.add_argument("--label", default="",
                        help="Label for output (e.g. 'Separated')")
    args = parser.parse_args()

    label = f" [{args.label}]" if args.label else ""
    print(f"=== Echo Test{label} ===")
    print(f"Target: {args.host}:{args.port}")
    print(f"Clients: {args.clients}, Packets/client: {args.packets}, "
          f"Payload: {args.payload_size}B")
    print()

    results = [ClientResult() for _ in range(args.clients)]
    threads = []

    t_start = time.monotonic()

    for i in range(args.clients):
        t = threading.Thread(
            target=run_client,
            args=(args.host, args.port, args.packets, args.payload_size,
                  args.pkt_id, results[i]),
            daemon=True,
        )
        threads.append(t)
        t.start()

    for t in threads:
        t.join(timeout=30)

    t_end = time.monotonic()
    elapsed = t_end - t_start

    # Aggregate
    total_sent = sum(r.sent for r in results)
    total_recv = sum(r.received for r in results)
    total_errors = sum(r.errors for r in results)
    all_rtts = []
    for r in results:
        all_rtts.extend(r.rtts_us)

    total_packets = args.clients * args.packets
    throughput = total_recv / elapsed if elapsed > 0 else 0

    print(f"--- Results{label} ---")
    print(f"Sent:     {total_sent}/{total_packets}")
    print(f"Received: {total_recv}/{total_packets}")
    print(f"Errors:   {total_errors}")
    print(f"Elapsed:  {elapsed*1000:.1f} ms")
    print(f"Throughput: {throughput:.0f} echo/s")

    if all_rtts:
        print()
        print(f"RTT (us):")
        print(f"  min:    {min(all_rtts):.0f}")
        print(f"  median: {statistics.median(all_rtts):.0f}")
        print(f"  p95:    {sorted(all_rtts)[int(len(all_rtts)*0.95)]:.0f}")
        print(f"  p99:    {sorted(all_rtts)[int(len(all_rtts)*0.99)]:.0f}")
        print(f"  max:    {max(all_rtts):.0f}")
        print(f"  avg:    {statistics.mean(all_rtts):.0f}")

    if total_errors == 0 and total_recv == total_packets:
        print(f"\n  OK — all {total_packets} packets echoed correctly.")
    else:
        print(f"\n  FAIL — {total_errors} errors, "
              f"{total_packets - total_recv} missing.")
        sys.exit(1)


if __name__ == "__main__":
    main()
