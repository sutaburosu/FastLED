#!/usr/bin/env python3
"""fbuild wrapper for FastLED build operations.

This module provides a wrapper around fbuild commands for use by the FastLED
CI system. It integrates with the fbuild DaemonConnection for build and deploy operations.

Usage:
    from ci.util.fbuild_runner import (
        run_fbuild_compile,
        run_fbuild_upload,
    )

    # Compile using fbuild
    success = run_fbuild_compile(build_dir, environment="esp32s3", verbose=True)

    # Upload using fbuild
    success = run_fbuild_upload(build_dir, environment="esp32s3", port="COM3")
"""

from pathlib import Path
from typing import Any

from fbuild import Daemon, connect_daemon


def ensure_fbuild_daemon() -> None:
    """Ensure the fbuild daemon is running."""
    Daemon.ensure_running()


def run_fbuild_compile(
    build_dir: Path,
    environment: str | None = None,
    verbose: bool = False,
    clean: bool = False,
    timeout: float = 1800,
) -> bool:
    """Compile the project using fbuild CLI subprocess.

    Uses ``fbuild build`` as a subprocess so that Ctrl+C terminates it
    immediately (the Rust binary handles SIGINT natively).

    Args:
        build_dir: Project directory containing platformio.ini
        environment: Build environment (None = auto-detect)
        verbose: Enable verbose output
        clean: Perform clean build
        timeout: Maximum build time in seconds (default: 30 minutes)

    Returns:
        True if compilation succeeded, False otherwise
    """
    import shutil
    import subprocess

    if environment is None:
        raise ValueError("environment must be specified for fbuild compilation")

    print("=" * 60)
    print("COMPILING (fbuild)")
    print("=" * 60)

    fbuild_exe = shutil.which("fbuild")
    if fbuild_exe is None:
        print("❌ fbuild CLI not found on PATH")
        return False

    cmd: list[str] = [
        fbuild_exe,
        str(build_dir),
        "build",
        "-e",
        environment,
    ]
    if verbose:
        cmd.append("-v")
    if clean:
        cmd.append("-c")

    print(f"Running: {subprocess.list2cmdline(cmd)}")
    print()

    try:
        result = subprocess.run(
            cmd,
            timeout=timeout,
        )
        success = result.returncode == 0
    except KeyboardInterrupt as ki:
        from ci.util.global_interrupt_handler import handle_keyboard_interrupt

        print("\nKeyboardInterrupt: Stopping compilation")
        handle_keyboard_interrupt(ki)
        raise
    except subprocess.TimeoutExpired:
        print(f"\n❌ Compilation timed out after {timeout}s\n")
        return False
    except Exception as e:
        print(f"\n❌ Compilation error: {e}\n")
        return False

    if success:
        print("\n✅ Compilation succeeded (fbuild)\n")
    else:
        print("\n❌ Compilation failed (fbuild)\n")

    return success


def run_fbuild_upload(
    build_dir: Path,
    environment: str | None = None,
    upload_port: str | None = None,
    verbose: bool = False,
    timeout: float = 1800,
) -> bool:
    """Upload firmware using fbuild.

    This function uses fbuild deploy without monitoring. For upload+monitor,
    use the run_fbuild_deploy function instead.

    Args:
        build_dir: Project directory containing platformio.ini
        environment: Build environment (None = auto-detect)
        upload_port: Serial port (None = auto-detect)
        verbose: Enable verbose output
        timeout: Maximum deploy time in seconds (default: 30 minutes)

    Returns:
        True if upload succeeded, False otherwise
    """
    print("=" * 60)
    print("UPLOADING (fbuild)")
    print("=" * 60)

    ensure_fbuild_daemon()

    try:
        if environment is None:
            raise ValueError("environment must be specified for fbuild upload")

        with connect_daemon(build_dir, environment) as conn:
            success: bool = conn.deploy(
                port=upload_port,
                clean=False,
                skip_build=True,  # Upload-only mode - firmware already compiled
                monitor_after=False,  # Don't monitor - FastLED has its own monitoring
                timeout=timeout,
            )

        if success:
            print("\n✅ Upload succeeded (fbuild)\n")
        else:
            print("\n❌ Upload failed (fbuild)\n")

        return success

    except KeyboardInterrupt as ki:
        from ci.util.global_interrupt_handler import handle_keyboard_interrupt

        print("\nKeyboardInterrupt: Stopping upload")
        handle_keyboard_interrupt(ki)
        raise
    except Exception as e:
        print(f"\n❌ Upload error: {e}\n")
        return False


def run_fbuild_deploy(
    build_dir: Path,
    environment: str | None = None,
    upload_port: str | None = None,
    verbose: bool = False,
    clean: bool = False,
    monitor_after: bool = False,
    timeout: float = 1800,
) -> bool:
    """Deploy firmware using fbuild with optional monitoring.

    Args:
        build_dir: Project directory containing platformio.ini
        environment: Build environment (None = auto-detect)
        upload_port: Serial port (None = auto-detect)
        verbose: Enable verbose output
        clean: Perform clean build before deploy
        monitor_after: Start monitoring after deploy
        timeout: Maximum deploy time in seconds (default: 30 minutes)

    Returns:
        True if deploy (and optional monitoring) succeeded, False otherwise
    """
    print("=" * 60)
    print("DEPLOYING (fbuild)")
    print("=" * 60)

    ensure_fbuild_daemon()

    try:
        if environment is None:
            raise ValueError("environment must be specified for fbuild deploy")

        with connect_daemon(build_dir, environment) as conn:
            success: bool = conn.deploy(
                port=upload_port,
                clean=clean,
                monitor_after=monitor_after,
                timeout=timeout,
            )

        if success:
            print("\n✅ Deploy succeeded (fbuild)\n")
        else:
            print("\n❌ Deploy failed (fbuild)\n")

        return success

    except KeyboardInterrupt as ki:
        from ci.util.global_interrupt_handler import handle_keyboard_interrupt

        print("\nKeyboardInterrupt: Stopping deploy")
        handle_keyboard_interrupt(ki)
        raise
    except Exception as e:
        print(f"\n❌ Deploy error: {e}\n")
        return False


def run_fbuild_monitor(
    build_dir: Path,
    environment: str | None = None,
    port: str | None = None,
    baud_rate: int = 115200,
    timeout: float | None = None,
) -> bool:
    """Monitor serial output using fbuild.

    Args:
        build_dir: Project directory
        environment: Build environment (None = auto-detect)
        port: Serial port (None = auto-detect)
        baud_rate: Serial baud rate (default: 115200)
        timeout: Monitoring timeout in seconds (None = no timeout)

    Returns:
        True if monitoring succeeded, False otherwise
    """
    ensure_fbuild_daemon()

    try:
        if environment is None:
            raise ValueError("environment must be specified for fbuild monitor")

        with connect_daemon(build_dir, environment) as conn:
            success: bool = conn.monitor(
                port=port,
                baud_rate=baud_rate,
                timeout=timeout,
            )

        return success

    except KeyboardInterrupt as ki:
        from ci.util.global_interrupt_handler import handle_keyboard_interrupt

        print("\nKeyboardInterrupt: Stopping monitor")
        handle_keyboard_interrupt(ki)
        raise
    except Exception as e:
        print(f"\n❌ Monitor error: {e}\n")
        return False


def stop_fbuild_daemon() -> None:
    """Stop the fbuild daemon."""
    Daemon.stop()


def get_fbuild_daemon_status() -> dict[str, Any]:
    """Get current fbuild daemon status.

    Returns:
        Dictionary with daemon status information
    """
    return Daemon.status()  # type: ignore[return-value]
