#!/usr/bin/env python3
"""Regression checks for the shared coverage runner; no camera/target access."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

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
