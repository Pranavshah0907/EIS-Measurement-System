# Diagnostic Tools

Command-line tools for checking and debugging the CAN hardware connection.
Run these from the **repo root** after setup.

## Prerequisites

- Python virtual environment set up (run `setup.bat` first, or create manually)
- Vector VN1640A connected via USB
- Vector XL Driver Library installed
- Vector Hardware Config: app **"EIS-Measurement-System"** with CAN1 = CH1 at 125 kbps

## Tools

### `can_check.py` — Quick Hardware Check

Verifies VN1640A connection, CAN bus wiring, and MCU firmware heartbeat.

```bash
# Activate the virtual environment first
pc\.venv\Scripts\activate

# Run the check
python tools/can_check.py
```

**Output:** PASS/FAIL for each check with troubleshooting tips on failure.

### `can_monitor.py` — Live CAN Traffic Viewer

Shows all CAN frames in real-time with EIS protocol decoding (command names,
impedance values, status codes).

```bash
# Activate the virtual environment first
pc\.venv\Scripts\activate

# Monitor CH1 (default)
python tools/can_monitor.py

# Monitor CH2
python tools/can_monitor.py --channel 1

# Monitor both channels
python tools/can_monitor.py --both
```

**Press Ctrl+C to stop.**

### Example Output

```
[CH1] Monitoring started. Press Ctrl+C to stop.

[CH1] [12.345] ID=0x201  DLC=1  [00]  STAT (MCU -> PC)  -> READY
[CH1] [13.401] ID=0x100  DLC=5  [01 00 00 80 3F]  CMD (PC -> MCU)  -> SetStartHz = 1.00
[CH1] [13.456] ID=0x100  DLC=5  [02 00 00 7A 44]  CMD (PC -> MCU)  -> SetStopHz = 1000.00
[CH1] [13.511] ID=0x100  DLC=3  [03 14 00]  CMD (PC -> MCU)  -> SetPoints = 20
[CH1] [13.566] ID=0x100  DLC=1  [10]  CMD (PC -> MCU)  -> START
[CH1] [13.801] ID=0x201  DLC=1  [01]  STAT (MCU -> PC)  -> STARTED
[CH1] [55.123] ID=0x200  DLC=8  [AB 47 37 42 CD CC 4C BE]  DATA (MCU -> PC)  -> Re=45.8199  Im=-0.2000
```
