#!/usr/bin/env python3

from __future__ import annotations

import sys
from collections import defaultdict
from pathlib import Path
import xml.etree.ElementTree as ET


def label_for_artifact(name: str) -> str:
    if name.startswith("native-junit-"):
        return f"Native tests ({name.removeprefix('native-junit-')})"
    if name.startswith("unity-junit-"):
        return f"Unity EditMode tests ({name.removeprefix('unity-junit-')})"
    return name


def parse_counts(xml_path: Path) -> tuple[int, int, int]:
    root = ET.parse(xml_path).getroot()
    nodes = [root]

    tests = failures = skipped = 0
    found_suite_attrs = False

    for node in nodes:
        if node.tag not in {"testsuite", "testsuites"}:
            continue

        node_tests = node.attrib.get("tests")
        node_failures = node.attrib.get("failures")
        node_errors = node.attrib.get("errors")
        node_skipped = node.attrib.get("skipped", node.attrib.get("disabled", "0"))

        if node_tests is not None:
            found_suite_attrs = True
            tests += int(node_tests)
            failures += int(node_failures or 0) + int(node_errors or 0)
            skipped += int(node_skipped or 0)

    if found_suite_attrs:
        return tests, failures, skipped

    cases = root.findall(".//testcase")
    tests = len(cases)
    for case in cases:
        if case.find("skipped") is not None:
            skipped += 1
        if case.find("failure") is not None or case.find("error") is not None:
            failures += 1
    return tests, failures, skipped


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: render_junit_summary.py <artifacts-dir>", file=sys.stderr)
        return 2

    artifacts_dir = Path(sys.argv[1])
    grouped: dict[str, list[Path]] = defaultdict(list)

    for xml_path in sorted(artifacts_dir.rglob("*.xml")):
        try:
            rel = xml_path.relative_to(artifacts_dir)
        except ValueError:
            continue
        artifact_name = rel.parts[0] if len(rel.parts) > 1 else xml_path.stem
        grouped[artifact_name].append(xml_path)

    print("### Test summary")
    print()

    if not grouped:
        print("No JUnit test reports were found.")
        return 0

    print("| Test suite | Tests | Passed | Skipped | Failed |")
    print("|---|---:|---:|---:|---:|")

    for artifact_name in sorted(grouped):
        tests = failures = skipped = 0
        for xml_path in grouped[artifact_name]:
            t, f, s = parse_counts(xml_path)
            tests += t
            failures += f
            skipped += s
        passed = max(tests - failures - skipped, 0)
        print(
            f"| {label_for_artifact(artifact_name)} | "
            f"{tests} | {passed} | {skipped} | {failures} |"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
