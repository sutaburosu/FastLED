#!/usr/bin/env python3
# pyright: reportUnknownMemberType=false
"""Checker to ensure test files mirror the directory structure of source files.

This checker validates that test files in tests/ follow the same directory
structure as their corresponding source files in src/. For example:
- If src/fl/stl/flat_map.h exists, the test should be at tests/fl/stl/flat_map.cpp
- If the test is at tests/fl/flat_map.cpp, this is flagged as an error

This ensures test organization matches source organization for maintainability.
"""

from pathlib import Path

from ci.util.check_files import FileContent, FileContentChecker
from ci.util.paths import PROJECT_ROOT


TESTS_ROOT = PROJECT_ROOT / "tests"
SRC_ROOT = PROJECT_ROOT / "src"

# Test files that are exempt from path matching (infrastructure/entry points)
EXCLUDED_TEST_FILES = {
    "doctest_main.cpp",  # Test framework entry point
}

# Pre-existing test files that don't match source paths.
# These are grandfathered in — new tests MUST have an exact source match.
# Fix these incrementally by renaming/moving to match their source file.
_LEGACY_ALLOWLIST: set[str] = {
    "tests/fastled_core.cpp",
    "tests/fl/active_strip_data_json.cpp",
    "tests/fl/alloca.hpp",
    "tests/fl/audio.cpp",
    "tests/fl/audio/detector/downbeat_synthetic.hpp",
    "tests/fl/audio/detector/vocal_real_audio.hpp",
    "tests/fl/audio/fft.cpp",
    "tests/fl/audio/gain.hpp",
    "tests/fl/audio/test_helpers.hpp",
    "tests/fl/audio_reactive.cpp",
    "tests/fl/audio_url.cpp",
    "tests/fl/bytestream.cpp",
    "tests/fl/channels/channel_data_padding.cpp",
    "tests/fl/channels/channel_manager.cpp",
    "tests/fl/channels/channels.cpp",
    "tests/fl/channels/spi_channel.cpp",
    "tests/fl/channels/wave8_spi.cpp",
    "tests/fl/chipsets/hd108_chipset.cpp",
    "tests/fl/clamp.cpp",
    "tests/fl/codec/decode_file.hpp",
    "tests/fl/coroutine.cpp",
    "tests/fl/detail/async_logger_error_detection.hpp",
    "tests/fl/detail/async_logger_output.hpp",
    "tests/fl/fltest_self_test.cpp",
    "tests/fl/force_inline.cpp",
    "tests/fl/fx/2d/animartrix_test.hpp",
    "tests/fl/gfx/draw_disc.hpp",
    "tests/fl/gfx/draw_disc_16.hpp",
    "tests/fl/gfx/draw_line.hpp",
    "tests/fl/gfx/draw_ring.hpp",
    "tests/fl/gfx/draw_thick_line.hpp",
    "tests/fl/gfx/perf_primitives.hpp",
    "tests/fl/hsv2rgb_accuracy.cpp",
    "tests/fl/map_range.hpp",
    "tests/fl/memory.cpp",
    "tests/fl/noise_range.cpp",
    "tests/fl/noise_ring.cpp",
    "tests/fl/numeric_limits.hpp",
    "tests/fl/power_estimation.cpp",
    "tests/fl/slice.cpp",
    "tests/fl/stl/allocator_move.cpp",
    "tests/fl/stl/asio/http/chunked_encoding.cpp",
    "tests/fl/stl/asio/http/http_promise.cpp",
    "tests/fl/stl/asio/http/stream_client.cpp",
    "tests/fl/stl/asio/http/stream_server.cpp",
    "tests/fl/stl/asio/http/stream_transport.cpp",
    "tests/fl/stl/asio/test_endpoint.cpp",
    "tests/fl/stl/asio/test_error_code.cpp",
    "tests/fl/stl/asio/test_tcp_acceptor.cpp",
    "tests/fl/stl/asio/test_tcp_socket.cpp",
    "tests/fl/stl/cstdint.cpp",
    "tests/fl/stl/delay.cpp",
    "tests/fl/stl/function_list.cpp",
    "tests/fl/stl/math.cpp",
    "tests/fl/stl/qsort.cpp",
    "tests/fl/stl/random.cpp",
    "tests/fl/stl/strstream_integers.cpp",
    "tests/fl/stl/test_file_handle.cpp",
    "tests/fl/types.cpp",
    "tests/fl/unused.cpp",
    "tests/platforms/esp/32/drivers/i2s/i2s_lcd_cam_mock.cpp",
    "tests/platforms/esp/32/drivers/lcd_cam/lcd_rgb_mock.cpp",
    "tests/platforms/esp/32/drivers/parlio/parlio_driver.cpp",
    "tests/platforms/esp/32/drivers/rmt/rmt_5/rmt5_channel_driver.cpp",
    "tests/platforms/esp/32/drivers/rmt/rmt_5/rmt5_nibble_lut.cpp",
    "tests/platforms/esp/32/drivers/spi/channel_driver_spi_routing.cpp",
    "tests/platforms/esp/32/drivers/spi/spi_batching_logic.cpp",
    "tests/platforms/esp/32/drivers/spi/spi_peripheral.cpp",
    "tests/platforms/esp/32/interrupts/riscv_interrupts.cpp",
    "tests/platforms/platform_init.cpp",
    "tests/platforms/shared/active_strip_data/stub_led_capture.cpp",
    "tests/platforms/shared/blend8.cpp",
    "tests/platforms/shared/clockless_block_generic.cpp",
    "tests/platforms/shared/fl_init_verification.cpp",
    "tests/platforms/shared/lcd50.cpp",
    "tests/platforms/shared/serial_printf.cpp",
    "tests/platforms/shared/sketch_runner.cpp",
    "tests/platforms/shared/spi_force_software.cpp",
}


class TestPathStructureChecker(FileContentChecker):
    """Checker class for test file path structure validation."""

    def __init__(self):
        self.violations: dict[str, list[tuple[int, str]]] = {}

    def should_process_file(self, file_path: str) -> bool:
        """Check if file should be processed for path structure validation."""
        # Only check files in tests directory
        if not file_path.startswith(str(TESTS_ROOT)):
            return False

        # Check .cpp and .hpp test files (sub-tests use .hpp extension)
        if not file_path.endswith((".cpp", ".hpp")):
            return False

        test_path = Path(file_path)

        # Skip excluded files (infrastructure/entry points)
        if test_path.name in EXCLUDED_TEST_FILES:
            return False

        # Skip tests/misc/ directory (these tests don't need to match source structure)
        # Skip tests/profile/ directory (standalone performance benchmarks)
        # Skip tests/shared/ directory (shared test infrastructure)
        # Skip any test_utils/ directories (test utilities)
        try:
            rel_path = test_path.relative_to(TESTS_ROOT)
            if rel_path.parts[0] in ("misc", "profile", "shared"):
                return False
            if "test_utils" in rel_path.parts:
                return False
        except (ValueError, IndexError):
            pass

        return True

    def check_file_content(self, file_content: FileContent) -> list[str]:
        """Check if test file path matches the source file directory structure.

        Rule: tests/**/file.cpp must match src/**/file.h or src/**/file.hpp
        Exception: Tests in tests/misc/ are exempt (don't need to match).
        Exception: Tests with '// ok standalone' comment are exempt.
        """
        test_path = Path(file_content.path)

        # Get the relative path from tests root: tests/fl/flat_map.cpp -> fl/flat_map
        rel_from_tests = test_path.relative_to(TESTS_ROOT)
        test_name_no_ext = rel_from_tests.with_suffix("")  # Remove .cpp

        # Check for source file at exact matching path
        # Check for .h, .hpp, or .cpp.hpp files (ESP32 implementations use .cpp.hpp)
        expected_source_h = SRC_ROOT / test_name_no_ext.with_suffix(".h")
        expected_source_hpp = SRC_ROOT / test_name_no_ext.with_suffix(".hpp")
        expected_source_cpp_hpp = SRC_ROOT / (str(test_name_no_ext) + ".cpp.hpp")

        # If matching source file exists at the expected location, no issue
        if (
            expected_source_h.exists()
            or expected_source_hpp.exists()
            or expected_source_cpp_hpp.exists()
        ):
            return []

        # Skip legacy allowlisted files (pre-existing mismatches)
        rel_posix = rel_from_tests.as_posix()
        legacy_key = f"tests/{rel_posix}"
        if legacy_key in _LEGACY_ALLOWLIST:
            return []

        # Check if file has "// ok standalone" comment in first few lines
        for line in file_content.lines[:5]:  # Check first 5 lines
            if "// ok standalone" in line.lower():
                return []  # Exempt from path matching requirement

        # Source file doesn't exist at expected location
        # Flag as violation (test file has no corresponding source at matching path)
        rel_current_test = test_path.relative_to(PROJECT_ROOT)

        message = (
            f"Test file has no corresponding source file at matching path. "
            f"Test is at '{rel_current_test}' but no source file found at "
            f"'src/{rel_from_tests.with_suffix('.h')}', 'src/{rel_from_tests.with_suffix('.hpp')}', "
            f"or 'src/{rel_from_tests.with_name(rel_from_tests.stem + '.cpp.hpp')}'. "
            f"\n\n"
            f"REQUIRED ACTIONS (in order of preference):\n"
            f"  1. RENAME the test to match the source file it's testing (best option)\n"
            f"  2. MERGE this test into an existing test file that tests the same source — each test\n"
            f"     file costs compile time, so consolidating into fewer files is strongly preferred\n"
            f"  3. MOVE to 'tests/misc/{test_path.name}' if this truly doesn't test a specific source file\n\n"
            f"⚠️  DO NOT add '// ok standalone' unless absolutely necessary. This amnesty is a last\n"
            f"resort for rare infrastructure files that genuinely cannot be organized. AI agents\n"
            f"should NEVER add this comment — instead fix the path or consolidate tests.\n\n"
            f"Avoid creating tests in 'tests/misc/' - prefer mirroring source directory structure.\n"
            f"Test organization should mirror source organization for maintainability.\n"
            f"Note: Source matcher checks .h, .hpp, and .cpp.hpp files."
        )

        self.violations[file_content.path] = [(1, message)]
        return []  # We collect violations internally


def main() -> None:
    """Run test path structure checker standalone."""
    from ci.util.check_files import run_checker_standalone

    checker = TestPathStructureChecker()
    run_checker_standalone(
        checker,
        [str(TESTS_ROOT)],
        "Found test files with incorrect path structure",
        extensions=[".cpp", ".hpp"],
    )


if __name__ == "__main__":
    main()
