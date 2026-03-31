#!/usr/bin/env python3
"""
CAN Hardware Check — Quick pass/fail diagnostic.

Verifies:
  1. Vector VN1640A is reachable via XL Driver
  2. CAN bus is wired correctly (receives frames)
  3. MCU firmware is running (heartbeat on CAN 0x201)

Usage:
    python tools/can_check.py

Prerequisites:
    - Vector XL Driver Library installed
    - Vector Hardware Config: app "MCP_Integration" with CAN1 = CH1 at 125 kbps
    - VN1640A connected via USB

Exit codes:
    0 = all checks passed
    1 = one or more checks failed
"""

import sys
import time

try:
    import can
except ImportError:
    print("ERROR: python-can not installed.")
    print("  Run:  pip install python-can[vector]")
    sys.exit(1)

APP_NAME = "MCP_Integration"
CHANNEL  = 0        # Vector CH1 (index 0)
BITRATE  = 125_000  # 125 kbps

CAN_ID_STAT  = 0x201
STAT_READY   = 0x00
LISTEN_TIMEOUT = 5.0  # seconds


def main():
    print()
    print("=" * 55)
    print("  CAN Hardware Check")
    print("=" * 55)
    print()

    # ── Check 1: Open Vector VN1640A ──────────────────────
    print("[1/3] Opening VN1640A ...", end=" ", flush=True)
    try:
        bus = can.Bus(
            interface="vector",
            channel=CHANNEL,
            bitrate=BITRATE,
            app_name=APP_NAME,
            rx_queue_size=256,
        )
    except Exception as exc:
        print("FAIL")
        print(f"      Cannot open VN1640A: {exc}")
        print()
        print("  Troubleshooting:")
        print("    - Is the VN1640A plugged in via USB?")
        print("    - Is the Vector XL Driver Library installed?")
        print("    - In Vector Hardware Config, is app 'MCP_Integration'")
        print("      assigned with CAN1 = CH1 (index 0)?")
        print()
        sys.exit(1)

    print("OK")

    # ── Check 2+3: Listen for MCU heartbeat ───────────────
    print("[2/3] Listening for CAN traffic ...", end=" ", flush=True)
    got_any_frame = False
    got_heartbeat = False

    try:
        deadline = time.monotonic() + LISTEN_TIMEOUT
        while time.monotonic() < deadline:
            frame = bus.recv(timeout=0.5)
            if frame is None:
                continue
            if frame.is_error_frame:
                print("FAIL")
                print("      CAN error frame received — check wiring and termination.")
                print()
                sys.exit(1)

            got_any_frame = True

            if (frame.arbitration_id == CAN_ID_STAT
                    and len(frame.data) >= 1
                    and frame.data[0] == STAT_READY):
                got_heartbeat = True
                break
    finally:
        bus.shutdown()

    if not got_any_frame:
        print("FAIL")
        print("      No CAN frames received in {:.0f}s.".format(LISTEN_TIMEOUT))
        print()
        print("  Troubleshooting:")
        print("    - Is the CAN cable connected? (VN1640A CH1 DB9 -> SBC-CAN01)")
        print("    - Is 120 ohm termination present at both ends?")
        print("    - Is the ADICUP3029 powered on?")
        print()
        sys.exit(1)

    print("OK")

    print("[3/3] MCU firmware heartbeat ...", end=" ", flush=True)
    if got_heartbeat:
        print("OK")
        print()
        print("  All checks PASSED. Hardware is ready.")
        print()
        sys.exit(0)
    else:
        print("WARN")
        print("      VN1640A works and CAN frames are flowing, but no MCU")
        print("      heartbeat (0x201/0x00) was received.")
        print()
        print("  Possible causes:")
        print("    - ADICUP3029 is still booting — try again in a few seconds")
        print("    - Firmware not flashed — flash mcp_integration.hex via DAPLINK")
        print("    - Board is in a sweep — wait for it to finish")
        print()
        sys.exit(1)


if __name__ == "__main__":
    main()
