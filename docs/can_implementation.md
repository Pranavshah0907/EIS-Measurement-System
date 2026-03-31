# CAN Bus Implementation Guide

How CAN communication works in this EIS measurement system — from physical layer to application protocol.

---

## 1. Physical Layer

```
PC (python-can)                                         Embedded MCU
     |                                                       |
 Vector VN1640A                                     EVAL-ADICUP3029
 (USB CAN interface)                                  (ARM Cortex-M3)
     |                                                       |
  DB9 connector                                    SPI1 (P8 PMOD header)
     |                                                       |
  Pin 7 ── CANH ──────── twisted pair ──────── CANH    Joy-IT SBC-CAN01
  Pin 2 ── CANL ──────── (~50 cm cable) ────── CANL    MCP2515 + TJA1050
     |                                                       |
  120 ohm                                              120 ohm (P1=ON)
  termination                                          termination
```

| Parameter | Value |
|-----------|-------|
| Baud rate | 125 kbps |
| Bus standard | CAN 2.0B, Standard frames (11-bit ID) |
| Termination | 120 ohm at both ends (required for ACK) |
| Transceiver | TJA1050 on SBC-CAN01, TJA1057G on VN1640A |
| Logic voltage | 3.3V (MCP2515 powered from ADICUP3029 3.3V) |
| Transceiver voltage | 5V (TJA1050 powered from ADICUP3029 AVDD_5V) |
| Cable | Male DB9 to bare wire into screw terminal |

**No level shifter needed.** The SBC-CAN01 has separate VCC (3.3V for MCP2515 logic) and VCC1 (5V for TJA1050 transceiver). All SPI signals run at 3.3V.

---

## 2. CAN Frame Structure

Every frame on the bus uses **standard CAN** (11-bit arbitration ID, no extended frames):

```
┌─────┬───────────────┬─────┬─────┬───────────────────────────────────┬─────┬─────┬─────┐
│ SOF │ Arbitration ID│ RTR │ IDE │         Data (0-8 bytes)          │ CRC │ ACK │ EOF │
│ 1b  │    11 bits    │ 1b  │ 1b  │      DLC × 8 bits                │ 15b │ 2b  │ 7b  │
└─────┴───────────────┴─────┴─────┴───────────────────────────────────┴─────┴─────┴─────┘
```

**Frame overhead:** 47 bits (SOF + ID + control + CRC + ACK + EOF + interframe spacing)
**Maximum data:** 8 bytes = 64 bits
**Total for 8-byte frame:** 47 + 64 = 111 bits + bit stuffing (~6 extra) ≈ 117 bits

**Transmission time for one 8-byte frame at 125 kbps:**
117 bits / 125,000 bits/s = **~0.94 ms per frame**

---

## 3. Bit Timing (125 kbps from 16 MHz crystal)

The MCP2515 has a 16 MHz crystal. CAN bit timing is configured via three registers:

| Register | Value | Binary | Meaning |
|----------|-------|--------|---------|
| CNF1 | 0x03 | 00000011 | BRP=3 (prescaler), SJW=1 TQ |
| CNF2 | 0xB5 | 10110101 | PS1=7 TQ, Propagation=6 TQ, single sample |
| CNF3 | 0x01 | 00000001 | PS2=2 TQ |

**Derived timing:**

```
Time Quantum (TQ) = 2 x (BRP + 1) / 16 MHz = 2 x 4 / 16 MHz = 0.5 us

One CAN bit = Sync(1) + Prop(6) + PS1(7) + PS2(2) = 16 TQ = 8.0 us

Bit rate = 1 / 8.0 us = 125,000 bits/s = 125 kbps  ✓

Sample point = (1 + 6 + 7) / 16 = 87.5%
```

> **Critical:** CNF1 must be 0x03 (BRP=3). Using 0x07 (BRP=7) gives 62.5 kbps — loopback test passes but real bus communication fails because the PC side is at 125 kbps.

---

## 4. MCP2515 SPI Interface

The ADICUP3029 has **no built-in CAN controller**. It talks to the MCP2515 via SPI1 on the P8 PMOD header:

| ADICUP3029 | GPIO | SBC-CAN01 | Function |
|------------|------|-----------|----------|
| P8 PMOD | P1.6 | SCK | SPI Clock |
| P8 PMOD | P1.7 | SO (MISO) | Data from MCP2515 |
| P8 PMOD | P1.8 | SI (MOSI) | Data to MCP2515 |
| P8 PMOD | P1.9 | CS | Chip Select (active low) |

**SPI mode:** 0 (CPOL=0, CPHA=0)

**SPI commands used to control the MCP2515:**

| Command | Byte | Usage |
|---------|------|-------|
| RESET | 0xC0 | Reset chip, enter Config mode |
| READ | 0x03 + addr | Read register value |
| WRITE | 0x02 + addr + data | Write register value |
| BIT MODIFY | 0x05 + addr + mask + data | Change specific bits |
| RTS TXB0 | 0x81 | Trigger transmission of TX buffer 0 |
| READ RX0 | 0x90 | Read RX buffer 0 (auto-clears interrupt) |

---

## 5. MCP2515 Initialization Sequence

On power-up, the firmware runs this sequence:

```
1. RESET command (0xC0)
   └─ MCP2515 enters Config mode (CANSTAT = 0x80)
   └─ Wait 2 ms for PLL to stabilize

2. Verify SPI wiring
   └─ Read CANSTAT register
   └─ Expect 0x80 (Config mode)
   └─ If 0xFF → MISO not connected
   └─ If 0x00 → CS or MOSI issue

3. Set baud rate registers
   └─ CNF1 = 0x03, CNF2 = 0xB5, CNF3 = 0x01

4. Configure RX buffer
   └─ RXB0CTRL = 0x60 (accept all frames, no filtering)

5. Switch to Normal mode
   └─ Write CANCTRL = 0x00 (request Normal mode)
   └─ Wait 1 ms
   └─ Read CANSTAT, verify bits [7:5] = 0x00
```

---

## 6. Application Protocol

Three CAN IDs are used:

| CAN ID | Direction | Name | Payload | Purpose |
|--------|-----------|------|---------|---------|
| 0x100 | PC → MCU | Command | 1-5 bytes | Configure sweep + trigger |
| 0x200 | MCU → PC | Data | 8 bytes | Impedance measurement point |
| 0x201 | MCU → PC | Status | 1 byte | Heartbeat + sweep state |

### Commands (ID 0x100)

Byte 0 is the command code. Remaining bytes are the payload:

| Byte 0 | Command | Payload (bytes 1-4) | Example |
|--------|---------|---------------------|---------|
| 0x01 | SetStartHz | float32 LE (Hz) | `[01 00 00 80 3F]` = 1.0 Hz |
| 0x02 | SetStopHz | float32 LE (Hz) | `[02 00 00 7A 44]` = 1000.0 Hz |
| 0x03 | SetPoints | uint16 LE (count) | `[03 28 00]` = 40 points |
| 0x04 | SetACVoltPP | float32 LE (mV) | `[04 00 00 96 43]` = 300.0 mV |
| 0x05 | SetDCVolt | float32 LE (mV) | `[05 00 00 96 44]` = 1200.0 mV |
| 0x10 | START | none | `[10]` = begin sweep |
| 0x11 | STOP | none | `[11]` = abort (placeholder) |

> All multi-byte values use **little-endian** byte order (IEEE 754 for floats).

### Impedance Data (ID 0x200)

Each frame carries one complex impedance measurement:

```
Byte:  0    1    2    3    4    5    6    7
     [───── Re (float32 LE) ─────][───── Im (float32 LE) ─────]
            Real part (ohms)             Imaginary part (ohms)
```

### Status (ID 0x201)

Single byte indicating firmware state:

| Byte 0 | Meaning | When sent |
|--------|---------|-----------|
| 0x00 | READY | Heartbeat, ~1/second while idle |
| 0x01 | STARTED | After RCAL calibration completes, sweep begins |
| 0x02 | COMPLETE | All impedance points sent, sweep finished |

---

## 7. Complete Sweep Sequence

Here is the full signal flow when the user clicks "Start Sweep":

```
 Time     Direction    ID      Data                    What happens
 ─────    ─────────    ──────  ──────────────────────  ──────────────────────────
  0 ms    PC → MCU    0x100   [01 00 00 80 3F]        SetStartHz = 1.0 Hz
 50 ms    PC → MCU    0x100   [02 00 00 7A 44]        SetStopHz = 1000.0 Hz
100 ms    PC → MCU    0x100   [03 28 00]              SetPoints = 40
150 ms    PC → MCU    0x100   [04 00 00 96 43]        SetACVoltPP = 300 mV
200 ms    PC → MCU    0x100   [05 00 00 96 44]        SetDCVolt = 1200 mV
250 ms    PC → MCU    0x100   [10]                    START — trigger sweep

           MCU reinitializes AD5941 sequencer with new config
           MCU runs RCAL calibration (~10-40 seconds)

 ~40 s    MCU → PC    0x201   [01]                    STARTED — RCAL done
                                                       AD5941 begins measuring

 ~43 s    MCU → PC    0x200   [Re₁ Re₁ Re₁ Re₁       Point 1: impedance at 1.0 Hz
                               Im₁ Im₁ Im₁ Im₁]       (~3.3 s per point at DFT 1024)

 ~47 s    MCU → PC    0x200   [Re₂ ... Im₂]           Point 2: next frequency
                      ...
                      ...      (40 points total)
                      ...
~175 s    MCU → PC    0x200   [Re₄₀ ... Im₄₀]        Point 40: impedance at 1000 Hz

~175 s    MCU → PC    0x201   [02]                    COMPLETE — sweep finished
                                                       PC saves .eis file
                                                       Browser shows Nyquist plot
```

**Timing between config frames:** 50 ms gaps (CONFIG_GAP_S in can_worker.py)
**RCAL calibration:** 10-40 seconds depending on start frequency
**Per-point measurement:** ~3.3 seconds at 1 Hz with DFT size 1024
**Total sweep time (1 Hz - 1 kHz, 40 points):** ~3-4 minutes

---

## 8. Transmit & Receive Mechanics

### Sending a Frame (firmware side)

```
1. Wait for previous TX to complete
   └─ Poll TXB0CTRL register, check TXREQ bit
   └─ Spin until TXREQ = 0 (previous frame sent)

2. Load frame into TX buffer 0
   └─ WRITE registers TXB0SIDH through TXB0D7:
      - SIDH = (ID >> 3)           ← upper 8 bits of 11-bit ID
      - SIDL = (ID & 0x07) << 5   ← lower 3 bits + EXIDE=0
      - EID8, EID0 = 0x00         ← not used (standard frame)
      - DLC = data length
      - D0-D7 = data bytes

3. Trigger transmission
   └─ Send RTS command (0x81) via SPI
   └─ MCP2515 arbitrates on bus and transmits
   └─ Takes ~1 ms for an 8-byte frame at 125 kbps
```

### Receiving a Frame (firmware side)

```
1. Poll for received frame
   └─ Read CANINTF register
   └─ Check RX0IF bit (bit 0)
   └─ If timeout_ms = 0: single check, return immediately if empty
   └─ If timeout_ms > 0: retry every 1 ms until set or timeout

2. Read frame from RX buffer 0
   └─ Send READ RX0 command (0x90)
   └─ Read SIDH, SIDL → reconstruct 11-bit ID
   └─ Read DLC → data length
   └─ Read D0 through D[DLC-1] → data bytes
   └─ Raising CS automatically clears the RX0IF interrupt flag

3. Return frame to application
```

**IDLE state polling:** The main loop calls `mcp2515_recv_frame(&rxf, 0)` with timeout=0 (non-blocking). Each iteration takes ~10 us, giving an effective poll rate of ~100 kHz.

---

## 9. PC-Side CAN Communication

The PC uses **python-can** with the **Vector XL Driver** interface:

```python
bus = can.Bus(
    interface="vector",
    channel=0,              # VN1640A CH1
    bitrate=125_000,        # 125 kbps
    app_name="EIS-Measurement-System",
    rx_queue_size=256,
)
```

**Sending commands:**
```python
msg = can.Message(arbitration_id=0x100, data=payload, is_extended_id=False)
bus.send(msg)
```

**Receiving data:**
```python
frame = bus.recv(timeout=1.0)  # blocks up to 1 second
if frame and frame.arbitration_id == 0x200:
    re, im = struct.unpack("<ff", bytes(frame.data[:8]))
```

### Timeouts

| Timeout | Value | Purpose |
|---------|-------|---------|
| Diagnostics | 5 s | Wait for MCU heartbeat (0x201/0x00) |
| RCAL | 120 s | Wait for STARTED status (0x201/0x01) |
| Per-point | 15 s | Max gap between consecutive data frames |

If a timeout fires, the PC saves any partial data received and reports an error to the browser.

---

## 10. Heartbeat & Diagnostics

While idle (no sweep running), the MCU sends a **READY heartbeat** approximately once per second:

```
MCU main loop:
  s_idle_hb counter increments each iteration (~10 us)
  When s_idle_hb reaches 100,000 → send CAN 0x201 [0x00]
  Reset counter
```

The browser uses this heartbeat for hardware diagnostics:

```
[1/3] Vector VN1640A .... check if bus opens without error
[2/3] CAN bus ........... check if any frames arrive within 5 s
[3/3] MCU firmware ...... check if heartbeat (0x201/0x00) arrives
```

All three must pass (green) before the Start Sweep button enables.

---

## Quick Reference Card

```
Baud rate:       125 kbps
Crystal:         16 MHz (MCP2515)
Bit timing:      16 TQ/bit, sample point 87.5%
Frame type:      Standard CAN (11-bit ID)
Frame time:      ~1 ms per 8-byte frame

PC → MCU:        ID 0x100  (config + START/STOP)
MCU → PC:        ID 0x200  (impedance: Re + Im, float32 LE)
                 ID 0x201  (status: 0x00=READY, 0x01=STARTED, 0x02=COMPLETE)

Config gap:      50 ms between command frames
Heartbeat:       ~1/s while idle (0x201/0x00)
Sweep time:      ~3-4 min (40 points, 1 Hz - 1 kHz, DFT 1024)
```
