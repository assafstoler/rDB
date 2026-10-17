#!/usr/bin/env python3
"""Run native CTest suites with isolated GCC counters and gcovr reports."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import datetime
import fcntl
import hashlib
import html
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


def report_options(directory, detailed=True):
    options = ["--json", str(directory / "coverage.json"),
               "--json-summary", str(directory / "summary.json")]
    if detailed:
        options += ["--html-details", str(directory / "index.html"),
                    "--cobertura", str(directory / "coverage.xml"),
                    "--txt", str(directory / "summary.txt"), "--print-summary"]
    return options


def partition_reports(config, coverage, sources):
    """Require one owner for every measured/unbuilt source, with no double counting."""
    reports = config["reports"]
    if not reports or not isinstance(reports, list):
        raise ValueError("reports must be a nonempty list")
    patterns, groups = [], {}
    for report in reports:
        name = report["id"]
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_-]+", name) or name in groups:
            raise ValueError("Invalid or duplicate coverage report id: " + str(name))
        patterns.append((name, re.compile(report["filter"])))
        groups[name] = {"spec": report, "trace": {**coverage, "files": []},
                        "unmeasured_sources": []}
    measured = {entry["file"]: entry for entry in coverage["files"]}
    for path in sorted(set(measured) | set(sources)):
        owners = [name for name, pattern in patterns if pattern.search(path)]
        if len(owners) != 1:
            raise ValueError("Expected exactly one coverage report for " + path +
                             "; matched: " + (", ".join(owners) or "none"))
        group = groups[owners[0]]
        if path in measured:
            group["trace"]["files"].append(measured[path])
        else:
            group["unmeasured_sources"].append(path)
    return groups


def render_reports(args, config, coverage, sources, run, result, env, tests_ok):
    """Collect gcov once, then render disjoint JSON traces concurrently.

    Running two gcov scans over the same objects can race on generated files.
    Each renderer instead reads its own trace and writes its own directory.
    """
    groups = partition_reports(config, coverage, sources)
    workers = min(args.jobs, len(groups))
    jobs_per_report = max(1, args.jobs // workers)
    result["report_concurrency"] = workers
    print("Rendering " + ", ".join(groups) + " coverage reports with " +
          str(workers) + " concurrent workers", flush=True)

    def render(item):
        name, group = item
        directory = run / name
        directory.mkdir()
        spec = group["spec"]
        data = {**result, "report": str(directory), "report_id": name,
                "title": spec.get("title", name), "status": "failed",
                "scope": {**config, "filter": spec["filter"]},
                "unmeasured_sources": group["unmeasured_sources"]}
        data.pop("coverage", None)
        data["scope"].pop("reports", None)
        try:
            if not group["trace"]["files"]:
                raise RuntimeError("No production-code coverage was measured in report " + name)
            trace = directory / "input.json"
            write_json(trace, group["trace"])
            data["gcovr_exit"] = logged(
                [args.gcovr, "--root", str(args.source.resolve()),
                 "--add-tracefile", str(trace), "--no-markers",
                 "--html-title", config["project"] + " - " + data["title"],
                 "-j", str(jobs_per_report)] + report_options(directory),
                directory / "gcovr.log", args.source.resolve(), env)
            if data["gcovr_exit"]:
                raise RuntimeError("gcovr failed; see " + str(directory / "gcovr.log"))
            summary = json.loads((directory / "summary.json").read_text())
            if not summary["line_total"]:
                raise RuntimeError("Empty coverage report: " + name)
            data["coverage"] = {k: v for k, v in summary.items() if k != "files"}
            data["status"] = "passed" if tests_ok else "failed"
        except (OSError, RuntimeError, ValueError, KeyError, subprocess.SubprocessError) as error:
            data["error"] = str(error)
        write_json(directory / "run.json", data)
        return name, data

    with ThreadPoolExecutor(max_workers=workers) as executor:
        return dict(executor.map(render, groups.items()))


def report_index(run, result):
    """A small landing page; each report keeps its own coverage denominator."""
    escape = html.escape
    rows = []
    for name, report in result["reports"].items():
        coverage = report.get("coverage", {})
        measured = "coverage" in report
        link = name + ("/index.html" if measured else "/run.json")
        cells = "".join("<td>" + escape(str(coverage.get(metric + "_percent", "unavailable"))) +
                        ("%" if measured else "") + "</td>"
                        for metric in ("line", "function", "branch"))
        rows.append('<tr><td><a href="' + escape(link, quote=True) + '">' +
                    escape(report["title"]) + "</a></td>" + cells + "<td>" +
                    escape(report["status"]) + "</td><td>" +
                    escape(report.get("error", "")) + "</td></tr>")
    tests = result["tests"]
    (run / "index.html").write_text(
        '<!doctype html><html lang="en"><meta charset="utf-8"><title>' +
        escape(result["project"]) + ' coverage</title><style>'
        'body{font:16px sans-serif;max-width:1100px;margin:3em auto;padding:0 1em}'
        'table{border-collapse:collapse;width:100%}th,td{padding:.8em;text-align:left;'
        'border-bottom:1px solid #ccc}a{color:#1764a0}</style><h1>' +
        escape(result["project"]) + ' coverage</h1><p>One shared test run: ' +
        str(tests["passed"]) + '/' + str(tests["total"]) + ' passed; ' +
        str(tests["failed"]) + ' failed; ' + str(tests["skipped"]) + ' skipped.</p>'
        '<p>Each source belongs to exactly one report. Shared code is counted once. '
        'The reports use the same test run; their test counts must not be added together.</p>'
        '<table><tr><th>Report</th><th>Lines</th><th>Functions</th><th>Branches</th>'
        '<th>Status</th><th>Diagnostic</th></tr>' + "".join(rows) + '</table>'
        '<p><a href="junit.xml">Test results</a> | <a href="ctest.log">Test log</a> | '
        '<a href="run.json">Run metadata</a></p></html>\n')


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
        # Exercise this build's instrumented DSOs, even if an older installed
        # copy is present in the caller's library path. Keep dependency paths.
        env = dict(os.environ, GCOV_PREFIX=str(counters), GCOV_PREFIX_STRIP="0",
                   NO_COLOR="1")
        library_dirs = list(dict.fromkeys(args.library_dir))
        if env.get("LD_LIBRARY_PATH"):
            library_dirs.append(env["LD_LIBRARY_PATH"])
        if library_dirs:
            env["LD_LIBRARY_PATH"] = ":".join(library_dirs)
        result.update(compiler=compiler_version, gcov=gcov_version, gcovr="8.6",
                      library_path=env.get("LD_LIBRARY_PATH", ""))
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
             "-j", str(args.jobs), str(objects)] +
            report_options(run, detailed="reports" not in config),
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
        sources = inventory(source, config["filter"])
        result["unmeasured_sources"] = sorted(set(sources) - measured)
        result["counter_files"] = sum(1 for _ in objects.rglob("*.gcda"))
        if not result["counter_files"]:
            raise RuntimeError("Tests produced no counters; coverage result is incomplete")
        tests_ok = test_rc == 0 and result["tests"]["total"] > 0 and not result["tests"]["failed"]
        reports_ok = True
        if "reports" in config:
            result["reports"] = render_reports(args, config, coverage, sources, run, result, env, tests_ok)
            report_index(run, result)
            reports_ok = all(report["status"] == "passed" for report in result["reports"].values())
            for name, report in result["reports"].items():
                if "error" in report:
                    print("coverage: " + name + ": " + report["error"], file=sys.stderr)
        exit_code = 0 if tests_ok and reports_ok else 1
        result["status"] = "passed" if exit_code == 0 else "failed"
    except (OSError, RuntimeError, ValueError, KeyError, IndexError, re.error, ET.ParseError, subprocess.SubprocessError) as error:
        result["error"] = str(error)
        print("coverage: " + str(error), file=sys.stderr)
    finally:
        result["wall_seconds"] = time.monotonic() - started
        result["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        write_json(run / "run.json", result)
        publish_report(build, run)
    print("Coverage report: " + str(build / "coverage-report"), flush=True)
    if "coverage" in result:
        for report in result.get("reports", {"all": result}).values():
            if "coverage" in report:
                print("{project}: lines {line_percent}% ({line_covered}/{line_total}), "
                      "functions {function_percent}%, branches {branch_percent}%".format(
                          project=report.get("title", config["project"]), **report["coverage"]), flush=True)
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
    parser.add_argument("--library-dir", action="append", default=[],
                        help="Prefer an instrumented shared-library directory when testing")
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
