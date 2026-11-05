#!/usr/bin/env python3
"""Run AMC's Google Benchmark baselines and summarise their JSON output.

The AMC benchmark targets live in `bench/` and are built as separate
Google Benchmark executables. This runner keeps the process model explicit:

  * every executable is discovered with `--benchmark_list_tests`;
  * each executable gets its own process and JSON file;
  * optional benchmark-name filters are forwarded to Google Benchmark;
  * results are normalised to nanoseconds and printed as a Markdown table.

Usage:
    python tools/run_bench.py                          # all AMC benchmarks
    python tools/run_bench.py --list                   # discover names
    python tools/run_bench.py FullPipeline             # regex filter
    python tools/run_bench.py --exe amc-cache-perf-bench --repetitions 3
    python tools/run_bench.py --bin build/bin --out bench/results
    python tools/run_bench.py --pin off                # do not pin CPUs
    python tools/run_bench.py --min-time 0.2s --repetitions 5

Aue's `abix-bench` is intentionally not in this list: it is a standalone Lua
micro-benchmark with its own text output, not a Google Benchmark program.

Pinning is cross-platform where the OS supports it: Linux uses cpufreq +
sched_setaffinity, Windows uses EfficiencyClass + SetProcessAffinityMask, and
macOS currently runs unpinned.
"""

from __future__ import annotations

import argparse
import ctypes
import functools
import json
import os
import struct
import subprocess
import sys
from pathlib import Path

IS_WINDOWS = sys.platform == "win32"
# Unix exposes sched_setaffinity; Windows uses SetProcessAffinityMask via ctypes.
HAS_AFFINITY = hasattr(os, "sched_setaffinity") or IS_WINDOWS

DEFAULT_BENCHES = [
    "amc-pass-perf-bench",
    "amc-artifact-perf-bench",
    "amc-cache-perf-bench",
    "amc-query-perf-bench",
]

TIME_UNIT_NS = {
    "ns": 1.0,
    "us": 1_000.0,
    "ms": 1_000_000.0,
    "s": 1_000_000_000.0,
}


def exe_name(base: str) -> str:
    return base + ".exe" if IS_WINDOWS else base


def _read_int(path: Path) -> int | None:
    try:
        return int(path.read_text(encoding="ascii").strip())
    except (OSError, ValueError):
        return None


def cpu_scaling_advice() -> str | None:
    """Warn once when the Linux cpufreq governor is not `performance`."""
    try:
        governor = Path("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor").read_text(
            encoding="ascii"
        ).strip()
    except OSError:
        return None
    if governor == "performance":
        return None
    return (
        f"CPU scaling is enabled (cpu0 governor: {governor}) - real-time numbers will be noisy.\n"
        "  fix:  sudo cpupower frequency-set -g performance\n"
        "  or:   echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor"
    )


def performance_cpu_pool() -> list[int]:
    """Linux P-core candidates: CPUs at the highest `cpuinfo_max_freq`."""
    freqs: dict[int, int] = {}
    for path in Path("/sys/devices/system/cpu").glob("cpu[0-9]*/cpufreq/cpuinfo_max_freq"):
        freq = _read_int(path)
        if freq:
            freqs[int(path.parent.parent.name[3:])] = freq
    if not freqs:
        return []
    top = max(freqs.values())
    pool = sorted(cpu for cpu, freq in freqs.items() if freq >= top * 0.95)
    return pool if len(pool) < len(freqs) else []


def online_cpus() -> list[int]:
    """Logical CPUs the kernel lists as online (Linux only)."""
    try:
        text = Path("/sys/devices/system/cpu/online").read_text(encoding="ascii").strip()
    except OSError:
        return []
    try:
        return parse_cpu_list(text)
    except ValueError:
        return []


def _windows_core_classes() -> dict[int, list[int]]:
    """Windows logical CPUs grouped by `EfficiencyClass`.

    P-cores report the higher class, E-cores the lower one. Parsed from
    `GetLogicalProcessorInformationEx(RelationProcessorCore)`.
    """
    if not IS_WINDOWS:
        return {}
    relation_processor_core = 0
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.GetLogicalProcessorInformationEx.argtypes = [
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_uint32),
    ]
    kernel32.GetLogicalProcessorInformationEx.restype = ctypes.c_int
    size = ctypes.c_uint32(0)
    kernel32.GetLogicalProcessorInformationEx(relation_processor_core, None, ctypes.byref(size))
    if size.value == 0:
        return {}
    buffer = ctypes.create_string_buffer(size.value)
    if not kernel32.GetLogicalProcessorInformationEx(relation_processor_core, buffer, ctypes.byref(size)):
        return {}
    raw = bytes(buffer[: size.value])
    classes: dict[int, list[int]] = {}
    offset = 0
    while offset + 8 <= len(raw):
        relationship, record_size = struct.unpack_from("<II", raw, offset)
        if record_size < 8:
            break
        body = raw[offset + 8 : offset + record_size]
        if relationship == relation_processor_core and len(body) >= 24:
            _flags, efficiency = struct.unpack_from("<BB", body, 0)
            group_count = struct.unpack_from("<H", body, 22)[0]
            for group_index in range(group_count):
                base = 24 + group_index * 16
                if base + 10 > len(body):
                    break
                mask, group = struct.unpack_from("<QH", body, base)
                for bit in range(64):
                    if mask & (1 << bit):
                        classes.setdefault(efficiency, []).append(group * 64 + bit)
        offset += record_size
    return classes


def p_core_pool() -> list[int]:
    """Logical CPUs in the highest performance class / frequency pool."""
    if IS_WINDOWS:
        classes = _windows_core_classes()
        if len(classes) < 2:
            return []
        return sorted(classes[max(classes)])
    return performance_cpu_pool()


def e_core_pool() -> list[int]:
    """Logical CPUs below the P-core class (Windows) or outside the P-core pool."""
    if IS_WINDOWS:
        classes = _windows_core_classes()
        if len(classes) < 2:
            return []
        top = max(classes)
        return sorted(cpu for efficiency, cpus in classes.items() if efficiency != top for cpu in cpus)
    p_cores = set(performance_cpu_pool())
    if not p_cores:
        return []
    return [cpu for cpu in online_cpus() if cpu not in p_cores]


def parse_cpu_list(spec: str) -> list[int]:
    """`0-3,8,10-11` -> [0, 1, 2, 3, 8, 10, 11]."""
    cpus: list[int] = []
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        first, sep, last = part.partition("-")
        cpus.extend(range(int(first), int(last) + 1) if sep else [int(first)])
    return cpus


def format_cpu_list(cpus: list[int]) -> str:
    if not cpus:
        return ""
    spans: list[str] = []
    start = prev = cpus[0]
    for cpu in cpus[1:]:
        if cpu == prev + 1:
            prev = cpu
            continue
        spans.append(f"{start}-{prev}" if start != prev else str(start))
        start = prev = cpu
    spans.append(f"{start}-{prev}" if start != prev else str(start))
    return ",".join(spans)


def resolve_pin(pin: str) -> tuple[list[int], str]:
    """Return (allowed_cpus, note). Empty cpus means unpinned."""
    if pin in ("off", "all"):
        return [], ""
    if pin in ("auto", "p"):
        cpus = p_core_pool()
        if not cpus:
            return [], "no P-core pool detected"
    elif pin == "e":
        cpus = e_core_pool()
        if not cpus:
            return [], "no E-core pool detected"
    else:
        cpus = parse_cpu_list(pin)
    if not HAS_AFFINITY:
        return [], "CPU affinity is not supported on this platform"
    return cpus, ""


def apply_windows_affinity(pid: int, cpus: list[int]) -> bool:
    """Restrict a running child to `cpus` through SetProcessAffinityMask."""
    if not IS_WINDOWS:
        return False
    mask = 0
    for cpu in cpus:
        mask |= 1 << cpu
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.restype = ctypes.c_void_p
    kernel32.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
    kernel32.SetProcessAffinityMask.restype = ctypes.c_int
    kernel32.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    # PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION
    handle = kernel32.OpenProcess(0x0200 | 0x1000, False, pid)
    if not handle:
        return False
    try:
        return bool(kernel32.SetProcessAffinityMask(handle, ctypes.c_size_t(mask)))
    finally:
        kernel32.CloseHandle(handle)


def pin_spec(value: str) -> str:
    if value in ("auto", "off", "all", "p", "e"):
        return value
    try:
        cpus = parse_cpu_list(value)
    except ValueError:
        cpus = []
    if not cpus:
        raise argparse.ArgumentTypeError(
            f"expected `auto`, `off`, `all`, `p`, `e` or a CPU list like `0-7`, got {value!r}"
        )
    return value


def strip_child_noise(text: str) -> str:
    noise = ("***WARNING*** CPU scaling", "***WARNING*** Failed to set thread affinity")
    return "\n".join(line for line in text.splitlines() if not line.startswith(noise)).strip()


def discover_benchmarks(exe: Path) -> list[str]:
    result = subprocess.run(
        [str(exe), "--benchmark_list_tests=true"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        cwd=exe.parent,
    )
    if result.returncode != 0:
        raise SystemExit(
            f"`{exe} --benchmark_list_tests` failed (exit {result.returncode})\n"
            f"{result.stderr[-800:]}"
        )
    names: list[str] = []
    for line in result.stdout.splitlines():
        stripped = line.strip()
        # Google Benchmark indents parameterised rows and may print a banner.
        if stripped and stripped == line and " " not in stripped:
            names.append(stripped)
    return names


def run_benchmark(
    exe: Path,
    out: Path,
    filters: list[str],
    repetitions: int,
    min_time: str,
    allowed_cpus: list[int],
) -> bool:
    cmd = [str(exe)]
    if filters:
        cmd.append("--benchmark_filter=" + "|".join(filters))
    cmd += [
        f"--benchmark_repetitions={max(repetitions, 1)}",
        "--benchmark_format=json",
        "--benchmark_out_format=json",
        f"--benchmark_out={out}",
        "--benchmark_color=false",
    ]
    if min_time:
        cmd.append(f"--benchmark_min_time={min_time}")

    preexec = (
        functools.partial(os.sched_setaffinity, 0, set(allowed_cpus))
        if allowed_cpus and not IS_WINDOWS
        else None
    )
    popen_kwargs = {}
    if preexec is not None:
        popen_kwargs["preexec_fn"] = preexec
    process = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        cwd=exe.parent,
        **popen_kwargs,
    )
    if allowed_cpus and IS_WINDOWS and not apply_windows_affinity(process.pid, allowed_cpus):
        print(f"  [warn] could not set affinity for {exe.name}", file=sys.stderr)
    _, stderr = process.communicate()
    if process.returncode != 0:
        print(f"  [FAIL] {exe.name}: exit {process.returncode}", file=sys.stderr)
        excerpt = strip_child_noise(stderr[-800:])
        if excerpt:
            print(excerpt, file=sys.stderr)
        return False
    return True


def to_ns(value: float, unit: str) -> float:
    return value * TIME_UNIT_NS.get(unit, 1.0)


def summarize(path: Path) -> list[tuple[str, int, float, float]]:
    """(benchmark, threads, mean ns, stddev ns) from Google Benchmark JSON."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return []

    mean: dict[str, float] = {}
    sd: dict[str, float] = {}
    threads: dict[str, int] = {}
    iterations: dict[str, list[float]] = {}

    for row in data.get("benchmarks", []):
        run_type = row.get("run_type")
        name = row.get("name", "")
        unit = row.get("time_unit", "ns")
        if run_type == "aggregate":
            agg = row.get("aggregate_name")
            base = name[: -len("_" + agg)] if agg and name.endswith("_" + agg) else name
            threads[base] = row.get("threads", 1)
            if agg == "mean":
                mean[base] = to_ns(row["cpu_time"], unit)
            elif agg == "stddev":
                sd[base] = to_ns(row["cpu_time"], unit)
        elif run_type == "iteration":
            iterations.setdefault(name, []).append(to_ns(row["cpu_time"], unit))
            threads.setdefault(name, row.get("threads", 1))

    if not mean:
        for name, values in iterations.items():
            mean[name] = sum(values) / len(values)
            sd[name] = 0.0

    return [
        (name, threads.get(name, 1), mean[name], sd.get(name, 0.0))
        for name in sorted(mean)
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description="Run AMC Google Benchmark baselines")
    parser.add_argument("filters", nargs="*", help="benchmark name filters (regex, forwarded to Google Benchmark)")
    parser.add_argument("--bin", default="build/Release/bin", help="directory containing the benchmark executables")
    parser.add_argument("--out", default="build/bench", help="directory for per-executable JSON output")
    parser.add_argument("--repetitions", type=int, default=5, help="Google Benchmark repetitions")
    parser.add_argument("--min-time", default="", help="optional bound per benchmark, e.g. 0.2s")
    parser.add_argument(
        "--exe",
        action="append",
        dest="exes",
        help=f"benchmark executable to run (repeatable); default: {' '.join(DEFAULT_BENCHES)}",
    )
    parser.add_argument(
        "--pin",
        type=pin_spec,
        default="auto",
        help="restrict children to a CPU set: `auto`/`p` (P-cores), `e` (E-cores), "
        "`off`/`all`, or a list like `0-7`",
    )
    parser.add_argument("--list", action="store_true", help="list discovered benchmarks and exit")
    args = parser.parse_args()

    bin_dir = Path(args.bin).resolve()
    out_dir = Path(args.out).resolve()
    exes = args.exes or DEFAULT_BENCHES

    missing = [name for name in exes if not (bin_dir / exe_name(name)).is_file()]
    for name in missing:
        print(f"  [skip] {name} not found in {bin_dir}", file=sys.stderr)
    exes = [name for name in exes if name not in missing]
    if not exes:
        print(f"no benchmark executables found in {bin_dir}", file=sys.stderr)
        return 2

    if args.list:
        for name in exes:
            exe = bin_dir / exe_name(name)
            try:
                names = discover_benchmarks(exe)
            except SystemExit as error:
                print(error, file=sys.stderr)
                return 2
            print(f"{name} ({len(names)} benchmarks):")
            for benchmark_name in names:
                print(f"  {benchmark_name}")
        return 0

    out_dir.mkdir(parents=True, exist_ok=True)
    allowed_cpus, pin_note = resolve_pin(args.pin)

    print(
        f"running {len(exes)} benchmark executable(s), {max(args.repetitions, 1)} repetitions",
        flush=True,
    )
    scaling = cpu_scaling_advice()
    if scaling:
        print(f"  [warn] {scaling}", file=sys.stderr)
    if allowed_cpus:
        print(f"  [pin] restricting every process to CPUs {format_cpu_list(allowed_cpus)}", file=sys.stderr)
    elif pin_note:
        print(f"  [pin] {pin_note}, running unpinned", file=sys.stderr)

    summary: dict[str, list[tuple[str, int, float, float]]] = {}
    failed: list[str] = []
    for name in exes:
        exe = bin_dir / exe_name(name)
        path = out_dir / f"{name}.json"
        print(f"  {name:28s} -> {path}", flush=True)
        if not run_benchmark(exe, path, args.filters, args.repetitions, args.min_time, allowed_cpus):
            failed.append(name)
        summary[name] = summarize(path)

    print()
    print("| executable | benchmark | threads | mean ns | sd ns |")
    print("|---|---|---:|---:|---:|")
    for name in exes:
        for benchmark_name, threads, mean, sd in summary.get(name, []):
            print(f"| `{name}` | `{benchmark_name}` | {threads} | {mean:,.3f} | {sd:,.3f} |")

    if failed:
        print(f"\n{len(failed)} executable(s) failed: {', '.join(failed)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
