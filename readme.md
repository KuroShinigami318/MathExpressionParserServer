# MathExpressionParserServer

A cross-platform C++20 TCP server (built with [CMake](https://cmake.org/) and [Asio](https://think-async.com/Asio/)) that parses and evaluates math expressions sent by clients.

## Prerequisites

- **CMake** 3.28.3 or newer
- **Python** 3.10 or newer, with the `requests` package:
  ```
  pip install requests
  ```
- A **C++20** compiler:
  - Windows: Visual Studio 2022 (MSVC)
  - Linux / macOS: GCC 11+ or Clang 14+

## Setup

The helper scripts in [scripts/](scripts) drive the whole setup. You normally only need to run the **build** script — it downloads the dependencies and generates the CMake project automatically on first run. The steps below are ordered; step 1 is one-time only.

### 1. Download the build scripts (only if `scripts/` is missing)

On a fresh clone the build scripts may not be present. Fetch them with `bootstrap.py`:

```
py bootstrap.py            # Windows
python3 bootstrap.py       # Linux / macOS
```

By default this downloads the `CMakeBuildScripts` repo into `scripts/`. You can override the owner and destination folder: `py bootstrap.py <owner> <folder>`.

### 2. Build

Run the build script from the `scripts/` folder. On the first run it automatically:
1. runs `repo_init` — downloads the dependencies into `libs/`, then
2. runs `gen_prj` — generates the CMake build system into `build/`, then
3. compiles the project.

**Windows:**
```
build.bat Debug
build.bat Release
```

**Linux / macOS (cross-platform Python):**
```
python3 build.py Debug
python3 build.py Release
```

If no configuration is given, `Release` is used. The compiled executable is written to `Debug/` or `Release/` at the repository root.

> Note: you don't need to call `repo_init` and `gen_prj` manually — the build script invokes them for you. Run them individually only if you want to refresh dependencies or regenerate the project.

### Building in Visual Studio (optional)

On Windows you can open the folder in Visual Studio 2022 (which has built-in CMake support) or open the solution generated under `build/`, then build from the IDE.

## Running the server

After building, run the executable from `Debug/` or `Release/`:

```
./Debug/MathExpressionParserServer --protocol tcp --port 8081
```

Options:

| Option | Alias | Description | Default |
| --- | --- | --- | --- |
| `--protocol` | `-pro` | Transport protocol (`tcp`) | `tcp` |
| `--port` | `-P` | Listening port | `8081` |
| `--help` | `-h` | Show usage and exit | – |

## Stress testing

[tests/stress_test.py](tests/stress_test.py) is a standalone async load generator (Python 3.10+, no extra dependencies). It opens many concurrent TCP connections, sends math expressions, and reports throughput, latency percentiles, and error counts.

Start the server, then in another terminal:

```
# 100 connections for 10 seconds against a random workload
python3 tests/stress_test.py --host 127.0.0.1 --port 8081 -c 100 -d 10

# fixed total of 50k requests, verifying every result
python3 tests/stress_test.py -c 200 -n 50000 --validate
```

Run `python3 tests/stress_test.py --help` for all options (workload shape, timeouts, fixed expression, etc.). Note: integer division results are not validated because C++ and Python truncate negative quotients differently.

### Large-input (1 GB) test

[tests/large_expression_test.py](tests/large_expression_test.py) verifies the requirement that a single expression up to 1 GB is processed within a few minutes. It streams one large expression (generated in chunks, so the client stays memory-light), enforces a time budget, and validates the result.

```
# Full 1 GiB run, must finish within 5 minutes (default budget)
python3 tests/large_expression_test.py --size 1GiB --max-seconds 300

# Quicker 100 MB check that also exercises * - and () priorities
python3 tests/large_expression_test.py --size 100MB --pattern groups
```

The workloads use closed-form expected values that never overflow the server's 64-bit accumulator (`ones`: `1+1+...+1`; `groups`: `(2*3-1)+...` = 5 each). The test also sends a small warm-up expression first to confirm the socket stays open for subsequent requests. Exit codes: `0` ok, `1` wrong/error result, `2` timeout or over budget, `3` connection error.
