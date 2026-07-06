#!/usr/bin/env python3
"""Stress / load test for MathExpressionParserServer.

Opens many concurrent TCP connections and repeatedly sends math expressions,
measuring throughput, latency and error rates.

Protocol (reverse-engineered from the server sources):
  * TCP, default port 8081.
  * A request is one math expression terminated by a newline ('\\n'). The
    server buffers incoming bytes and evaluates the whole buffer once the
    last received byte is '\\n'.
  * The minimum request size is 2 bytes (at least one character plus '\\n').
  * Integer-only arithmetic with the operators + - * / and parentheses ().
  * The response is the integer result as text (e.g. "42") or an error string
    that begins with "Error:". Responses are NOT newline-terminated, so this
    client treats the first read after a request as the complete response
    (responses are tiny and arrive in a single TCP segment).

Examples:
  # 100 connections, 10 seconds, random expressions
  python3 tests/stress_test.py --connections 100 --duration 10

  # 50k total requests across 200 connections, fixed expression
  python3 tests/stress_test.py -c 200 -n 50000 --expression "1+2*3"

  # Validate the correctness of every response
  python3 tests/stress_test.py -c 50 --duration 5 --validate
"""

from __future__ import annotations

import argparse
import asyncio
import random
import socket
import sys
import time
from dataclasses import dataclass, field
from typing import Optional


# --------------------------------------------------------------------------- #
# Statistics
# --------------------------------------------------------------------------- #
@dataclass
class Stats:
    attempted: int = 0          # requests we decided to send
    completed: int = 0          # round-trips that produced a response
    success: int = 0            # numeric result (and correct when validating)
    server_error: int = 0       # response started with "Error:"
    mismatch: int = 0           # numeric result but wrong value
    timeout: int = 0            # request timed out
    conn_error: int = 0         # could not connect / connection failed
    closed_early: int = 0       # connection closed while waiting for a reply
    latencies_ms: list[float] = field(default_factory=list)
    mismatches: list[tuple[str, int, str]] = field(default_factory=list)  # expr, expected, got


# --------------------------------------------------------------------------- #
# Expression generation
# --------------------------------------------------------------------------- #
class ExpressionFactory:
    """Generates valid integer infix expressions and their expected value.

    Division is never validated because C++ integer division truncates toward
    zero while Python's // truncates toward negative infinity; the expected
    value is therefore reported as None whenever '/' appears.
    """

    def __init__(self, ops: str, min_terms: int, max_terms: int,
                 max_operand: int, use_parens: bool):
        self.ops = ops
        self.min_terms = max(1, min_terms)
        self.max_terms = max(self.min_terms, max_terms)
        self.max_operand = max(1, max_operand)
        self.use_parens = use_parens

    def _number(self) -> str:
        return str(random.randint(0, self.max_operand))

    def _term(self, depth: int) -> str:
        if self.use_parens and depth > 0 and random.random() < 0.3:
            return "(" + self._expr(depth - 1) + ")"
        return self._number()

    def _expr(self, depth: int) -> str:
        count = random.randint(self.min_terms, self.max_terms)
        parts = [self._term(depth)]
        for _ in range(count - 1):
            parts.append(random.choice(self.ops))
            parts.append(self._term(depth))
        return "".join(parts)

    def make(self) -> tuple[str, Optional[int]]:
        expr = self._expr(2 if self.use_parens else 0)
        expected: Optional[int] = None
        if "/" not in expr:
            # Safe: expr contains only digits, spaces and + - * ( ).
            expected = eval(expr, {"__builtins__": {}}, {})  # noqa: S307
        return expr, expected


# --------------------------------------------------------------------------- #
# Worker
# --------------------------------------------------------------------------- #
async def worker(
    idx: int,
    host: str,
    port: int,
    stats: Stats,
    factory: Optional[ExpressionFactory],
    fixed: Optional[tuple[str, Optional[int]]],
    should_stop,
    req_timeout: float,
    connect_timeout: float,
    nodelay: bool,
    validate: bool,
) -> None:
    try:
        reader, writer = await asyncio.wait_for(
            asyncio.open_connection(host, port), timeout=connect_timeout
        )
    except (OSError, asyncio.TimeoutError):
        stats.conn_error += 1
        return

    if nodelay:
        sock = writer.get_extra_info("socket")
        if sock is not None:
            try:
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            except OSError:
                pass

    try:
        while not should_stop():
            stats.attempted += 1
            expr, expected = fixed if fixed is not None else factory.make()

            t0 = time.perf_counter()
            try:
                writer.write((expr + "\n").encode())
                await writer.drain()
                data = await asyncio.wait_for(reader.read(65536), timeout=req_timeout)
            except asyncio.TimeoutError:
                stats.timeout += 1
                break
            except (ConnectionError, OSError):
                stats.conn_error += 1
                break

            if not data:  # peer closed the connection
                stats.closed_early += 1
                break

            elapsed_ms = (time.perf_counter() - t0) * 1000.0
            stats.completed += 1
            stats.latencies_ms.append(elapsed_ms)

            text = data.decode(errors="replace").strip()
            if text.startswith("Error:"):
                stats.server_error += 1
            elif validate and expected is not None:
                try:
                    ok = int(text) == expected
                except ValueError:
                    ok = False
                if ok:
                    stats.success += 1
                else:
                    stats.mismatch += 1
                    if len(stats.mismatches) < 10:
                        stats.mismatches.append((expr, expected, text))
            else:
                stats.success += 1
    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except (OSError, asyncio.TimeoutError):
            pass


# --------------------------------------------------------------------------- #
# Live progress
# --------------------------------------------------------------------------- #
async def monitor(stats: Stats, stop_event: asyncio.Event, interval: float = 1.0) -> None:
    last_completed = 0
    last_t = time.perf_counter()
    try:
        while not stop_event.is_set():
            await asyncio.sleep(interval)
            now = time.perf_counter()
            cur = stats.completed
            rps = (cur - last_completed) / (now - last_t) if now > last_t else 0.0
            errors = stats.server_error + stats.mismatch + stats.timeout + stats.conn_error
            sys.stderr.write(
                f"\r  completed={cur:<10} {rps:>10.1f} req/s   errors={errors:<8}"
            )
            sys.stderr.flush()
            last_completed, last_t = cur, now
    except asyncio.CancelledError:
        pass
    finally:
        sys.stderr.write("\r" + " " * 60 + "\r")
        sys.stderr.flush()


# --------------------------------------------------------------------------- #
# Reporting
# --------------------------------------------------------------------------- #
def percentile(sorted_vals: list[float], p: float) -> float:
    if not sorted_vals:
        return 0.0
    if len(sorted_vals) == 1:
        return sorted_vals[0]
    k = (len(sorted_vals) - 1) * (p / 100.0)
    lo = int(k)
    hi = min(lo + 1, len(sorted_vals) - 1)
    frac = k - lo
    return sorted_vals[lo] * (1 - frac) + sorted_vals[hi] * frac


def report(stats: Stats, host: str, port: int, connections: int, elapsed: float,
           validate: bool) -> None:
    lat = sorted(stats.latencies_ms)
    mean = sum(lat) / len(lat) if lat else 0.0
    throughput = stats.completed / elapsed if elapsed > 0 else 0.0

    line = "-" * 50
    print()
    print("=" * 50)
    print("               Stress Test Report")
    print("=" * 50)
    print(f"Target            : {host}:{port}")
    print(f"Connections       : {connections}")
    print(f"Wall time         : {elapsed:.2f} s")
    print(line)
    print(f"Attempted         : {stats.attempted}")
    print(f"Completed         : {stats.completed}")
    print(f"Throughput        : {throughput:.1f} req/s")
    print(line)
    print(f"Success           : {stats.success}")
    if validate:
        print(f"Mismatches        : {stats.mismatch}")
    print(f"Server errors     : {stats.server_error}")
    print(f"Timeouts          : {stats.timeout}")
    print(f"Conn errors       : {stats.conn_error}")
    print(f"Closed early      : {stats.closed_early}")
    print(line)
    print("Latency (ms)")
    if lat:
        print(f"  min   : {lat[0]:.2f}")
        print(f"  mean  : {mean:.2f}")
        print(f"  p50   : {percentile(lat, 50):.2f}")
        print(f"  p90   : {percentile(lat, 90):.2f}")
        print(f"  p95   : {percentile(lat, 95):.2f}")
        print(f"  p99   : {percentile(lat, 99):.2f}")
        print(f"  max   : {lat[-1]:.2f}")
    else:
        print("  (no completed requests)")
    print("=" * 50)

    if validate and stats.mismatches:
        print("\nSample mismatches (expr => expected, got):")
        for expr, expected, got in stats.mismatches:
            print(f"  {expr} => {expected}, got {got!r}")


# --------------------------------------------------------------------------- #
# Entry point
# --------------------------------------------------------------------------- #
async def run(args: argparse.Namespace) -> int:
    stats = Stats()

    fixed: Optional[tuple[str, Optional[int]]] = None
    factory: Optional[ExpressionFactory] = None
    if args.expression is not None:
        expr = args.expression
        expected = None
        if args.validate and "/" not in expr:
            try:
                expected = eval(expr, {"__builtins__": {}}, {})  # noqa: S307
            except Exception:
                expected = None
        fixed = (expr, expected)
    else:
        factory = ExpressionFactory(
            ops=args.ops,
            min_terms=args.min_terms,
            max_terms=args.max_terms,
            max_operand=args.max_operand,
            use_parens=args.parens,
        )

    # Stop condition: request count and/or deadline.
    deadline = None
    if args.duration is not None:
        deadline = time.monotonic() + args.duration
    target = args.requests

    def should_stop() -> bool:
        if target is not None and stats.attempted >= target:
            return True
        if deadline is not None and time.monotonic() >= deadline:
            return True
        return False

    print(f"Starting stress test against {args.host}:{args.port}")
    print(f"  connections={args.connections}  "
          f"{'requests=' + str(target) if target else 'duration=' + str(args.duration) + 's'}  "
          f"validate={args.validate}")

    stop_event = asyncio.Event()
    mon = None
    if not args.quiet:
        mon = asyncio.create_task(monitor(stats, stop_event))

    start = time.perf_counter()
    workers = [
        asyncio.create_task(
            worker(
                idx=i,
                host=args.host,
                port=args.port,
                stats=stats,
                factory=factory,
                fixed=fixed,
                should_stop=should_stop,
                req_timeout=args.timeout,
                connect_timeout=args.connect_timeout,
                nodelay=not args.no_nodelay,
                validate=args.validate,
            )
        )
        for i in range(args.connections)
    ]

    try:
        await asyncio.gather(*workers)
    except KeyboardInterrupt:
        for w in workers:
            w.cancel()
    elapsed = time.perf_counter() - start

    stop_event.set()
    if mon is not None:
        mon.cancel()
        await asyncio.gather(mon, return_exceptions=True)

    report(stats, args.host, args.port, args.connections, elapsed, args.validate)

    # Non-zero exit if nothing completed or validation found problems.
    if stats.completed == 0:
        return 2
    if args.validate and stats.mismatch > 0:
        return 1
    return 0


def parse_args(argv: Optional[list[str]] = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Stress / load test for MathExpressionParserServer.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--host", default="127.0.0.1", help="server host")
    p.add_argument("--port", "-P", type=int, default=8081, help="server port")
    p.add_argument("-c", "--connections", type=int, default=50,
                   help="number of concurrent connections")

    stop = p.add_argument_group("stop condition (default: --duration 10)")
    stop.add_argument("-d", "--duration", type=float,
                      help="run for this many seconds")
    stop.add_argument("-n", "--requests", type=int,
                      help="stop after this many total requests")

    gen = p.add_argument_group("expression generation")
    gen.add_argument("--expression", help="send this fixed expression every time")
    gen.add_argument("--ops", default="+-*",
                     help="operators to pick from (subset of +-*/)")
    gen.add_argument("--min-terms", type=int, default=3,
                     help="minimum operands per expression")
    gen.add_argument("--max-terms", type=int, default=7,
                     help="maximum operands per expression")
    gen.add_argument("--max-operand", type=int, default=1000,
                     help="operands are random ints in [0, max-operand]")
    gen.add_argument("--parens", action="store_true",
                     help="include parentheses in generated expressions")

    p.add_argument("--validate", action="store_true",
                   help="check each numeric result against the expected value "
                        "(division is not validated)")
    p.add_argument("--timeout", type=float, default=10.0,
                   help="per-request timeout in seconds")
    p.add_argument("--connect-timeout", type=float, default=10.0,
                   help="connection timeout in seconds")
    p.add_argument("--no-nodelay", action="store_true",
                   help="do NOT set TCP_NODELAY on client sockets")
    p.add_argument("--quiet", action="store_true",
                   help="suppress the live progress line")

    args = p.parse_args(argv)
    if args.duration is None and args.requests is None:
        args.duration = 10.0
    if args.validate and args.expression is None:
        # Keep validated arithmetic away from long-long overflow.
        args.max_operand = min(args.max_operand, 99)
        args.max_terms = min(args.max_terms, 7)
        args.ops = "".join(ch for ch in args.ops if ch in "+-*") or "+-*"
    return args


def main() -> None:
    args = parse_args()
    try:
        rc = asyncio.run(run(args))
    except KeyboardInterrupt:
        rc = 130
    sys.exit(rc)


if __name__ == "__main__":
    main()
