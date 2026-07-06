#!/usr/bin/env python3
"""Large-input (up to 1 GB) test for MathExpressionParserServer.

Verifies the requirement: "Input expression could be up to 1GB. Processing 1 Gb
of expression shouldn't take more than several minutes."

What it does
------------
1. Opens a single TCP connection.
2. (optional) Sends a tiny warm-up expression ("2+3\\n" -> "5") to confirm the
   server is alive and that the socket stays open for further expressions.
3. Streams one very large expression (default ~1 GiB) terminated by '\\n',
   generated on the fly in fixed-size chunks so the client never holds the whole
   payload in memory.
4. Reads the single result the server writes back (results are not newline
   terminated) and measures end-to-end processing time and throughput.
5. Optionally validates the numeric result against the known expected value and
   fails if processing exceeded the time budget.

The built-in workloads are chosen so the expected value is known in closed form
and never overflows the server's 64-bit (long long) accumulator:

  ones   : 1+1+1+...+1              expected = number_of_ones
  groups : (2*3-1)+(2*3-1)+...      expected = 5 * number_of_groups
           exercises + - * and () / operator priorities at scale

Examples
--------
  # Full 1 GiB run against a local server, must finish within 5 minutes
  python3 tests/large_expression_test.py --size 1GiB --max-seconds 300

  # Quicker 100 MB smoke test exercising priorities and brackets
  python3 tests/large_expression_test.py --size 100MB --pattern groups

Exit codes: 0 ok | 1 wrong/error result | 2 timeout or too slow | 3 connection error
"""

from __future__ import annotations

import argparse
import asyncio
import re
import socket
import sys
import time
from dataclasses import dataclass
from typing import Optional


# --------------------------------------------------------------------------- #
# Workload patterns
# --------------------------------------------------------------------------- #
@dataclass
class Workload:
    """A streamable expression: `count` repetitions of `unit` then `final`."""
    unit: bytes            # repeated body element, e.g. b"1+"
    final: bytes           # terminator that closes the expression, ends with b"\n"
    count: int             # number of `unit` repetitions before `final`
    total_bytes: int       # exact bytes that will be sent (including final)
    expected: int          # expected integer result
    description: str


def build_workload(pattern: str, size_bytes: int) -> Workload:
    if pattern == "ones":
        # 1+1+...+1\n  -> sum == number of ones
        unit, final = b"1+", b"1\n"
        # total = 2*count + 2  =>  count = (size - 2) // 2
        count = max(0, (size_bytes - len(final)) // len(unit))
        total = len(unit) * count + len(final)
        ones = count + 1
        return Workload(unit, final, count, total, ones,
                        f"1+1+...+1  ({ones} ones)")
    if pattern == "groups":
        # (2*3-1)+(2*3-1)+...+(2*3-1)\n  -> 5 per group
        unit, final = b"(2*3-1)+", b"(2*3-1)\n"  # both 8 bytes
        # total = 8*(groups-1) + 8 = 8*groups  =>  groups = size // 8
        groups = max(1, size_bytes // len(unit))
        count = groups - 1
        total = len(unit) * count + len(final)
        return Workload(unit, final, count, total, 5 * groups,
                        f"(2*3-1)+...  ({groups} groups, =5 each)")
    raise ValueError(f"unknown pattern: {pattern}")


# --------------------------------------------------------------------------- #
# Size parsing
# --------------------------------------------------------------------------- #
_UNITS = {
    "": 1, "b": 1,
    "kb": 1000, "mb": 1000**2, "gb": 1000**3,
    "kib": 1024, "mib": 1024**2, "gib": 1024**3,
    "k": 1024, "m": 1024**2, "g": 1024**3,
}


def parse_size(text: str) -> int:
    m = re.fullmatch(r"\s*([0-9]*\.?[0-9]+)\s*([a-zA-Z]*)\s*", text)
    if not m:
        raise argparse.ArgumentTypeError(f"invalid size: {text!r}")
    value, suffix = float(m.group(1)), m.group(2).lower()
    if suffix not in _UNITS:
        raise argparse.ArgumentTypeError(
            f"unknown size suffix {suffix!r} (use B/KB/MB/GB or KiB/MiB/GiB)")
    n = int(value * _UNITS[suffix])
    if n < 2:
        raise argparse.ArgumentTypeError("size must be at least 2 bytes")
    return n


def human_bytes(n: float) -> str:
    for unit in ("B", "KiB", "MiB", "GiB", "TiB"):
        if abs(n) < 1024 or unit == "TiB":
            return f"{n:.2f} {unit}" if unit != "B" else f"{int(n)} B"
        n /= 1024
    return f"{n:.2f} TiB"


# --------------------------------------------------------------------------- #
# Shared progress state
# --------------------------------------------------------------------------- #
@dataclass
class Progress:
    total_bytes: int
    sent: int = 0
    send_done: bool = False


# --------------------------------------------------------------------------- #
# Networking helpers
# --------------------------------------------------------------------------- #
async def read_result(reader: asyncio.StreamReader, first_timeout: float,
                      quiet: float = 0.25) -> Optional[str]:
    """Read a full (non-delimited) response.

    Blocks up to `first_timeout` for the first byte, then keeps reading until
    `quiet` seconds pass with no new data (results are tiny and unframed).
    Returns None if the peer closed the connection without sending anything.
    """
    data = await asyncio.wait_for(reader.read(4096), timeout=first_timeout)
    if not data:
        return None
    buf = bytearray(data)
    while True:
        try:
            more = await asyncio.wait_for(reader.read(4096), timeout=quiet)
        except asyncio.TimeoutError:
            break
        if not more:
            break
        buf += more
    return buf.decode(errors="replace").strip()


async def stream_expression(writer: asyncio.StreamWriter, wl: Workload,
                            chunk_bytes: int, progress: Progress) -> None:
    """Send `wl` to the socket in chunks, applying write backpressure."""
    units_per_block = max(1, chunk_bytes // len(wl.unit))
    block = wl.unit * units_per_block
    remaining = wl.count
    while remaining > 0:
        k = min(units_per_block, remaining)
        payload = block if k == units_per_block else wl.unit * k
        writer.write(payload)
        await writer.drain()
        remaining -= k
        progress.sent += len(payload)
    writer.write(wl.final)
    await writer.drain()
    progress.sent += len(wl.final)
    progress.send_done = True


async def monitor(progress: Progress, stop: asyncio.Event, interval: float = 1.0) -> None:
    last_sent, last_t = 0, time.perf_counter()
    try:
        while not stop.is_set():
            await asyncio.sleep(interval)
            now = time.perf_counter()
            rate = (progress.sent - last_sent) / (now - last_t) if now > last_t else 0
            pct = (progress.sent / progress.total_bytes * 100) if progress.total_bytes else 0
            phase = "waiting for result" if progress.send_done else "sending"
            sys.stderr.write(
                f"\r  {phase}: {human_bytes(progress.sent)} "
                f"({pct:5.1f}%)  {human_bytes(rate)}/s   ")
            sys.stderr.flush()
            last_sent, last_t = progress.sent, now
    except asyncio.CancelledError:
        pass
    finally:
        sys.stderr.write("\r" + " " * 72 + "\r")
        sys.stderr.flush()


# --------------------------------------------------------------------------- #
# Test steps
# --------------------------------------------------------------------------- #
async def warmup(reader, writer, timeout: float) -> Optional[str]:
    """Confirm liveness and that the socket keeps serving. Returns error text or None."""
    writer.write(b"2+3\n")
    await writer.drain()
    try:
        result = await read_result(reader, first_timeout=timeout)
    except asyncio.TimeoutError:
        return "warm-up timed out (no response to '2+3')"
    if result is None:
        return "server closed the connection during warm-up"
    if result != "5":
        return f"warm-up expected '5', got {result!r}"
    return None


async def run(args: argparse.Namespace) -> int:
    wl = build_workload(args.pattern, args.size)
    chunk_bytes = max(len(wl.unit), int(args.chunk_mb * 1024 * 1024))

    print(f"Target        : {args.host}:{args.port}")
    print(f"Pattern       : {args.pattern}  [{wl.description}]")
    print(f"Payload       : {human_bytes(wl.total_bytes)} ({wl.total_bytes} bytes)")
    print(f"Expected value: {wl.expected}")
    print(f"Time budget   : {args.max_seconds:.0f} s")
    print("-" * 56)

    # Connect.
    try:
        reader, writer = await asyncio.wait_for(
            asyncio.open_connection(args.host, args.port),
            timeout=args.connect_timeout)
    except (OSError, asyncio.TimeoutError) as exc:
        print(f"CONNECTION ERROR: {exc}")
        return 3

    sock = writer.get_extra_info("socket")
    if sock is not None:
        try:
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        except OSError:
            pass

    try:
        # Warm-up on the same socket.
        if args.warmup:
            err = await warmup(reader, writer, args.connect_timeout)
            if err:
                print(f"WARM-UP FAILED: {err}")
                return 1
            print("Warm-up       : ok (2+3 => 5, socket kept open)")

        progress = Progress(total_bytes=wl.total_bytes)
        stop = asyncio.Event()
        mon = None if args.quiet else asyncio.create_task(monitor(progress, stop))

        async def transfer() -> Optional[str]:
            send_start = time.perf_counter()
            await stream_expression(writer, wl, chunk_bytes, progress)
            nonlocal send_time
            send_time = time.perf_counter() - send_start
            # Remaining time in the budget is used to wait for the result.
            remaining = max(1.0, args.max_seconds - send_time)
            return await read_result(reader, first_timeout=remaining)

        send_time = 0.0
        start = time.perf_counter()
        try:
            result = await asyncio.wait_for(transfer(), timeout=args.max_seconds)
        except asyncio.TimeoutError:
            result = None
            timed_out = True
        else:
            timed_out = False
        total_time = time.perf_counter() - start

        stop.set()
        if mon is not None:
            mon.cancel()
            await asyncio.gather(mon, return_exceptions=True)

        # Report.
        rate = wl.total_bytes / total_time if total_time > 0 else 0
        print(f"Sent          : {human_bytes(progress.sent)} in {send_time:.2f} s")
        print(f"Total time    : {total_time:.2f} s")
        print(f"Throughput    : {human_bytes(rate)}/s")
        print("-" * 56)

        if timed_out or result is None:
            if timed_out:
                print(f"RESULT: TIMEOUT — exceeded {args.max_seconds:.0f}s budget "
                      f"(sent {human_bytes(progress.sent)})")
            else:
                print("RESULT: FAIL — server closed connection without replying")
            return 2

        print(f"Server result : {result!r}")
        if result.startswith("Error:"):
            print("RESULT: FAIL — server returned an error for a valid expression")
            return 1

        if args.validate:
            try:
                ok = int(result) == wl.expected
            except ValueError:
                ok = False
            if not ok:
                print(f"RESULT: FAIL — expected {wl.expected}, got {result!r}")
                return 1
            print(f"RESULT: PASS — correct value in {total_time:.2f}s "
                  f"(budget {args.max_seconds:.0f}s)")
        else:
            print(f"RESULT: OK — response received in {total_time:.2f}s (not validated)")
        return 0
    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except (OSError, asyncio.TimeoutError):
            pass


def parse_args(argv: Optional[list[str]] = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Large-input (up to 1 GB) test for MathExpressionParserServer.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    p.add_argument("--host", default="127.0.0.1", help="server host")
    p.add_argument("--port", "-P", type=int, default=8081, help="server port")
    p.add_argument("--size", type=parse_size, default="1GiB",
                   help="expression size, e.g. 1GiB, 1GB, 500MB, 1048576")
    p.add_argument("--pattern", choices=("ones", "groups"), default="ones",
                   help="ones: 1+1+...+1 ; groups: (2*3-1)+... (tests priorities)")
    p.add_argument("--max-seconds", type=float, default=300.0,
                   help="processing time budget (fail if exceeded)")
    p.add_argument("--chunk-mb", type=float, default=8.0,
                   help="send buffer chunk size in MB")
    p.add_argument("--connect-timeout", type=float, default=15.0,
                   help="connection / warm-up timeout in seconds")
    p.add_argument("--validate", action=argparse.BooleanOptionalAction, default=True,
                   help="validate the numeric result (default: on)")
    p.add_argument("--warmup", action=argparse.BooleanOptionalAction, default=True,
                   help="send a tiny warm-up expression first (default: on)")
    p.add_argument("--quiet", action="store_true", help="suppress the live progress line")
    return p.parse_args(argv)


def main() -> None:
    args = parse_args()
    try:
        rc = asyncio.run(run(args))
    except KeyboardInterrupt:
        rc = 130
    sys.exit(rc)


if __name__ == "__main__":
    main()
