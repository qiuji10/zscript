#!/usr/bin/env python3
"""Convert a minimal NUnit3 result file into JUnit XML.

This is intended for Unity's EditMode test output, whose root element is
`<test-run>`. GitHub's `mikepenz/action-junit-report` expects JUnit XML, so we
normalize the NUnit3 output into a single JUnit testsuite.
"""

from __future__ import annotations

import argparse
import sys
import xml.etree.ElementTree as ET


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", help="Path to the NUnit3 XML file")
    parser.add_argument("output", help="Path to write the JUnit XML file")
    return parser.parse_args()


def as_float(value: str | None) -> str:
    if not value:
        return "0"
    try:
        return str(float(value))
    except ValueError:
        return "0"


def first_text(parent: ET.Element, tag: str) -> str:
    node = parent.find(tag)
    if node is None:
        return ""
    return "".join(node.itertext()).strip()


def junit_case_from_nunit(test_case: ET.Element) -> ET.Element:
    attrs = {
        "name": test_case.get("name", ""),
        "classname": test_case.get("classname", ""),
        "time": as_float(test_case.get("duration")),
    }
    junit_case = ET.Element("testcase", attrs)

    result = (test_case.get("result") or "").lower()
    message = first_text(test_case, "./failure/message")
    stack = first_text(test_case, "./failure/stack-trace")
    output = first_text(test_case, "./output")

    if result == "failed":
        failure = ET.SubElement(
            junit_case,
            "failure",
            {"message": message or "test failed", "type": "AssertionError"},
        )
        failure.text = stack or message or "test failed"
    elif result in {"skipped", "inconclusive"}:
        skipped = ET.SubElement(
            junit_case,
            "skipped",
            {"message": message or result or "skipped"},
        )
        skipped.text = message or result or "skipped"

    if output:
        system_out = ET.SubElement(junit_case, "system-out")
        system_out.text = output

    return junit_case


def main() -> int:
    args = parse_args()
    tree = ET.parse(args.input)
    root = tree.getroot()
    if root.tag != "test-run":
        raise SystemExit(f"expected NUnit3 <test-run> root, got <{root.tag}>")

    test_cases = root.findall(".//test-case")
    tests = len(test_cases)
    failures = sum(1 for case in test_cases if (case.get("result") or "").lower() == "failed")
    skipped = sum(
        1 for case in test_cases if (case.get("result") or "").lower() in {"skipped", "inconclusive"}
    )

    assembly = root.find(".//test-suite[@type='Assembly']")
    suite_name = assembly.get("name", "Unity EditMode Tests") if assembly is not None else "Unity EditMode Tests"

    testsuites = ET.Element("testsuites")
    suite = ET.SubElement(
        testsuites,
        "testsuite",
        {
            "name": suite_name,
            "tests": str(tests),
            "failures": str(failures),
            "errors": "0",
            "skipped": str(skipped),
            "time": as_float(root.get("duration")),
        },
    )

    for case in test_cases:
        suite.append(junit_case_from_nunit(case))

    ET.ElementTree(testsuites).write(args.output, encoding="utf-8", xml_declaration=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
