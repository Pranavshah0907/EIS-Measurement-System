"""
CAN Bus Monitor — EIS Protocol
===============================
Listens on VN1640A and prints all received CAN frames with EIS protocol decoding.
Useful for debugging CAN communication between PC and ADICUP3029.

Usage:
    python tools/can_monitor.py              # monitor CH1 (default)
    python tools/can_monitor.py --channel 1  # monitor CH2
    python tools/can_monitor.py --both       # monitor both channels

Prerequisites:
    - Vector XL Driver Library installed
    - Vector Hardware Config: app "MCP_Integration" with CAN1 = CH1
    - VN1640A connected via USB
"""

import can
import time
import argparse
import threading

APP_NAME = "MCP_Integration"
BITRATE  = 125000

# Known CAN IDs in our EIS protocol
KNOWN_IDS = {
    0x100: "CMD  (PC -> MCU)",
    0x200: "DATA (MCU -> PC)",
    0x201: "STAT (MCU -> PC)",
}

# Command byte names (first byte of 0x100 frames)
CMD_NAMES = {
    0x01: "SetStartHz",
    0x02: "SetStopHz",
    0x03: "SetPoints",
    0x04: "SetACVoltPP",
    0x05: "SetDCVolt",
    0x10: "START",
    0x11: "STOP",
}

# Status byte names (first byte of 0x201 frames)
STAT_NAMES = {
    0x00: "READY",
    0x01: "STARTED",
    0x02: "COMPLETE",
}


def decode_frame(msg: can.Message) -> str:
    """Decode a CAN frame into a human-readable string."""
    label = KNOWN_IDS.get(msg.arbitration_id, "UNKNOWN")
    data_hex = " ".join(f"{b:02X}" for b in msg.data)
    ts = f"{msg.timestamp:.3f}"

    extra = ""
    # Command frame (PC -> MCU)
    if msg.arbitration_id == 0x100 and len(msg.data) >= 1:
        cmd = msg.data[0]
        name = CMD_NAMES.get(cmd, f"0x{cmd:02X}")
        if cmd in (0x01, 0x02, 0x04, 0x05) and len(msg.data) >= 5:
            import struct
            val = struct.unpack("<f", bytes(msg.data[1:5]))[0]
            extra = f"  -> {name} = {val:.2f}"
        elif cmd == 0x03 and len(msg.data) >= 3:
            import struct
            val = struct.unpack("<H", bytes(msg.data[1:3]))[0]
            extra = f"  -> {name} = {val}"
        else:
            extra = f"  -> {name}"

    # Impedance data frame (MCU -> PC)
    elif msg.arbitration_id == 0x200 and len(msg.data) == 8:
        import struct
        re_val, im_val = struct.unpack("<ff", bytes(msg.data[:8]))
        extra = f"  -> Re={re_val:.4f}  Im={im_val:.4f}"

    # Status frame (MCU -> PC)
    elif msg.arbitration_id == 0x201 and len(msg.data) >= 1:
        stat = msg.data[0]
        name = STAT_NAMES.get(stat, f"0x{stat:02X}")
        extra = f"  -> {name}"

    return f"[{ts}] ID=0x{msg.arbitration_id:03X}  DLC={msg.dlc}  [{data_hex}]  {label}{extra}"


def monitor_channel(channel: int, label: str, stop_event: threading.Event):
    try:
        bus = can.Bus(
            interface='vector',
            channel=channel,
            bitrate=BITRATE,
            app_name=APP_NAME,
            rx_queue_size=64
        )
    except Exception as e:
        print(f"[{label}] ERROR opening channel: {e}")
        return

    print(f"[{label}] Monitoring started. Press Ctrl+C to stop.\n")
    try:
        while not stop_event.is_set():
            msg = bus.recv(timeout=0.1)
            if msg is not None:
                print(f"[{label}] {decode_frame(msg)}")
    except KeyboardInterrupt:
        pass
    finally:
        bus.shutdown()
        print(f"\n[{label}] Monitor stopped.")


def main():
    parser = argparse.ArgumentParser(description="CAN Bus Monitor for MCP Integration project")
    parser.add_argument('--channel', type=int, default=0, help='Channel index to monitor (0=CH1, 1=CH2)')
    parser.add_argument('--both', action='store_true', help='Monitor both CH1 and CH2 simultaneously')
    args = parser.parse_args()

    stop_event = threading.Event()

    if args.both:
        print("Monitoring CH1 (index 0) and CH2 (index 1)...")
        t1 = threading.Thread(target=monitor_channel, args=(0, "CH1", stop_event), daemon=True)
        t2 = threading.Thread(target=monitor_channel, args=(1, "CH2", stop_event), daemon=True)
        t1.start()
        t2.start()
        try:
            while t1.is_alive() or t2.is_alive():
                time.sleep(0.1)
        except KeyboardInterrupt:
            stop_event.set()
    else:
        monitor_channel(args.channel, f"CH{args.channel + 1}", stop_event)


if __name__ == "__main__":
    main()
