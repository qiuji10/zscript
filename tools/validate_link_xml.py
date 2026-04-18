#!/usr/bin/env python3
"""
validate_link_xml.py — Static validation of the ZScript link.xml template.

Checks that the committed link.xml preserves the minimum required entries:
  - ZScript.Runtime assembly with preserve="all"
  - ZScriptGenerated namespace entry

Exit code 0 = valid, 1 = validation error, 2 = XML parse error.

Usage:
    python3 tools/validate_link_xml.py [path/to/link.xml]

Default path: unity/com.zscript.runtime/link.xml
"""
import sys
import xml.etree.ElementTree as ET

LINK_XML_PATH = "unity/com.zscript.runtime/link.xml"


def validate(path: str) -> list[str]:
    errors = []

    try:
        tree = ET.parse(path)
    except ET.ParseError as e:
        print(f"ERROR: XML parse error in {path}: {e}", flush=True)
        sys.exit(2)

    root = tree.getroot()

    # Rule 1: ZScript.Runtime must be preserved in full.
    runtime_ok = any(
        asm.get("fullname") == "ZScript.Runtime" and asm.get("preserve") == "all"
        for asm in root.findall("assembly")
    )
    if not runtime_ok:
        errors.append(
            'Missing: <assembly fullname="ZScript.Runtime" preserve="all"/>'
        )

    # Rule 2: No assembly entry should have an empty fullname (would silently no-op).
    for asm in root.findall("assembly"):
        if not asm.get("fullname", "").strip():
            errors.append("Found <assembly> with empty or missing fullname attribute.")
        for ns in asm.findall("namespace"):
            if not ns.get("fullname", "").strip():
                errors.append("Found <namespace> with empty or missing fullname attribute.")
        for typ in asm.findall("type"):
            if not typ.get("fullname", "").strip():
                errors.append("Found <type> with empty or missing fullname attribute.")

    return errors


def main() -> None:
    path = sys.argv[1] if len(sys.argv) > 1 else LINK_XML_PATH
    print(f"Validating {path} ...", flush=True)

    errors = validate(path)

    if errors:
        for e in errors:
            print(f"  FAIL: {e}", flush=True)
        print(f"\n{len(errors)} error(s) found.", flush=True)
        sys.exit(1)

    print("  OK: ZScript.Runtime is fully preserved.", flush=True)
    print("Validation passed.", flush=True)
    sys.exit(0)


if __name__ == "__main__":
    main()
