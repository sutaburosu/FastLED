#!/usr/bin/env python3
# pyright: reportUnknownMemberType=false
"""Checker for global-scope C ctype functions - should use fl:: variants instead."""

import re

from ci.util.check_files import FileContent, FileContentChecker, is_excluded_file


# C standard library character functions that have fl:: equivalents
CTYPE_FUNCTIONS = [
    "isspace",
    "isdigit",
    "isalpha",
    "isalnum",
    "isupper",
    "islower",
    "tolower",
    "toupper",
]

# Match bare function call or ::function call, but not fl::function
# Uses negative lookbehind to exclude fl:: prefix
# (?<!\w) prevents matching substring endings like "myisspace"
# (?<!fl::) prevents matching fl::isspace
_PATTERN = re.compile(r"(?<!\w)(?::{2})?\b(" + "|".join(CTYPE_FUNCTIONS) + r")\s*\(")

# Match fl:: prefixed calls (these are OK)
_FL_PATTERN = re.compile(r"\bfl::(" + "|".join(CTYPE_FUNCTIONS) + r")\s*\(")


class CtypeGlobalChecker(FileContentChecker):
    """Checker for global-scope C ctype function usage."""

    def __init__(self):
        self.violations: dict[str, list[tuple[int, str]]] = {}

    def should_process_file(self, file_path: str) -> bool:
        """Check if file should be processed."""
        if not file_path.endswith((".cpp", ".h", ".hpp", ".ino", ".cpp.hpp")):
            return False

        if is_excluded_file(file_path):
            return False

        if "third_party" in file_path or "thirdparty" in file_path:
            return False

        # Exclude the cctype.h definition file itself
        normalized = file_path.replace("\\", "/")
        if normalized.endswith("fl/stl/cctype.h"):
            return False

        return True

    def check_file_content(self, file_content: FileContent) -> list[str]:
        """Check file content for global-scope ctype function usage."""
        violations: list[tuple[int, str]] = []
        in_multiline_comment = False

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

            # Remove single-line comment portion
            code_part = line.split("//")[0]

            # Skip lines with suppression comment
            if "// ok ctype" in line or "// okay ctype" in line:
                continue

            # Find all ctype function calls
            all_matches = _PATTERN.findall(code_part)
            if not all_matches:
                continue

            # Subtract fl:: prefixed ones (which are fine)
            fl_matches = _FL_PATTERN.findall(code_part)

            # Count bare/global uses
            bare_count = len(all_matches) - len(fl_matches)
            if bare_count > 0:
                for func in set(all_matches):
                    # Check if this specific function appears without fl:: prefix
                    bare_pattern = re.compile(r"(?<!\w)(?:::)?\b" + func + r"\s*\(")
                    fl_specific = re.compile(r"\bfl::" + func + r"\s*\(")
                    bare_hits = bare_pattern.findall(code_part)
                    fl_hits = fl_specific.findall(code_part)
                    if len(bare_hits) > len(fl_hits):
                        violations.append(
                            (
                                line_number,
                                f"Use fl::{func}() instead of {func}() or ::{func}(): {stripped}",
                            )
                        )

        if violations:
            self.violations[file_content.path] = violations

        return []  # MUST return empty list


def main() -> None:
    """Run ctype global checker standalone."""
    from ci.util.check_files import run_checker_standalone
    from ci.util.paths import PROJECT_ROOT

    checker = CtypeGlobalChecker()
    run_checker_standalone(
        checker,
        [str(PROJECT_ROOT / "src")],
        "Found global-scope C ctype function usage (use fl:: variants)",
    )


if __name__ == "__main__":
    main()
