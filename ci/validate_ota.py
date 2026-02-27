"""OTA validation helpers for FastLED hardware-in-the-loop testing.

Provides WiFi management and HTTP validation flows for --ota mode.
Tests the fl::OTA web-based firmware update interface by verifying:
  - Authentication (Basic Auth: admin:<password>)
  - Unauthenticated access is rejected (401)
  - OTA upload endpoint exists and handles errors
"""

from __future__ import annotations

import base64
from typing import TYPE_CHECKING

import httpx
from colorama import Fore, Style

from ci.rpc_client import RpcClient, RpcTimeoutError
from ci.util.global_interrupt_handler import handle_keyboard_interrupt_properly
from ci.validate_net import create_wifi_manager


if TYPE_CHECKING:
    from ci.util.serial_interface import SerialInterface


async def run_ota_validation(
    upload_port: str,
    serial_iface: "SerialInterface | None",
    timeout: float = 60.0,
) -> int:
    """Run OTA validation (--ota).

    1. Send startOta RPC to ESP32 (starts WiFi AP + OTA HTTP server)
    2. Connect host to ESP32's WiFi AP
    3. Run HTTP tests against OTA endpoints with auth checks
    4. Send stopOta RPC to cleanup
    5. Restore host WiFi

    Args:
        upload_port: Serial port for RPC communication
        serial_iface: Pre-created serial interface
        timeout: RPC timeout in seconds

    Returns:
        Exit code (0 = success, 1 = failure)
    """
    wifi = create_wifi_manager()

    print()
    print("=" * 60)
    print("OTA VALIDATION MODE")
    print("=" * 60)
    print()

    # Save current WiFi SSID for restore
    original_ssid = wifi.get_current_ssid()
    if original_ssid:
        print(f"  Current WiFi: '{original_ssid}' (will restore after test)")
    else:
        print("  No current WiFi connection detected")

    client: RpcClient | None = None

    try:
        # Connect to device via RPC
        print(f"\n  Connecting to device on {upload_port}...")
        client = RpcClient(upload_port, timeout=timeout, serial_interface=serial_iface)
        await client.connect(boot_wait=3.0, drain_boot=True)
        print(f"  {Fore.GREEN}Connected to device{Style.RESET_ALL}")

        # Step 1: Start OTA on device
        print("\n--- Step 1: Start WiFi AP + OTA Server on ESP32 ---")
        response = await client.send("startOta", timeout=30.0)
        ota_info = response.data

        if not isinstance(ota_info, dict) or not ota_info.get("success"):
            error = (
                ota_info.get("error", "Unknown error")
                if isinstance(ota_info, dict)
                else str(ota_info)
            )
            print(f"  {Fore.RED}Failed to start OTA: {error}{Style.RESET_ALL}")
            return 1

        ssid = ota_info.get("ssid", "")
        password = ota_info.get("password", "")
        ip = ota_info.get("ip", "192.168.4.1")
        port = ota_info.get("port", 80)
        ota_password = ota_info.get("ota_password", "")
        hostname = ota_info.get("hostname", "")
        print(
            f"  {Fore.GREEN}OTA started: SSID={ssid}, IP={ip}:{port}, hostname={hostname}{Style.RESET_ALL}"
        )

        # Step 2: Connect host to ESP32 WiFi AP
        print("\n--- Step 2: Connect Host to ESP32 WiFi AP ---")
        if not wifi.connect(ssid, password):
            print(f"  {Fore.RED}Failed to connect host to ESP32 AP{Style.RESET_ALL}")
            return 1

        # Step 3: Run HTTP tests
        print(f"\n--- Step 3: Validate OTA HTTP Endpoints on {ip}:{port} ---")
        base_url = f"http://{ip}:{port}"
        tests_passed = 0
        tests_failed = 0

        # Build Basic Auth header
        auth_value = base64.b64encode(f"admin:{ota_password}".encode()).decode()
        auth_headers = {"Authorization": f"Basic {auth_value}"}
        bad_auth_value = base64.b64encode(b"admin:wrongpassword").decode()
        bad_auth_headers = {"Authorization": f"Basic {bad_auth_value}"}

        # Test 1: GET / with valid auth -> 200 + HTML
        print("\n  Test 1: GET / (valid auth)")
        try:
            r = httpx.get(
                f"{base_url}/",
                headers=auth_headers,
                timeout=10.0,
                follow_redirects=True,
            )
            if r.status_code == 200:
                print(
                    f"    {Fore.GREEN}PASS{Style.RESET_ALL} - status={r.status_code}, content_length={len(r.content)}"
                )
                tests_passed += 1
            else:
                print(
                    f"    {Fore.RED}FAIL{Style.RESET_ALL} - status={r.status_code} (expected 200)"
                )
                tests_failed += 1
        except httpx.HTTPError as e:
            print(f"    {Fore.RED}FAIL{Style.RESET_ALL} - {e}")
            tests_failed += 1

        # Test 2: GET / without auth -> 401
        print("\n  Test 2: GET / (no auth)")
        try:
            r = httpx.get(f"{base_url}/", timeout=10.0, follow_redirects=True)
            if r.status_code == 401:
                print(
                    f"    {Fore.GREEN}PASS{Style.RESET_ALL} - status={r.status_code} (correctly rejected)"
                )
                tests_passed += 1
            else:
                print(
                    f"    {Fore.RED}FAIL{Style.RESET_ALL} - status={r.status_code} (expected 401)"
                )
                tests_failed += 1
        except httpx.HTTPError as e:
            print(f"    {Fore.RED}FAIL{Style.RESET_ALL} - {e}")
            tests_failed += 1

        # Test 3: GET / with wrong password -> 401
        print("\n  Test 3: GET / (wrong password)")
        try:
            r = httpx.get(
                f"{base_url}/",
                headers=bad_auth_headers,
                timeout=10.0,
                follow_redirects=True,
            )
            if r.status_code == 401:
                print(
                    f"    {Fore.GREEN}PASS{Style.RESET_ALL} - status={r.status_code} (correctly rejected)"
                )
                tests_passed += 1
            else:
                print(
                    f"    {Fore.RED}FAIL{Style.RESET_ALL} - status={r.status_code} (expected 401)"
                )
                tests_failed += 1
        except httpx.HTTPError as e:
            print(f"    {Fore.RED}FAIL{Style.RESET_ALL} - {e}")
            tests_failed += 1

        # Test 4: POST /update with auth, no body -> error response (endpoint exists)
        print("\n  Test 4: POST /update (auth, no body)")
        try:
            r = httpx.post(f"{base_url}/update", headers=auth_headers, timeout=10.0)
            # We expect an error response (400 or 500) because no firmware data was sent,
            # but the key check is that the endpoint exists (not 404)
            if r.status_code != 404:
                print(
                    f"    {Fore.GREEN}PASS{Style.RESET_ALL} - status={r.status_code} (endpoint exists)"
                )
                tests_passed += 1
            else:
                print(
                    f"    {Fore.RED}FAIL{Style.RESET_ALL} - status=404 (endpoint not found)"
                )
                tests_failed += 1
        except httpx.HTTPError as e:
            print(f"    {Fore.RED}FAIL{Style.RESET_ALL} - {e}")
            tests_failed += 1

        # Test 5: POST /update with auth, invalid firmware (4 zero bytes) -> error
        print("\n  Test 5: POST /update (auth, invalid firmware data)")
        try:
            r = httpx.post(
                f"{base_url}/update",
                headers=auth_headers,
                content=b"\x00\x00\x00\x00",
                timeout=10.0,
            )
            # We expect an error response (bad firmware header), but not 404
            if r.status_code != 404:
                print(
                    f"    {Fore.GREEN}PASS{Style.RESET_ALL} - status={r.status_code} (handled invalid firmware)"
                )
                tests_passed += 1
            else:
                print(
                    f"    {Fore.RED}FAIL{Style.RESET_ALL} - status=404 (endpoint not found)"
                )
                tests_failed += 1
        except httpx.HTTPError as e:
            print(f"    {Fore.RED}FAIL{Style.RESET_ALL} - {e}")
            tests_failed += 1

        # Summary
        total = tests_passed + tests_failed
        print()
        print("=" * 60)
        if tests_failed == 0:
            print(
                f"{Fore.GREEN}OTA VALIDATION PASSED ({tests_passed}/{total} tests){Style.RESET_ALL}"
            )
            return 0
        else:
            print(
                f"{Fore.RED}OTA VALIDATION FAILED ({tests_passed}/{total} passed, {tests_failed} failed){Style.RESET_ALL}"
            )
            return 1

    except KeyboardInterrupt:
        print("\n\n  Interrupted by user")
        handle_keyboard_interrupt_properly()
        return 130
    except RpcTimeoutError:
        print(f"\n  {Fore.RED}Timeout waiting for OTA response{Style.RESET_ALL}")
        return 1
    except Exception as e:
        print(f"\n  {Fore.RED}OTA validation error: {e}{Style.RESET_ALL}")
        return 1
    finally:
        # Cleanup: stop OTA on device
        if client:
            try:
                await client.send("stopOta", timeout=10.0)
            except KeyboardInterrupt:
                handle_keyboard_interrupt_properly()
            except Exception:
                pass
            await client.close()
        # Restore original WiFi
        wifi.restore(original_ssid)
