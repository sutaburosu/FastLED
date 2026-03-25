"""Boards that use fbuild instead of PlatformIO for compilation."""

# These boards use fbuild (Rust-based build system) instead of direct PlatformIO CLI.
# fbuild provides faster builds via daemon-based caching and toolchain management.
FBUILD_BOARDS: frozenset[str] = frozenset({"esp32s3", "esp32c3", "esp32c6"})
