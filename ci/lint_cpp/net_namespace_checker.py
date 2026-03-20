#!/usr/bin/env python3
"""Checker to ensure headers in src/fl/<subdir>/ use proper fl::<subdir>:: namespaces.

Directory-to-namespace mapping:
  src/fl/<subdir>/*.h          → must use namespace fl::<subdir>
  src/fl/<subdir>/child/*.h    → must use namespace fl::<subdir>::child
  (and so on for any nested subdirectory)

Also checks .cpp.hpp implementation files.

Pure-include umbrella headers (no namespace declarations at all) are skipped.

No suppression — this rule cannot be bypassed.
"""

import re
from pathlib import PurePosixPath

from ci.util.check_files import FileContent, FileContentChecker
from ci.util.paths import PROJECT_ROOT


# Regex for namespace declarations: 'namespace foo {' or 'namespace foo::bar {'
_NAMESPACE_RE = re.compile(r"\bnamespace\s+([\w:]+)\s*\{")


class SubdirNamespaceChecker(FileContentChecker):
    """Checks that headers in src/fl/<subdir>/ use the proper fl::<subdir>:: namespace."""

    def __init__(self, subdir: str) -> None:
        self.subdir = subdir
        self.root = str(PROJECT_ROOT / "src" / "fl" / subdir).replace("\\", "/")
        self.violations: dict[str, list[tuple[int, str]]] = {}

    def should_process_file(self, file_path: str) -> bool:
        normalized = file_path.replace("\\", "/")
        if not normalized.startswith(self.root + "/") and normalized != self.root:
            return False
        return file_path.endswith(".h") or file_path.endswith(".cpp.hpp")

    @staticmethod
    def _expected_namespace_parts(file_path: str) -> list[str]:
        """Compute expected namespace parts from file path.

        src/fl/net/fetch.h                 -> ["fl", "net"]
        src/fl/net/http/stream_client.h    -> ["fl", "net", "http"]
        src/fl/audio/synth.h               -> ["fl", "audio"]
        src/fl/audio/detectors/beat.h      -> ["fl", "audio", "detectors"]
        src/fl/audio/fft/fft.h             -> ["fl", "audio", "fft"]
        """
        normalized = file_path.replace("\\", "/")
        # Find portion after src/fl/
        marker = "/src/fl/"
        idx = normalized.find(marker)
        if idx < 0:
            return ["fl"]
        rel = normalized[idx + len(marker) :]
        # rel is e.g. "net/fetch.h" or "audio/detectors/beat.h"
        parts = PurePosixPath(rel).parent.parts  # ("net",) or ("audio", "detectors")
        return ["fl"] + list(parts)

    def check_file_content(self, file_content: FileContent) -> list[str]:
        expected = self._expected_namespace_parts(file_content.path)
        # e.g. ["fl", "audio"] or ["fl", "audio", "detectors"]

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
                    f"Header in fl/{self.subdir}/ must use namespace {expected_ns}; "
                    f"missing namespace part(s): {', '.join(missing)}",
                )
            ]

        return []


# Backward-compatible alias
NetNamespaceChecker = lambda: SubdirNamespaceChecker("net")  # noqa: E731


def main() -> None:
    """Run checker standalone."""
    import sys

    from ci.util.check_files import run_checker_standalone

    # Determine which subdirs to check based on CLI args
    subdirs = [arg for arg in sys.argv[1:] if not arg.startswith("-")]
    if not subdirs:
        subdirs = ["net", "audio"]

    for subdir in subdirs:
        checker = SubdirNamespaceChecker(subdir)
        run_checker_standalone(
            checker,
            [str(PROJECT_ROOT / "src" / "fl" / subdir)],
            f"Found fl/{subdir}/ headers with incorrect namespace",
            extensions=[".h", ".cpp.hpp"],
        )


if __name__ == "__main__":
    main()
