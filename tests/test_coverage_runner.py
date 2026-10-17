#!/usr/bin/env python3
"""Regression checks for the shared coverage runner; no camera/target access."""
import contextlib
import fcntl
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock

import coverage_runner as runner
import coverage_summary


ROOT = Path(__file__).resolve().parents[1]
GCOVR = os.environ.get("TEAL_TEST_GCOVR") or shutil.which("gcovr")


class CoverageRunnerTests(unittest.TestCase):
    def test_notes_are_copied_without_stale_counters(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "build"
            obj = build / "objects"
            obj.mkdir(parents=True)
            (obj / "one.gcno").write_bytes(b"notes")
            (obj / "one.gcda").write_bytes(b"stale")
            old = build / "coverage-runs" / "old"
            old.mkdir(parents=True)
            (old / "excluded.gcno").write_bytes(b"old notes")
            copied = runner.prepare_notes(build, root / "fresh")
            self.assertEqual((copied / "objects/one.gcno").read_bytes(), b"notes")
            self.assertEqual(list(copied.rglob("*.gcda")), [])
            self.assertEqual(len(list(copied.rglob("*.gcno"))), 1)
            self.assertEqual((obj / "one.gcda").read_bytes(), b"stale")

    def test_missing_instrumentation_is_not_success(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(RuntimeError, "No .gcno"):
                runner.prepare_notes(root, root / "fresh")

    def test_final_optimization_is_checked(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            commands = [{"file": "module.cpp", "arguments": ["c++", "--coverage", "-O0", "-O2"]}]
            (build / "compile_commands.json").write_text(json.dumps(commands))
            with self.assertRaisesRegex(RuntimeError, "overridden optimization"):
                runner.validate_compile_flags(build, "-O0")
            commands[0]["arguments"] += ["-O0"]
            (build / "compile_commands.json").write_text(json.dumps(commands))
            runner.validate_compile_flags(build, "-O0")

    def test_junit_accounting(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "results.xml"
            path.write_text('<testsuite><testcase time="1.0"/><testcase time="2">'
                            '<failure/></testcase><testcase><skipped/></testcase></testsuite>')
            self.assertEqual(runner.junit_counts(path),
                             {"total": 3, "passed": 1, "failed": 1, "skipped": 1, "seconds": 3.0})

    def test_publish_preserves_operator_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            run = build / "coverage-runs/new"
            run.mkdir(parents=True)
            protected = build / "coverage-report"
            protected.mkdir()
            (protected / "keep").write_text("keep")
            with self.assertRaisesRegex(RuntimeError, "Refusing"):
                runner.publish_report(build, run)
            self.assertEqual((protected / "keep").read_text(), "keep")

    def test_new_reports_replace_only_the_link(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            for name in ("old", "new"):
                run = build / "coverage-runs" / name
                run.mkdir(parents=True)
                runner.publish_report(build, run)
                self.assertEqual((build / "coverage-report").resolve(), run)
            self.assertTrue((build / "coverage-runs/old").is_dir())

    def test_dashboard_preserves_project_denominators_and_failures(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reports = []
            for name, state, total in (("rDB", "passed", 10), ("rdbfw", "failed", 90)):
                path = root / (name + ".json")
                path.write_text(json.dumps({"project": name, "report": str(root / name),
                    "status": state, "commit": "abc", "machine": "x86_64",
                    "coverage": {"line_total": total, "line_covered": 1, "line_percent": 100 / total}}))
                reports.append(path)
            self.assertEqual(coverage_summary.summarize(reports, root / "summary"), 1)
            data = json.loads((root / "summary/summary.json").read_text())
            self.assertEqual([d["coverage"]["line_total"] for d in data], [10, 90])
            self.assertIn("rdbfw", (root / "summary/index.html").read_text())

    def simulated_run(self, *, gcov_version="gcov (GCC) 13.3.0",
                      gcovr_version="gcovr 8.6", junit=None, counters=True,
                      config_text=None, sources=None, library_dirs=()):
        """Test runner failure accounting; the real-compiler test stays separate."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, build = root / "source", root / "build"
            source.mkdir()
            build.mkdir()
            config = source / "coverage.json"
            config.write_text(config_text if config_text is not None else
                              json.dumps({"project": "fixture", "filter": "^src/"}))
            (build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Debug\n")
            (build / "one.gcno").write_bytes(b"fixture notes")
            (build / "compile_commands.json").write_text(json.dumps([{
                "file": "src/one.c", "arguments": ["cc", "--coverage", "-O0"]}]))
            args = SimpleNamespace(source=source, build=build, config=config,
                compiler="compiler", gcov="gcov", gcovr="gcovr", ctest="ctest",
                optimization="-O0", jobs=1, library_dir=library_dirs)

            def capture(argv):
                return {"compiler": "13.3.0", "gcov": gcov_version,
                        "gcovr": gcovr_version}[argv[0]]

            def logged(argv, path, cwd, env):
                path.write_text("synthetic runner fixture\n")
                if argv[0] == "ctest":
                    Path(argv[argv.index("--output-junit") + 1]).write_text(
                        junit if junit is not None else "<testsuite><testcase/></testsuite>")
                    if counters:
                        target = Path(env["GCOV_PREFIX"]) / build.relative_to(build.anchor)
                        (target / "one.gcda").write_bytes(b"fixture counters")
                else:
                    Path(argv[argv.index("--json-summary") + 1]).write_text(json.dumps({
                        "line_total": 1, "line_covered": 1, "line_percent": 100,
                        "function_total": 1, "function_covered": 1, "function_percent": 100,
                        "branch_total": 2, "branch_covered": 2, "branch_percent": 100}))
                    Path(argv[argv.index("--json") + 1]).write_text(json.dumps({
                        "files": [{"file": "src/one.c"}]}))
                return 0

            with mock.patch.object(runner, "git_metadata", return_value={}), \
                 mock.patch.object(runner, "capture", side_effect=capture), \
                 mock.patch.object(runner, "logged", side_effect=logged) as commands, \
                 mock.patch.object(runner, "inventory", return_value=sources or ["src/one.c"]), \
                 contextlib.redirect_stdout(io.StringIO()), \
                 contextlib.redirect_stderr(io.StringIO()):
                result = runner.run_coverage(args)
            report = build / "coverage-report"
            self.assertTrue(report.is_symlink())
            return result, json.loads((report / "run.json").read_text()), commands.call_count

    def test_tool_version_mismatches_fail_before_running_tests(self):
        for options, message in (
                ({"gcov_version": "gcov (GCC) 12.2.0"}, "version mismatch"),
                ({"gcovr_version": "gcovr 9.0"}, "pinned gcovr")):
            with self.subTest(options=options):
                rc, data, commands = self.simulated_run(**options)
                self.assertNotEqual(rc, 0)
                self.assertEqual(data["status"], "failed")
                self.assertIn(message, data["error"])
                self.assertEqual(commands, 0)

    def test_empty_suite_and_missing_counters_are_failures(self):
        rc, data, commands = self.simulated_run(junit="<testsuite/>")
        self.assertNotEqual(rc, 0)
        self.assertEqual(data["tests"]["total"], 0)
        self.assertEqual(data["status"], "failed")
        self.assertEqual(commands, 2)
        rc, data, commands = self.simulated_run(counters=False)
        self.assertNotEqual(rc, 0)
        self.assertIn("no counters", data["error"])
        self.assertEqual(data["status"], "failed")

    def test_malformed_junit_and_config_publish_failed_report(self):
        for options in ({"junit": "<broken"}, {"config_text": "{broken"}):
            with self.subTest(options=options):
                rc, data, _ = self.simulated_run(**options)
                self.assertNotEqual(rc, 0)
                self.assertEqual(data["status"], "failed")
                self.assertTrue(data["error"])

    def test_unmeasured_sources_are_reported(self):
        rc, data, _ = self.simulated_run(sources=["src/one.c", "src/unbuilt.c"])
        self.assertEqual(rc, 0)
        self.assertEqual(data["unmeasured_sources"], ["src/unbuilt.c"])

    def test_instrumented_libraries_precede_existing_dependency_paths(self):
        with mock.patch.dict(os.environ, {"LD_LIBRARY_PATH": "/installed:/dependencies"}):
            rc, data, _ = self.simulated_run(library_dirs=["/coverage", "/coverage/control", "/coverage"])
            self.assertEqual(rc, 0)
            self.assertEqual(data["library_path"], "/coverage:/coverage/control:/installed:/dependencies")
            self.assertEqual(os.environ["LD_LIBRARY_PATH"], "/installed:/dependencies")

    def test_junit_errors_and_nested_suites(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "junit.xml"
            path.write_text('<testsuites><testsuite><testcase time="0.5"><error/>'
                            '</testcase></testsuite><testsuite><testcase/>'
                            '<testcase><skipped/></testcase></testsuite></testsuites>')
            self.assertEqual(runner.junit_counts(path), {
                "total": 3, "passed": 1, "failed": 1, "skipped": 1, "seconds": 0.5})

    def test_build_lock_rejects_overlapping_run(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with (root / "coverage.lock").open("w") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                result = subprocess.run(["python3", "-B", str(ROOT / "tests/coverage_runner.py"),
                    "--build", str(root), "--source", str(root), "--config", str(root / "config.json")],
                    text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10)
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("coverage:", result.stdout)
            self.assertFalse((root / "coverage-runs").exists())

    def test_dashboard_escapes_diagnostics_and_rejects_empty_input(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertEqual(coverage_summary.summarize([], root / "empty"), 1)
            report = root / "run.json"
            report.write_text(json.dumps({"project": "<rDB&>", "report": str(root / "a b"),
                "status": "failed", "error": "<diagnostic>"}))
            self.assertEqual(coverage_summary.summarize([report], root / "dashboard"), 1)
            html = (root / "dashboard/index.html").read_text()
            self.assertIn("&lt;rDB&amp;&gt;", html)
            self.assertIn("&lt;diagnostic&gt;", html)
            self.assertIn("a%20b", html)
            self.assertIn("unavailable", html)

    def test_normal_build_survives_missing_tools_and_cross_coverage_refusal(self):
        for cross in (False, True):
            with self.subTest(cross=cross), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                source, build = root / "source", root / "build"
                shutil.copytree(ROOT / "tests/fixtures/coverage", source)
                configure = ["cmake", "-S", str(source), "-B", str(build),
                             "-DTEAL_COVERAGE_GCOVR=/no/such/gcovr", "-DTEAL_COVERAGE_JOBS=2",
                             "-DTEAL_TEST_HELPER=" + str(ROOT / "cmake/TealCoverage.cmake")]
                if cross:
                    configure += ["-DCMAKE_SYSTEM_NAME=Linux"]
                result = subprocess.run(configure, text=True, capture_output=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertFalse((build / "coverage").exists())
                result = subprocess.run(["cmake", "--build", str(build), "--target", "coverage"],
                                        text=True, capture_output=True, timeout=60)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                if cross:
                    self.assertIn("Coverage requires a native build", result.stdout)
                    self.assertFalse((build / "coverage").exists())
                else:
                    report = build / "coverage/coverage-report/run.json"
                    self.assertEqual(json.loads(report.read_text())["status"], "failed")
                commands = json.loads((build / "compile_commands.json").read_text())
                self.assertTrue(all("--coverage" not in c["command"] for c in commands))

    @unittest.skipUnless(GCOVR, "Install tests/coverage-requirements.txt for compiler integration")
    def test_normal_build_coverage_target_is_isolated_and_repeatable(self):
        generators = ["Unix Makefiles"] + (["Ninja"] if shutil.which("ninja") else [])
        for generator in generators:
            with self.subTest(generator=generator), tempfile.TemporaryDirectory(prefix="coverage target ") as directory:
                root = Path(directory)
                source, build, shadow = root / "source", root / "build", root / "old libraries"
                shutil.copytree(ROOT / "tests/fixtures/coverage", source)
                shadow.mkdir()
                env = dict(os.environ, LD_LIBRARY_PATH=str(shadow))
                for key in ("GCOV_PREFIX", "GCOV_PREFIX_STRIP"):
                    env.pop(key, None)

                def call(argv, expected=0):
                    result = subprocess.run(argv, env=env, text=True, stdout=subprocess.PIPE,
                                            stderr=subprocess.STDOUT, timeout=60)
                    if expected == 0:
                        self.assertEqual(result.returncode, 0, result.stdout)
                    else:
                        self.assertNotEqual(result.returncode, 0, result.stdout)
                    return result.stdout

                # A stale library would make the test fail if the runner did not
                # put its instrumented build first, even without shell setup.
                old_source = shadow / "old.c"
                old_source.write_text("int calculate(int value) { return -999; }\n")
                call(["cc", "-shared", "-fPIC", str(old_source), "-o", str(shadow / "libsample.so")])
                flags = "-DFIXTURE_FORWARDED=7"
                prefixes = str(root / "first path") + ";" + str(root / "second path")
                configure = ["cmake", "-S", str(source), "-B", str(build), "-G", generator,
                             "-DCMAKE_C_FLAGS=" + flags, "-DCMAKE_PREFIX_PATH=" + prefixes,
                             "-DTEAL_COVERAGE_GCOVR=" + str(GCOVR), "-DTEAL_COVERAGE_JOBS=2",
                             "-DTEAL_TEST_HELPER=" + str(ROOT / "cmake/TealCoverage.cmake")]
                call(configure)
                call(["cmake", "--build", str(build), "--parallel", "2"])
                original = hashlib.sha256((build / "libsample.so").read_bytes()).hexdigest()
                commands = (build / "compile_commands.json").read_text()
                self.assertNotIn("--coverage", commands)
                command = (["make", "-C", str(build), "coverage"] if generator == "Unix Makefiles" else
                           ["cmake", "--build", str(build), "--target", "coverage"])
                call(command)
                child = build / "coverage"
                report = child / "coverage-report"
                first = report.resolve()
                data = json.loads((report / "run.json").read_text())
                self.assertEqual(data["status"], "passed")
                self.assertEqual(data["tests"]["passed"], 1)
                self.assertEqual(data["library_path"].split(":"), [str(child), str(shadow)])
                cache = (child / "CMakeCache.txt").read_text()
                self.assertIn("CMAKE_C_FLAGS:STRING=" + flags, cache)
                self.assertIn("CMAKE_PREFIX_PATH:STRING=" + prefixes, cache)
                runner.validate_compile_flags(child, "-O0")
                self.assertFalse((child / "coverage").exists())  # No recursive launchers.
                self.assertEqual(hashlib.sha256((build / "libsample.so").read_bytes()).hexdigest(), original)
                self.assertEqual((build / "compile_commands.json").read_text(), commands)
                self.assertEqual(list((build / "CMakeFiles").rglob("*.gcno")), [])

                call(configure + ["-DFIXTURE_FAIL=ON", "-DBUILD_TESTING=OFF"])
                call(command, expected=1)
                data = json.loads((report / "run.json").read_text())
                self.assertEqual(data["status"], "failed")
                self.assertEqual(data["tests"]["failed"], 1)
                self.assertNotEqual(report.resolve(), first)
                self.assertTrue(first.is_dir())
                self.assertTrue((report / "coverage.xml").is_file())

    @unittest.skipUnless(GCOVR, "Install tests/coverage-requirements.txt for compiler integration")
    def test_native_fixture_and_failed_test_reporting(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            build = root / "build"
            shutil.copytree(ROOT / "tests/fixtures/coverage", source)
            env = dict(os.environ)
            # An enclosing coverage run must not influence this fixture's counters.
            for key in ("GCOV_PREFIX", "GCOV_PREFIX_STRIP"):
                env.pop(key, None)

            def call(argv):
                result = subprocess.run(argv, env=env, text=True, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT)
                return result.returncode, result.stdout

            configure = ["cmake", "-S", str(source), "-B", str(build),
                         "-DTEAL_ENABLE_COVERAGE=ON", "-DTEAL_COVERAGE_JOBS=2",
                         "-DTEAL_COVERAGE_GCOVR=" + str(GCOVR),
                         "-DTEAL_TEST_HELPER=" + str(ROOT / "cmake/TealCoverage.cmake")]
            rc, output = call(configure)
            self.assertEqual(rc, 0, output)
            command = ["cmake", "--build", str(build), "--target", "coverage", "-j2"]
            rc, output = call(command)
            self.assertEqual(rc, 0, output)
            report = build / "coverage-report"
            first = report.resolve()
            data = json.loads((report / "run.json").read_text())
            self.assertEqual(data["status"], "passed")
            self.assertEqual(data["tests"]["passed"], 1)
            self.assertLess(data["coverage"]["line_percent"], 100)
            files = json.loads((report / "coverage.json").read_text())["files"]
            self.assertEqual({f["file"] for f in files}, {"libcalc.c", "libunused.c"})
            unused = next(f for f in files if f["file"] == "libunused.c")
            self.assertTrue(unused["lines"])
            self.assertTrue(all(line["count"] == 0 for line in unused["lines"]))
            self.assertEqual(list((build / "CMakeFiles").rglob("*.gcda")), [])
            for name in ("index.html", "coverage.xml", "junit.xml", "summary.txt"):
                self.assertTrue((report / name).is_file())

            rc, output = call(configure + ["-DFIXTURE_FAIL=ON"])
            self.assertEqual(rc, 0, output)
            rc, output = call(command)
            self.assertNotEqual(rc, 0, output)
            self.assertNotEqual(report.resolve(), first)
            data = json.loads((report / "run.json").read_text())
            self.assertEqual(data["status"], "failed")
            self.assertEqual(data["tests"]["failed"], 1)
            self.assertTrue((report / "coverage.xml").is_file())
            self.assertTrue(first.is_dir())

            direct = ["python3", "-B", str(ROOT / "tests/coverage_runner.py"),
                      "--source", str(source), "--build", str(build),
                      "--config", str(source / "tests/coverage.json"),
                      "--gcovr", "/no/such/gcovr"]
            previous = report.resolve()
            rc, output = call(direct)
            self.assertNotEqual(rc, 0, output)
            self.assertNotEqual(report.resolve(), previous)
            self.assertEqual(json.loads((report / "run.json").read_text())["status"], "failed")
            self.assertFalse((report / "coverage.xml").exists())

            config = source / "tests/coverage.json"
            policy = json.loads(config.read_text())
            policy["filter"] = "^not-a-source/"
            config.write_text(json.dumps(policy))
            rc, output = call(command)
            self.assertNotEqual(rc, 0, output)
            data = json.loads((report / "run.json").read_text())
            self.assertIn("No production-code coverage", data["error"])

            rc, output = call(configure + ["-DTEAL_ENABLE_COVERAGE=OFF"])
            self.assertEqual(rc, 0, output)
            commands = json.loads((build / "compile_commands.json").read_text())
            self.assertTrue(all("--coverage" not in c["command"] for c in commands))
            self.assertTrue(any("-O2" in c["command"] for c in commands))


if __name__ == "__main__":
    unittest.main()
