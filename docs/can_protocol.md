# CAN Protocol Specification — EIS Measurement

## Bus Parameters

| Parameter | Value |
|-----------|-------|
| Standard | CAN 2.0B |
| Baud rate | 125 kbps |
| Frame type | Standard (11-bit ID) |
| MCP2515 crystal | 16 MHz |
| CNF1 / CNF2 / CNF3 | 0x03 / 0xB5 / 0x01 |

---

## Commands (PC -> MCU) — CAN ID 0x100

| Byte 0 (CMD) | Name | Payload (bytes 1+) | Description |
|---------------|------|---------------------|-------------|
| 0x01 | SetStartHz | bytes 1-4: float32 LE (Hz) | Sweep start frequency |
| 0x02 | SetStopHz | bytes 1-4: float32 LE (Hz) | Sweep stop frequency |
| 0x03 | SetPoints | bytes 1-2: uint16 LE | Number of sweep points |
| 0x04 | SetACVoltPP | bytes 1-4: float32 LE (mV pp) | AC excitation amplitude |
| 0x05 | SetDCVolt | bytes 1-4: float32 LE (mV) | DC bias voltage |
| 0x10 | START | (no payload) | Start EIS sweep |
| 0x11 | STOP | (no payload) | Stop sweep |

### Example: Configure and start a sweep

```
SetStartHz:  ID=0x100  data=[0x01, 0x00, 0x00, 0x80, 0x3F]   -> 1.0 Hz
SetStopHz:   ID=0x100  data=[0x02, 0x00, 0x00, 0x7A, 0x44]   -> 1000.0 Hz
SetPoints:   ID=0x100  data=[0x03, 0x14, 0x00]                -> 20 points
SetACVoltPP: ID=0x100  data=[0x04, 0x00, 0x00, 0x96, 0x43]   -> 300.0 mV
SetDCVolt:   ID=0x100  data=[0x05, 0x00, 0x00, 0x96, 0x44]   -> 1200.0 mV
START:       ID=0x100  data=[0x10]
```

---

## Impedance Data (MCU -> PC) — CAN ID 0x200

| Bytes | Format | Description |
|-------|--------|-------------|
| 0-3 | float32 LE | Real part of impedance (ohms) |
| 4-7 | float32 LE | Imaginary part of impedance (ohms) |

One frame per frequency point. Points arrive in order from lowest to highest frequency.

---

## Status (MCU -> PC) — CAN ID 0x201

| Byte 0 | Name | Description |
|--------|------|-------------|
| 0x00 | READY | Firmware idle, heartbeat (~1/s) |
| 0x01 | STARTED | Sweep has begun (sent after RCAL calibration) |
| 0x02 | COMPLETE | All points measured, sweep done |

---

## Typical Sweep Sequence

```
PC  --[0x100 SetStartHz 1.0]-->  MCU
PC  --[0x100 SetStopHz 1000.0]-->  MCU
PC  --[0x100 SetPoints 20]-->  MCU
PC  --[0x100 SetACVoltPP 300.0]-->  MCU
PC  --[0x100 SetDCVolt 1200.0]-->  MCU
PC  --[0x100 START]-->  MCU

    ... RCAL calibration (~10-40s) ...

MCU --[0x201 STARTED]-->  PC

MCU --[0x200 Re=45.82 Im=-0.20]-->  PC   (point 1)
MCU --[0x200 Re=44.31 Im=-1.05]-->  PC   (point 2)
    ...
MCU --[0x200 Re=12.10 Im=-8.44]-->  PC   (point 20)

MCU --[0x201 COMPLETE]-->  PC
```
