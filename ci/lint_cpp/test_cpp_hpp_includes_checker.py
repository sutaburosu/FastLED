#!/usr/bin/env python3
"""Checker to ban *.cpp.hpp includes in test files (tests/**).

Test files should include the public .h headers, not the implementation
*.cpp.hpp files. Implementation details are linked via the unity build
system; tests should only depend on the public API surface.

This is a hard ban — no opt-out pragma is supported.
"""

import re

from ci.util.check_files import FileContent, FileContentChecker
from ci.util.paths import PROJECT_ROOT


TESTS_ROOT = PROJECT_ROOT / "tests"


class TestCppHppIncludesChecker(FileContentChecker):
    """Checker that bans #include of *.cpp.hpp files inside tests/**. No opt-out."""

    def __init__(self) -> None:
        self.violations: dict[str, list[tuple[int, str]]] = {}

    def should_process_file(self, file_path: str) -> bool:
        """Only process C++ files under tests/."""
        if not file_path.startswith(str(TESTS_ROOT)):
            return False
        if not file_path.endswith((".cpp", ".h", ".hpp")):
            return False
        return True

    def check_file_content(self, file_content: FileContent) -> list[str]:
        """Check for *.cpp.hpp includes and report violations."""
        violations: list[tuple[int, str]] = []
        in_multiline_comment = False

        cpp_hpp_include_pattern = re.compile(
            r'#\s*include\s+[<"]([^>"]+\.cpp\.hpp)[>"]'
        )

        for line_number, line in enumerate(file_content.lines, 1):
            stripped = line.strip()

            # Track multi-line comment state
            if "/*" in line:
                in_multiline_comment = True
            if "*/" in line:
                in_multiline_comment = False
                continue

            if in_multiline_comment:
                continue

            if stripped.startswith("//"):
                continue

            code_part = line.split("//")[0]

            match = cpp_hpp_include_pattern.search(code_part)
            if match:
                included_file = match.group(1)
                # Suggest the .h header replacement
                h_header = re.sub(r"\.cpp\.hpp$", ".h", included_file)
                # For .impl.cpp.hpp, strip both .impl and .cpp.hpp
                h_header = re.sub(r"\.impl\.h$", ".h", h_header)
                violations.append(
                    (
                        line_number,
                        f"{stripped} - Including *.cpp.hpp files in tests is banned (hard ban, no opt-out). "
                        f'Include the public header instead: #include "{h_header}"',
                    )
                )

        if violations:
            self.violations[file_content.path] = violations

        return []


def main() -> None:
    """Run test *.cpp.hpp includes checker standalone."""
    from ci.util.check_files import run_checker_standalone

    checker = TestCppHppIncludesChecker()
    run_checker_standalone(
        checker,
        [str(TESTS_ROOT)],
        "Found *.cpp.hpp includes in test files (tests should include .h headers, not .cpp.hpp)",
    )


if __name__ == "__main__":
    main()
