# MCP2515 CAN Integration Project

## End Goal
Send a message from a laptop through a CAN bus to an embedded MCU and verify it arrived.

```
PC (python-can)
    → Vector VN1640A (USB CAN interface)
        → CAN bus (125 kbps, twisted pair)
            → Joy-IT SBC-CAN01 (MCP2515 CAN controller + TJA1050 transceiver)
                → SPI (ADICUP3029 as SPI master, MCP2515 as SPI slave)
                    → ADICUP3029 (ADuCM3029 ARM Cortex-M3)
                        → UART on COM9 → PC terminal
                            "Received CAN: ID=0x100 data=[0x01, 0x02, 0x03]"
```

**Key fact:** ADuCM3029 has NO built-in CAN controller. It controls the MCP2515 purely via SPI.
The MCP2515 IS the CAN controller. ADICUP3029 is only an SPI master.

---

## Hardware
| Component | Details |
|-----------|---------|
| PC | Windows 11 |
| Vector CAN interface | **VN1640A** (Serial 539613, Driver 26.10, USB) — CH1 and CH2, TJA1057G, High speed |
| CAN controller board | **Joy-IT SBC-CAN01** — MCP2515 + TJA1050, **16 MHz crystal** (confirmed "SCTF 16.000"), P1 termination jumper = **ON** (120Ω active) |
| Target MCU board | **EVAL-ADICUP3029** — ADuCM3029 ARM Cortex-M3, 3.3V logic |
| IDE | **Keil MDK v5**, ARM Compiler V5.06 update 7, ADuCM302x_DFP pack installed |
| PC software | python-can 4.6.1 (primary) + CANalyzer/CANoe (licensed, secondary) |
| UART debug | COM9 (mbed CDC serial port, auto-detected from ADICUP3029 USB) |
| DAPLINK | Drive D: — drag-and-drop .hex flashing |

---

## Voltage — NO Level Shifter Needed
SBC-CAN01 has two separate power inputs by design:
- **VCC** → ADICUP3029 **3.3V** pin → powers MCP2515 logic at 3.3V ✓
- **VCC1** → ADICUP3029 **5V (AVDD_5V)** pin → powers TJA1050 CAN transceiver ✓
- All SPI signals operate at 3.3V — directly compatible with ADuCM3029

---

## Key Technical Parameters
| Parameter | Value |
|-----------|-------|
| CAN baud rate | 125 kbps |
| MCP2515 crystal | 16 MHz |
| CNF1 / CNF2 / CNF3 (125k @ 16MHz) | 0x03 / 0xB5 / 0x01 |
| SPI mode | 0,0 (CPOL=0, CPHA=0) |
| SPI max clock | 10 MHz |
| ADuCM3029 SPI0 CLK | GPIO0 (Arduino D13) |
| ADuCM3029 SPI0 MOSI | GPIO1 (Arduino D11) |
| ADuCM3029 SPI0 MISO | GPIO2 (Arduino D12) |
| ADuCM3029 SPI0 CS0 | GPIO3 (Arduino D10) |
| MCP2515 INT pin (board label: NT) | GPIO13 (Arduino D9) — not used until Sprint 5 |
| ADuCM3029 UART0 TX | GPIO10 (P0_10) |
| ADuCM3029 UART0 RX | GPIO11 (P0_11) |
| UART baud rate | 115200 |

**LED GPIO mapping:** Unknown — ADICUP3029 schematic is password-protected. Not required for current sprints. Will confirm via UART debug output when needed.

---

## CAN Protocol v1.0

**Commands (PC → MCU, CAN ID 0x100):**
| CMD byte | Name | Payload | Description |
|----------|------|---------|-------------|
| 0x01 | SetStartHz | bytes 1-4 = float32 LE (Hz) | Sweep start frequency |
| 0x02 | SetStopHz | bytes 1-4 = float32 LE (Hz) | Sweep stop frequency |
| 0x03 | SetPoints | bytes 1-2 = uint16 LE | Number of sweep points |
| 0x04 | SetACVoltPP | bytes 1-4 = float32 LE (mV pp) | AC excitation amplitude |
| 0x05 | SetDCVolt | bytes 1-4 = float32 LE (mV) | DC bias voltage |
| 0x10 | START | (no payload) | Start EIS sweep |
| 0x11 | STOP | (no payload) | Stop sweep (placeholder) |

**Data (MCU → PC):**
| CAN ID | Bytes | Meaning |
|--------|-------|---------|
| 0x200 | bytes 0-3 = float32 Re, bytes 4-7 = float32 Im | Impedance data point |
| 0x201 | byte 0 = status (0x00=READY, 0x01=STARTED, 0x02=COMPLETE) | Status/heartbeat |

---

## Wiring

### CAN Bus: Vector VN1640A CH1 (DB9) → SBC-CAN01 (screw terminal)
```
VN1640A CH1 DB9 male          SBC-CAN01 terminal
  Pin 7 (CANH)  ──────────────  CANH
  Pin 2 (CANL)  ──────────────  CANL
```
- Cable: male DB9 → twisted pair (50cm) → bare wire ends into screw terminal
- 120Ω termination: DB9 CAN 120Ω adapter at Vector end + P1=ON at SBC-CAN01 end ✓
- Cable is already made and ready

### SPI: SBC-CAN01 → ADICUP3029 Arduino header (direct, no level shifter)
| SBC-CAN01 | Signal | ADICUP3029 |
|-----------|--------|------------|
| VCC | 3.3V (MCP2515 logic) | 3.3V pin |
| VCC1 | 5V (TJA1050 transceiver) | 5V (AVDD_5V) |
| GND | Ground | GND |
| CS | SPI Chip Select | D10 (GPIO3) |
| SO | MISO | D12 (GPIO2) |
| SI | MOSI | D11 (GPIO1) |
| SCK | SPI Clock | D13 (GPIO0) |
| NT | INT (active low, future use) | D9 (GPIO13) |

---

## Sprint Plan

### ✅ Sprint 0 — Architecture & Planning (DONE)
Hardware identified, wiring designed, CAN protocol defined, project files created.

### ✅ Sprint 1 — PC CAN Layer Validated (DONE)
- python-can 4.6.1 installed
- Vector app "MCP_Integration" registered: CAN1=CH1(index 0), CAN2=CH2(index 1)
- Hardware loopback test PASSED: CH1 TX → CH2 RX at 125kbps, ID=0x100
- Key learning: 120Ω termination required — CAN ACK errors without it
- DB9 120Ω termination adapter (female-female) available

### 🔲 Sprint 2 — ADICUP3029 Firmware: UART + SPI → MCP2515 Register Read
**Goal:** Prove ADICUP firmware runs AND SPI wiring to MCP2515 is correct.
**No CAN bus cable needed yet.**

Steps:
1. Keil project setup (Device::Startup + CMSIS::Core + retarget.c for no-semihosting)
2. UART Hello World → COM9 → confirm firmware is running
3. Wire SBC-CAN01 to ADICUP3029 (SPI + power)
4. SPI init → send MCP2515 Reset command (0xC0)
5. Read MCP2515 CANSTAT register → expect 0x80 (Configuration mode)
6. UART prints: `"MCP2515 CANSTAT = 0x80 — SPI OK"`

**Deliverable:** PC terminal on COM9 shows correct MCP2515 register value.

### 🔲 Sprint 3 — MCP2515 Full Driver + Internal Loopback
**Goal:** Full MCP2515 driver working. No CAN bus cable needed.

Steps:
1. Implement MCP2515 init (CNF1/2/3 for 125kbps, set Normal/Loopback mode)
2. Set MCP2515 to **Loopback mode** (internal — TX routes back to RX inside chip)
3. Write test frame to TXB0 via SPI → trigger transmit
4. Read received frame from RXB0 via SPI
5. UART prints: `"Loopback OK: ID=0x100 data=[01 02 03]"`

**Deliverable:** UART confirms MCP2515 driver sends and receives correctly. Proves SPI driver + MCP2515 driver are solid before adding real CAN bus.

### 🔲 Sprint 4 — Full End-to-End Pipeline (THE MAIN GOAL)
**Goal:** PC sends CAN frame → arrives at ADICUP3029 → UART confirms it.

Steps:
1. Switch MCP2515 from Loopback → Normal mode
2. Connect CAN bus cable: VN1640A CH1 → SBC-CAN01 CANH/CANL
3. PC runs `python pc/send_test_message.py` → sends ID=0x100 data=[01,02,03]
4. MCP2515 receives frame from CAN bus
5. ADICUP reads frame from MCP2515 RXB0 via SPI
6. UART prints: `"Received CAN: ID=0x100 data=[01 02 03]"`
7. ADICUP sends ack frame (ID=0x200) back via MCP2515 → PC receives on CH1

**Deliverable:** COM9 terminal shows received CAN frame. python-can on PC receives 0x200 ack. Full pipeline proven.

### 🔲 Sprint 5 — Extensions (Future)
- Interrupt-driven reception (MCP2515 NT pin → GPIO13)
- LED control (confirm GPIO mapping via UART first)
- DBC file for CANalyzer
- Bidirectional complex messaging

---

## Keil Project Notes
- ARM Compiler: **V5.06 update 7** (NOT v6 — breaks ADuCM3029 DFP)
- Always include `retarget.c` (suppresses semihosting linker error)
- Flash: drag `.hex` from `Objects\` folder to **D: (DAPLINK)**
- UART appears on **COM9** (mbed CDC, no driver install needed)

---

## Development Workflow (Sprint 5A+)

### Build + Flash (Claude runs this)
```bash
python firmware/build_flash.py
```
- Calls UV4.exe (`C:\Keil_v5\UV4\UV4.exe`) to build `mcp_integration.uvprojx`
- Copies `.hex` to DAPLINK drive D: automatically
- UV4 exit codes: 0=ok, 1=warnings OK, 2+=error

### UART Monitor (you run this in your terminal, leave it running)
```bash
python firmware/build_flash.py --monitor
```
- Connects to COM10 at 115200 baud via pyserial
- Auto-reconnects after every flash/reset — no manual action needed
- Ctrl+C to stop
- Requires pyserial: `pip install pyserial` (use system Python, not .venv)

### EIS Plot (say "EIS plot: [paste data]" to Claude)
```bash
python pc/eis_plot.py [data_file.txt]   # or paste via stdin
```
- Generates `pc/results/Sweep_1Hz_1kHz_40pts_TIMESTAMP/`
  - `nyquist.png` — static plot
  - `report.html` — self-contained HTML report (open in browser)
  - `data.json`   — structured data + metadata
  - `raw_data.txt`— original UART output
- Update `SETUP["notes"]` in `eis_plot.py` before each test to describe DUT/conditions

### Current Firmware Settings (main.c `bat_struct_init`)
| Parameter | Value | Note |
|-----------|-------|------|
| DftNum | DFTNUM_1024 | Compromise: ~3.3s/pt at 1Hz. Use 4096–8192 for production accuracy |
| SweepStart | 1.0 Hz | |
| SweepStop | 1000.0 Hz | |
| SweepPoints | 40 | |
| SweepLog | bTRUE | |
| ACVoltPP | 300 mV | |
| DCVolt | 1200 mV | |
| RcalVal | 50 mOhm | |
| SPI0 DIV | 2 | ~4.3 MHz to AD5941 |

---

## PC Files
```
pc/
├── requirements.txt          — python-can, matplotlib, numpy
├── sprint1_loopback_test.py  — DONE: CH1→CH2 loopback ✓
├── can_monitor.py            — monitor all CAN traffic on CH1
├── send_test_message.py      — Sprint 4: send ID=0x100 test frame
├── eis_plot.py               — EIS Nyquist plotter + HTML report generator
└── results/                  — per-test output folders (Sweep_1Hz_1kHz_40pts_...)
```

## Firmware Files
```
firmware/
├── build_flash.py            — build + flash + UART monitor automation
└── mcp_integration/
    ├── mcp_integration.uvprojx   — Keil project
    └── src/
        ├── main.c                — main loop (Sprint 5A: EIS sweep)
        ├── retarget.c            — semihosting stub (always needed)
        ├── uart_hal.h/c          — UART0 driver
        ├── spi_hal.h/c           — SPI1 driver (MCP2515)
        ├── mcp2515.h/c           — MCP2515 CAN controller driver
        ├── ad5940.h/c            — AD5941 AFE driver (ADI)
        ├── BATImpedance.h/c      — EIS sweep application (ADI, modified)
        └── ADICUP3029Port.c      — AD5941 platform port (SPI0, GPIO, INT)
```

---

## Resources
- MCP2515 datasheet: `Data/MCP2515/MCP2515-Stand-Alone-CAN-Controller-with-SPI-20001801J.pdf`
- SBC-CAN01 datasheet: `Data/MCP2515/SBC-CAN01-Datasheet.pdf`
- ADICUP3029 docs: `Data/EVAL-ADICUP3029 Design Support Package/`
- ADI BSP GitHub: https://github.com/analogdevicesinc/EVAL-ADICUP3029
- MCP2515 Arduino lib (reference for SPI commands): https://github.com/autowp/arduino-mcp2515
- Vector XL Driver: https://www.vector.com/int/en/products/products-a-z/libraries-drivers/xl-driver-library/
