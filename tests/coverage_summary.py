#!/usr/bin/env python3
"""Combine per-project run.json files without mixing their coverage denominators."""
import argparse
import html
import json
import os
from pathlib import Path
import sys
from urllib.parse import quote

from coverage_runner import write_json


def summarize(paths, output):
    records = [json.loads(path.read_text()) for path in paths]
    output.mkdir(parents=True, exist_ok=True)
    rows = []
    for data in records:
        tests = data.get("tests", {})
        coverage = data.get("coverage", {})
        report = Path(data["report"])
        link = quote(os.path.relpath(report / "index.html", output))
        cells = [
            '<a href="' + html.escape(link, quote=True) + '">' + html.escape(data["project"]) + "</a>",
            html.escape((data.get("commit") or "unknown")[:12]) + (" (dirty)" if data.get("tracked_changes") else ""),
            html.escape(data.get("machine", "unknown")),
            html.escape(data["status"]),
            "{}/{}; {} skipped".format(tests.get("passed", 0), tests.get("total", 0), tests.get("skipped", 0)),
        ]
        for name in ("line", "function", "branch"):
            cells.append("{}% ({}/{})".format(coverage[name + "_percent"],
                         coverage[name + "_covered"], coverage[name + "_total"])
                         if name + "_percent" in coverage else "unavailable")
        cells.append(str(len(data.get("unmeasured_sources", []))))
        rows.append("<tr>" + "".join("<td>" + cell + "</td>" for cell in cells) + "</tr>")
        if data.get("error"):
            rows.append('<tr><td colspan="9">' + html.escape(data["error"]) + "</td></tr>")
    document = ('<!doctype html><html><head><meta charset="utf-8"><title>Teal coverage</title></head>'
                '<body><h1>Teal coverage</h1><p>Native test-suite coverage, not hardware validation. '
                'Percentages are kept separate by project; see each run.json for scope and toolchain.</p>'
                '<table border="1"><thead><tr>' + "".join(
                    "<th>" + x + "</th>" for x in ("Project", "Commit", "Platform", "Result", "CTest results",
                                                  "Lines", "Functions", "Branches", "Unmeasured sources"))
                + "</tr></thead><tbody>" + "".join(rows) + "</tbody></table></body></html>\n")
    (output / "index.html").write_text(document)
    write_json(output / "summary.json", records)
    for data in records:
        coverage = data.get("coverage", {})
        print("{}: {} | lines {}% | functions {}% | branches {}%".format(
            data["project"], data["status"], coverage.get("line_percent", "n/a"),
            coverage.get("function_percent", "n/a"), coverage.get("branch_percent", "n/a")))
    return 0 if records and all(d["status"] == "passed" for d in records) else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        return summarize(args.reports, args.output.resolve())
    except (OSError, ValueError, KeyError) as error:
        print("coverage summary: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
