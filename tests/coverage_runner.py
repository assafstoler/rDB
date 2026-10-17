#!/usr/bin/env python3
"""Run native CTest suites with isolated GCC counters and gcovr reports."""
import argparse
import datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET


def capture(argv, cwd=None):
    return subprocess.check_output(argv, cwd=cwd, text=True).strip()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def prepare_notes(build, counters):
    """Copy notes, never old counters; GCC mirrors absolute paths with strip=0."""
    notes = [p for p in build.rglob("*.gcno")
             if p.relative_to(build).parts[0] not in ("coverage-runs", "coverage-report")]
    if not notes:
        raise RuntimeError("No .gcno files: build all targets with TEAL_ENABLE_COVERAGE=ON first")
    for note in notes:
        destination = counters / note.relative_to(note.anchor)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(note, destination)
    return counters / build.relative_to(build.anchor)


def validate_compile_flags(build, optimization):
    commands = json.loads((build / "compile_commands.json").read_text())
    if not commands:
        raise RuntimeError("No compilation commands to validate")
    for item in commands:
        flags = item.get("arguments") or shlex.split(item["command"])
        levels = [f for f in flags if re.fullmatch(r"-O(?:[0-3gsz]|fast)", f)]
        if "--coverage" not in flags or not levels or levels[-1] != optimization:
            raise RuntimeError("Missing coverage flags or overridden optimization: " + item["file"])


def junit_counts(path):
    root = ET.parse(path).getroot()
    cases = list(root.iter("testcase"))
    failed = sum(c.find("failure") is not None or c.find("error") is not None for c in cases)
    skipped = sum(c.find("skipped") is not None for c in cases)
    return {"total": len(cases), "passed": len(cases) - failed - skipped,
            "failed": failed, "skipped": skipped,
            "seconds": sum(float(c.get("time", 0)) for c in cases)}


def git_metadata(source):
    try:
        diff = subprocess.check_output(["git", "diff", "HEAD"], cwd=source, stderr=subprocess.DEVNULL)
        return {"commit": capture(["git", "rev-parse", "HEAD"], source),
                "branch": capture(["git", "branch", "--show-current"], source),
                "tracked_changes": bool(diff), "diff_sha256": hashlib.sha256(diff).hexdigest()}
    except (OSError, subprocess.SubprocessError):
        return {"commit": None, "branch": None, "tracked_changes": None}


def inventory(source, pattern):
    # Include new untracked source files, but respect Git's build/vendor ignores.
    try:
        paths = subprocess.check_output(
            ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
            cwd=source, text=True, stderr=subprocess.DEVNULL).split("\0")
    except (OSError, subprocess.SubprocessError):
        paths = [str(p.relative_to(source)) for p in source.rglob("*") if p.is_file()]
    return sorted({p for p in paths if re.search(pattern, p)
                   and Path(p).suffix in (".c", ".cc", ".cpp", ".cxx")})


def publish_report(build, run):
    """Atomically switch only our symlink; never replace an operator directory."""
    link = build / "coverage-report"
    if link.exists() and not link.is_symlink():
        raise RuntimeError("Refusing to replace non-symlink " + str(link))
    temporary = run / "report-link"
    temporary.symlink_to(run.relative_to(build), target_is_directory=True)
    os.replace(temporary, link)


def logged(argv, path, cwd, env):
    with path.open("w") as log:
        log.write("$ " + shlex.join(argv) + "\n")
        log.flush()
        return subprocess.run(argv, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT).returncode


def run_coverage(args):
    source, build = args.source.resolve(), args.build.resolve()
    runs = build / "coverage-runs"
    runs.mkdir(exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="run-", dir=runs))
    counters = run / "counters"
    started = time.monotonic()
    result = {"schema": 1, "project": source.name, "source": str(source),
              "build": str(build), "report": str(run),
              **git_metadata(source),
              "platform": platform.platform(), "machine": platform.machine(),
              "optimization": args.optimization,
              "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "status": "failed"}
    exit_code = 1
    try:
        config = json.loads(args.config.read_text())
        result.update(project=config["project"], scope=config)
        validate_compile_flags(build, args.optimization)
        result["compile_commands_sha256"] = hashlib.sha256((build / "compile_commands.json").read_bytes()).hexdigest()
        compiler_version = capture([args.compiler, "-dumpfullversion", "-dumpversion"])
        gcov_version = capture([args.gcov, "--version"]).splitlines()[0]
        result.update(compiler=compiler_version, gcov=gcov_version, gcovr="8.6",
                      library_path=os.environ.get("LD_LIBRARY_PATH", ""))
        match = re.search(r"(\d+)\.\d+", gcov_version)
        if not match or match.group(1) != compiler_version.split(".")[0]:
            raise RuntimeError("gcov/compiler version mismatch: " + gcov_version + " / " + compiler_version)
        if capture([args.gcovr, "--version"]).splitlines()[0] != "gcovr 8.6":
            raise RuntimeError("Use the pinned gcovr==8.6 from tests/coverage-requirements.txt")
        result["configuration"] = {}
        for line in (build / "CMakeCache.txt").read_text().splitlines():
            if re.match(r"^(CMAKE_BUILD_TYPE|X86_BUILD|VIDEO_CORE_[A-Z_]+|TEAL_COVERAGE_[A-Z_]+):", line):
                name, value = line.split("=", 1)
                result["configuration"][name.split(":")[0]] = value
        objects = prepare_notes(build, counters)
        env = dict(os.environ, GCOV_PREFIX=str(counters), GCOV_PREFIX_STRIP="0",
                   NO_COLOR="1")
        print("Running " + config["project"] + " tests; counters: " + str(counters), flush=True)
        test_started = time.monotonic()
        test_rc = logged([args.ctest, "--output-on-failure", "--parallel", str(args.jobs),
                          "--output-junit", str(run / "junit.xml")],
                         run / "ctest.log", build, env)
        result["test_wall_seconds"] = time.monotonic() - test_started
        result["ctest_exit"] = test_rc
        result["tests"] = junit_counts(run / "junit.xml")
        # Always attempt coverage, even when assertions/timeouts failed.
        report_rc = logged(
            [args.gcovr, "--root", str(source), "--filter", config["filter"],
             "--gcov-executable", args.gcov, "--no-markers",
             "--html-details", str(run / "index.html"),
             "--cobertura", str(run / "coverage.xml"),
             "--json", str(run / "coverage.json"),
             "--json-summary", str(run / "summary.json"),
             "--txt", str(run / "summary.txt"), "--print-summary",
             "-j", str(args.jobs), str(objects)],
            run / "gcovr.log", source, env)
        result["gcovr_exit"] = report_rc
        if report_rc:
            raise RuntimeError("gcovr failed; see " + str(run / "gcovr.log"))
        summary = json.loads((run / "summary.json").read_text())
        coverage = json.loads((run / "coverage.json").read_text())
        if not coverage["files"] or not summary["line_total"]:
            raise RuntimeError("No production-code coverage was measured; refusing an empty report")
        measured = {f["file"] for f in coverage["files"]}
        result["coverage"] = {k: v for k, v in summary.items() if k != "files"}
        result["unmeasured_sources"] = sorted(set(inventory(source, config["filter"])) - measured)
        result["counter_files"] = sum(1 for _ in objects.rglob("*.gcda"))
        if not result["counter_files"]:
            raise RuntimeError("Tests produced no counters; coverage result is incomplete")
        exit_code = 0 if test_rc == 0 and result["tests"]["total"] > 0 and not result["tests"]["failed"] else 1
        result["status"] = "passed" if exit_code == 0 else "failed"
    except (OSError, RuntimeError, ValueError, KeyError, IndexError, ET.ParseError, subprocess.SubprocessError) as error:
        result["error"] = str(error)
        print("coverage: " + str(error), file=sys.stderr)
    finally:
        result["wall_seconds"] = time.monotonic() - started
        result["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        write_json(run / "run.json", result)
        publish_report(build, run)
    print("Coverage report: " + str(build / "coverage-report"), flush=True)
    if "coverage" in result:
        c = result["coverage"]
        print("{project}: lines {line_percent}% ({line_covered}/{line_total}), "
              "functions {function_percent}%, branches {branch_percent}%".format(
                  project=config["project"], **c), flush=True)
    return exit_code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--gcovr", default="gcovr")
    parser.add_argument("--gcov", default="gcov")
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--optimization", default="-O0")
    parser.add_argument("--jobs", type=int, default=1)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        # Serialize runs sharing a build directory; independent builds remain parallel.
        with (args.build / "coverage.lock").open("w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return run_coverage(args)
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print("coverage: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
