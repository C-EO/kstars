#!/usr/bin/env python3
"""
KStars Test Runner
==================
Discovers all CTest-registered Qt Test binaries, runs them (optionally in
parallel), saves per-test XML logs, and prints a colourised summary showing
which tests passed, which failed, and exactly where failures occurred.

Usage examples
--------------
  # Run every test
  python3 Tests/run_tests.py

  # Run only tests whose name contains "scheduler"
  python3 Tests/run_tests.py -k scheduler

  # Run only tests tagged with the 'stable' CTest label, 4 workers
  python3 Tests/run_tests.py -L stable -j 4

  # Exclude UI tests
  python3 Tests/run_tests.py --exclude-label ui

  # Specify a custom build dir and log output directory
  python3 Tests/run_tests.py --build-dir /tmp/kstars-build --log-dir /tmp/logs

  # Stream output live and also keep XML logs
  python3 Tests/run_tests.py -k focus --verbose --xml
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple


# ---------------------------------------------------------------------------
# ANSI colour helpers (auto-disabled when stdout is not a tty)
# ---------------------------------------------------------------------------

def _supports_color() -> bool:
    return hasattr(sys.stdout, "isatty") and sys.stdout.isatty()


_USE_COLOR = _supports_color()


def _c(code: str, text: str) -> str:
    if not _USE_COLOR:
        return text
    return f"\033[{code}m{text}\033[0m"


def green(t: str) -> str:  return _c("32", t)
def red(t: str) -> str:    return _c("31", t)
def yellow(t: str) -> str: return _c("33", t)
def cyan(t: str) -> str:   return _c("36", t)
def bold(t: str) -> str:   return _c("1",  t)
def dim(t: str) -> str:    return _c("2",  t)


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------

@dataclass
class Failure:
    function: str
    data_tag: str
    file: str
    line: str
    message: str


@dataclass
class TestResult:
    name: str
    command: List[str]
    labels: List[str]
    status: str = "pending"   # passed | failed | crashed | timeout | skipped
    elapsed: float = 0.0
    log_file: Optional[Path] = None
    failures: List[Failure] = field(default_factory=list)
    pass_count: int = 0
    fail_count: int = 0
    skip_count: int = 0
    returncode: int = 0


# ---------------------------------------------------------------------------
# CTest discovery
# ---------------------------------------------------------------------------

def discover_tests(build_tests_dir: Path) -> List[Dict]:
    """Return raw list of test dicts from `ctest --show-only=json-v1`."""
    result = subprocess.run(
        ["ctest", "--show-only=json-v1"],
        cwd=build_tests_dir,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0 and not result.stdout.strip():
        print(red(f"ERROR: ctest discovery failed in {build_tests_dir}"))
        print(result.stderr)
        sys.exit(1)
    try:
        data = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        print(red(f"ERROR: Failed to parse ctest JSON output: {exc}"))
        sys.exit(1)
    return data.get("tests", [])


def filter_tests(
    raw_tests: List[Dict],
    name_filter: Optional[str],
    include_labels: List[str],
    exclude_labels: List[str],
) -> List[Dict]:
    """Apply name substring and label filters."""
    result = []
    for t in raw_tests:
        name = t.get("name", "")
        labels = []
        # Labels live inside the properties list
        for prop in t.get("properties", []):
            if prop.get("name") == "LABELS":
                labels = prop.get("value", [])
                break

        if name_filter and name_filter.lower() not in name.lower():
            continue
        if include_labels:
            if not any(lbl in labels for lbl in include_labels):
                continue
        if exclude_labels:
            if any(lbl in labels for lbl in exclude_labels):
                continue

        t["_labels"] = labels
        result.append(t)
    return result


# ---------------------------------------------------------------------------
# XML parsing (Qt Test output format)
# ---------------------------------------------------------------------------

def _parse_one_xml_doc(xml_text: str) -> Tuple[int, int, int, List[Failure]]:
    """Parse a single Qt Test XML document chunk."""
    passes = fails = skips = 0
    failures: List[Failure] = []

    try:
        root = ET.fromstring(xml_text)
    except ET.ParseError:
        return passes, fails, skips, failures

    for tf in root.iter("TestFunction"):
        func_name = tf.attrib.get("name", "")

        for incident in tf.iter("Incident"):
            itype = incident.attrib.get("type", "")
            data_tag = ""
            for dt in incident.iter("DataTag"):
                data_tag = dt.text or ""

            if itype == "pass":
                passes += 1
            elif itype == "skip":
                skips += 1
            elif itype in ("fail", "xfail", "bfail"):
                fails += 1
                file_attr = incident.attrib.get("file", "")
                line_attr = incident.attrib.get("line", "")
                msg = ""
                for desc in incident.iter("Description"):
                    msg = (desc.text or "").strip()
                failures.append(Failure(
                    function=func_name,
                    data_tag=data_tag,
                    file=file_attr,
                    line=line_attr,
                    message=msg[:200],
                ))

    return passes, fails, skips, failures


def parse_qttest_xml(xml_path: Path) -> Tuple[int, int, int, List[Failure]]:
    """
    Parse a Qt Test XML file and return (pass_count, fail_count, skip_count, failures).

    Some test binaries run multiple QTest classes back-to-back and emit one
    XML document per class concatenated in the same file.  We split on the
    '<?xml' declaration boundary and parse each chunk independently.
    """
    passes = fails = skips = 0
    all_failures: List[Failure] = []

    try:
        text = xml_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return passes, fails, skips, all_failures

    if not text.strip():
        return passes, fails, skips, all_failures

    # Split into individual XML documents on every '<?xml' occurrence
    import re
    chunks = re.split(r'(?=<\?xml\b)', text)
    for chunk in chunks:
        chunk = chunk.strip()
        if not chunk:
            continue
        p, f, s, failures = _parse_one_xml_doc(chunk)
        passes += p
        fails  += f
        skips  += s
        all_failures.extend(failures)

    return passes, fails, skips, all_failures


# ---------------------------------------------------------------------------
# Running a single test
# ---------------------------------------------------------------------------

def run_one_test(
    raw: Dict,
    log_dir: Path,
    timeout: int,
    verbose: bool,
    save_xml: bool,
) -> TestResult:
    name = raw["name"]
    command: List[str] = raw.get("command", [])
    labels: List[str] = raw.get("_labels", [])

    if not command:
        result = TestResult(name=name, command=[], labels=labels, status="skipped")
        return result

    xml_log = log_dir / f"{name}.xml"
    plain_log = log_dir / f"{name}.log"

    # Run the binary with Qt Test XML output flag.
    # Qt Test writes the XML result to stdout; KDE/Qt startup diagnostics go to stderr.
    # We capture them separately so the XML file stays clean.
    cmd = command + ["-v2", "-xml"]

    t_start = time.monotonic()
    try:
        proc = subprocess.run(
            cmd,
            capture_output=not verbose,
            text=True,
            timeout=timeout,
            cwd=log_dir,   # safe working dir for any temp files the test creates
        )
        elapsed = time.monotonic() - t_start
        returncode = proc.returncode

        stdout_data = proc.stdout or "" if not verbose else ""
        stderr_data = proc.stderr or "" if not verbose else ""

    except subprocess.TimeoutExpired:
        elapsed = timeout
        returncode = -999
        stdout_data = ""
        stderr_data = f"[TIMEOUT after {timeout}s]"
    except FileNotFoundError:
        elapsed = 0.0
        returncode = -1
        stdout_data = ""
        stderr_data = f"[BINARY NOT FOUND: {command[0]}]"

    # stdout contains the Qt Test XML output; write it directly.
    # If for some reason XML appears mixed with preamble text, strip to first <?xml.
    xml_content = stdout_data
    if xml_content and not xml_content.lstrip().startswith("<?xml"):
        xml_start = xml_content.find("<?xml")
        xml_content = xml_content[xml_start:] if xml_start != -1 else ""

    xml_log.write_text(xml_content, encoding="utf-8", errors="replace")
    plain_log.write_text(stderr_data, encoding="utf-8", errors="replace")

    # Parse the XML for detailed results
    passes, fails, skips, failure_details = parse_qttest_xml(xml_log)

    # Determine overall status
    if returncode == -999:
        status = "timeout"
    elif returncode == -1:
        status = "crashed"
    elif returncode != 0 or fails > 0:
        status = "failed"
    else:
        status = "passed"

    tr = TestResult(
        name=name,
        command=command,
        labels=labels,
        status=status,
        elapsed=elapsed,
        log_file=xml_log,
        failures=failure_details,
        pass_count=passes,
        fail_count=fails,
        skip_count=skips,
        returncode=returncode,
    )
    return tr


# ---------------------------------------------------------------------------
# Summary printing
# ---------------------------------------------------------------------------

STATUS_ICON = {
    "passed":  green("✅ PASS"),
    "failed":  red("❌ FAIL"),
    "crashed": red("🔴 CRASH"),
    "timeout": yellow("⏱  TIME"),
    "skipped": dim("⏭  SKIP"),
    "pending": dim("…  PEND"),
}


def print_summary(results: List[TestResult], log_dir: Path, total_elapsed: float) -> None:
    sep = "─" * 72

    print()
    print(bold(sep))
    print(bold("  KStars Test Suite — Summary"))
    print(bold(sep))

    # Group by status for ordering: failed/crash/timeout first, then passed, skipped last
    ordered = (
        [r for r in results if r.status in ("failed", "crashed", "timeout")] +
        [r for r in results if r.status == "passed"] +
        [r for r in results if r.status == "skipped"]
    )

    for r in ordered:
        icon = STATUS_ICON.get(r.status, "?")
        elapsed_str = f"{r.elapsed:6.1f}s"
        counts = f"{green(str(r.pass_count)+'p')} {red(str(r.fail_count)+'f')} {dim(str(r.skip_count)+'s')}" \
                 if r.pass_count + r.fail_count + r.skip_count > 0 else ""
        label_str = dim(f"[{', '.join(r.labels)}]") if r.labels else ""
        print(f"  {icon}  {bold(r.name):<42} {elapsed_str}  {counts}  {label_str}")

        # Print failure details indented
        for f in r.failures:
            tag = f" [{f.data_tag}]" if f.data_tag else ""
            loc = f"{f.file}:{f.line}" if f.file else "unknown location"
            print(f"         {red('↳')} {cyan(f.function)}{yellow(tag)}")
            print(f"           {dim('at')} {loc}")
            if f.message:
                # Print first line of message
                first_line = f.message.split("\n")[0].strip()
                print(f"           {dim(first_line)}")

        if r.status in ("crashed", "timeout") and r.log_file:
            print(f"         {dim('log:')} {r.log_file}")

    print(bold(sep))

    # Aggregate counts
    total   = len(results)
    passed  = sum(1 for r in results if r.status == "passed")
    failed  = sum(1 for r in results if r.status == "failed")
    crashed = sum(1 for r in results if r.status == "crashed")
    timeout = sum(1 for r in results if r.status == "timeout")
    skipped = sum(1 for r in results if r.status == "skipped")

    parts = [
        green(f"{passed} passed"),
        red(f"{failed} failed") if failed else None,
        red(f"{crashed} crashed") if crashed else None,
        yellow(f"{timeout} timed out") if timeout else None,
        dim(f"{skipped} skipped") if skipped else None,
    ]
    summary_line = "  " + "  |  ".join(p for p in parts if p is not None)
    summary_line += f"  |  {dim(f'{total_elapsed:.1f}s total')}"
    print(summary_line)
    print(bold(sep))
    print(f"  {dim('Logs saved to:')} {log_dir}")
    print(bold(sep))
    print()


# ---------------------------------------------------------------------------
# Plain-text summary (written to summary.txt)
# ---------------------------------------------------------------------------

def write_plain_summary(results: List[TestResult], log_dir: Path, total_elapsed: float) -> None:
    lines: List[str] = []
    lines.append("KStars Test Suite — Summary")
    lines.append("=" * 72)

    for r in results:
        status = r.status.upper()
        lines.append(f"[{status:<7}] {r.name}  ({r.elapsed:.1f}s)  pass={r.pass_count} fail={r.fail_count} skip={r.skip_count}")
        for f in r.failures:
            tag = f" [{f.data_tag}]" if f.data_tag else ""
            loc = f"{f.file}:{f.line}" if f.file else "unknown location"
            lines.append(f"         FAILURE: {f.function}{tag}")
            lines.append(f"                  at {loc}")
            if f.message:
                lines.append(f"                  {f.message.split(chr(10))[0].strip()}")

    lines.append("=" * 72)
    passed  = sum(1 for r in results if r.status == "passed")
    failed  = sum(1 for r in results if r.status in ("failed", "crashed", "timeout"))
    skipped = sum(1 for r in results if r.status == "skipped")
    lines.append(f"Total: {len(results)}  Passed: {passed}  Failed: {failed}  Skipped: {skipped}  Time: {total_elapsed:.1f}s")

    (log_dir / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------
# Build step
# ---------------------------------------------------------------------------

def build_tests(build_dir: Path, jobs: int) -> None:
    print(bold(f"Building tests in {build_dir} …"))
    result = subprocess.run(
        ["cmake", "--build", str(build_dir), "--target", "all", f"-j{jobs}"],
        cwd=build_dir,
    )
    if result.returncode != 0:
        print(red("Build failed — aborting."))
        sys.exit(result.returncode)
    print(green("Build succeeded.\n"))


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="KStars test runner — runs CTest Qt Test binaries, logs output, prints summary.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )

    # Discovery / filtering
    parser.add_argument(
        "-k", "--filter", metavar="PATTERN",
        help="Only run tests whose name contains PATTERN (case-insensitive substring).",
    )
    parser.add_argument(
        "-L", "--label", metavar="LABEL", action="append", default=[],
        help="Only run tests that have this CTest label. May be repeated.",
    )
    parser.add_argument(
        "--exclude-label", metavar="LABEL", action="append", default=[],
        help="Exclude tests that have this CTest label. May be repeated.",
    )

    # Execution
    parser.add_argument(
        "-j", "--jobs", type=int, default=1, metavar="N",
        help="Run N tests in parallel (default: 1).",
    )
    parser.add_argument(
        "--timeout", type=int, default=120, metavar="SECONDS",
        help="Per-test timeout in seconds (default: 120).",
    )

    # Paths
    repo_root = Path(__file__).resolve().parent.parent
    default_build = repo_root / "build"
    parser.add_argument(
        "--build-dir", type=Path, default=default_build, metavar="DIR",
        help=f"CMake build directory (default: {default_build}).",
    )
    parser.add_argument(
        "--log-dir", type=Path, default=None, metavar="DIR",
        help="Directory for log files (default: ./test-logs-<timestamp>/).",
    )

    # Misc
    parser.add_argument(
        "--no-build", action="store_true",
        help="Skip the cmake --build step.",
    )
    parser.add_argument(
        "--xml", action="store_true",
        help="Preserve raw Qt XML output alongside .log files.",
    )
    parser.add_argument(
        "--verbose", action="store_true",
        help="Stream test output live to the terminal (implies --jobs 1).",
    )
    parser.add_argument(
        "--list", action="store_true",
        help="List discovered tests without running them, then exit.",
    )

    return parser.parse_args()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    args = parse_args()

    # Resolve build/Tests dir
    build_dir: Path = args.build_dir.resolve()
    build_tests_dir: Path = build_dir / "Tests"

    if not build_tests_dir.is_dir():
        print(red(f"ERROR: build/Tests directory not found: {build_tests_dir}"))
        print("Make sure you have configured with -DBUILD_TESTING=ON and built the project.")
        sys.exit(1)

    # Optionally build first
    if not args.no_build:
        build_tests(build_dir, args.jobs)

    # Discover tests
    print(bold("Discovering tests …"))
    raw_tests = discover_tests(build_tests_dir)
    filtered  = filter_tests(raw_tests, args.filter, args.label, args.exclude_label)

    if not filtered:
        print(yellow("No tests matched the given filters."))
        sys.exit(0)

    # List mode
    if args.list:
        print(bold(f"\nDiscovered {len(filtered)} test(s):"))
        for t in filtered:
            labels = t.get("_labels", [])
            label_str = f"  [{', '.join(labels)}]" if labels else ""
            print(f"  {t['name']}{label_str}")
        sys.exit(0)

    # Set up log dir
    if args.log_dir is None:
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        args.log_dir = Path.cwd() / f"test-logs-{stamp}"
    log_dir: Path = args.log_dir
    log_dir.mkdir(parents=True, exist_ok=True)

    # Verbose forces single-threaded (output would interleave otherwise)
    jobs = 1 if args.verbose else args.jobs

    print(bold(f"Running {len(filtered)} test(s)  [jobs={jobs}  timeout={args.timeout}s]"))
    print(dim(f"Logs → {log_dir}"))
    print()

    results: List[TestResult] = []
    global_start = time.monotonic()

    def _run(raw: Dict) -> TestResult:
        r = run_one_test(raw, log_dir, args.timeout, args.verbose, args.xml)
        icon = STATUS_ICON.get(r.status, "?")
        # Brief live status line
        if not args.verbose:
            elapsed_str = f"{r.elapsed:.1f}s"
            print(f"  {icon}  {r.name:<45} {elapsed_str}")
        return r

    if jobs == 1:
        for raw in filtered:
            results.append(_run(raw))
    else:
        futures = {}
        with ThreadPoolExecutor(max_workers=jobs) as ex:
            for raw in filtered:
                futures[ex.submit(_run, raw)] = raw["name"]
            for fut in as_completed(futures):
                results.append(fut.result())

    total_elapsed = time.monotonic() - global_start

    # Sort results back into discovery order for the summary
    order = {t["name"]: i for i, t in enumerate(filtered)}
    results.sort(key=lambda r: order.get(r.name, 9999))

    # Print and save summary
    print_summary(results, log_dir, total_elapsed)
    write_plain_summary(results, log_dir, total_elapsed)

    # Exit with non-zero if any test didn't pass
    any_bad = any(r.status not in ("passed", "skipped") for r in results)
    sys.exit(1 if any_bad else 0)


if __name__ == "__main__":
    main()
