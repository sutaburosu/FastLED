#!/usr/bin/env python3
"""Checker to ensure headers in src/fl/net/ use proper fl::net:: namespaces.

Directory-to-namespace mapping:
  src/fl/net/*.h       → must use namespace fl::net
  src/fl/net/http/*.h  → must use namespace fl::net::http
  src/fl/net/ble/*.h   → must use namespace fl::net::ble
  (and so on for any future subdirectory)

Pure-include umbrella headers (no namespace declarations at all) are skipped.

No suppression — this rule cannot be bypassed.
"""

import re
from pathlib import PurePosixPath

from ci.util.check_files import FileContent, FileContentChecker
from ci.util.paths import PROJECT_ROOT


NET_ROOT = str(PROJECT_ROOT / "src" / "fl" / "net").replace("\\", "/")

# Regex for namespace declarations: 'namespace foo {' or 'namespace foo::bar {'
_NAMESPACE_RE = re.compile(r"\bnamespace\s+([\w:]+)\s*\{")


class NetNamespaceChecker(FileContentChecker):
    """Checks that headers in src/fl/net/ use the proper fl::net:: namespace."""

    def __init__(self) -> None:
        self.violations: dict[str, list[tuple[int, str]]] = {}

    def should_process_file(self, file_path: str) -> bool:
        normalized = file_path.replace("\\", "/")
        if not normalized.startswith(NET_ROOT + "/") and normalized != NET_ROOT:
            return False
        return file_path.endswith(".h")

    @staticmethod
    def _expected_namespace_parts(file_path: str) -> list[str]:
        """Compute expected namespace parts from file path.

        src/fl/net/fetch.h            -> ["fl", "net"]
        src/fl/net/http/stream_client.h -> ["fl", "net", "http"]
        src/fl/net/ble/foo.h          -> ["fl", "net", "ble"]
        """
        normalized = file_path.replace("\\", "/")
        # Find portion after src/fl/
        marker = "/src/fl/"
        idx = normalized.find(marker)
        if idx < 0:
            return ["fl", "net"]
        rel = normalized[idx + len(marker) :]
        # rel is e.g. "net/fetch.h" or "net/http/stream_client.h"
        parts = PurePosixPath(rel).parent.parts  # ("net",) or ("net", "http")
        return ["fl"] + list(parts)

    def check_file_content(self, file_content: FileContent) -> list[str]:
        expected = self._expected_namespace_parts(file_content.path)
        # e.g. ["fl", "net"] or ["fl", "net", "http"]

        # Collect all namespace identifiers from declarations in the file
        found_parts: set[str] = set()
        has_any_namespace = False

        for line in file_content.lines:
            stripped = line.strip()
            # Skip obvious comment-only lines
            if (
                stripped.startswith("//")
                or stripped.startswith("/*")
                or stripped.startswith("*")
            ):
                continue
            # Remove inline comments
            code = stripped.split("//")[0]

            for match in _NAMESPACE_RE.finditer(code):
                has_any_namespace = True
                ns_name = match.group(1)
                for part in ns_name.split("::"):
                    if part:
                        found_parts.add(part)

        # Pure-include umbrella headers with no namespace declarations — skip
        if not has_any_namespace:
            return []

        # Check that all expected namespace parts appear
        missing = [p for p in expected if p not in found_parts]

        if missing:
            expected_ns = "::".join(expected)
            self.violations[file_content.path] = [
                (
                    1,
                    f"Header in fl/net/ must use namespace {expected_ns}; "
                    f"missing namespace part(s): {', '.join(missing)}",
                )
            ]

        return []


def main() -> None:
    """Run checker standalone."""
    from ci.util.check_files import run_checker_standalone

    checker = NetNamespaceChecker()
    run_checker_standalone(
        checker,
        [str(PROJECT_ROOT / "src" / "fl" / "net")],
        "Found fl/net/ headers with incorrect namespace",
        extensions=[".h"],
    )


if __name__ == "__main__":
    main()
