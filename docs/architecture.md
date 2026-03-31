# System Architecture

## Overview

This system performs Electrochemical Impedance Spectroscopy (EIS) measurements through a browser interface. The PC communicates with an embedded MCU over CAN bus to control an AD5941 impedance analyzer.

**Key fact:** The ADuCM3029 MCU has no built-in CAN controller. It uses the MCP2515 (an external CAN controller on the SBC-CAN01 board) purely via SPI.

## Block Diagram

```
Browser (index.html)
    |
    | SocketIO (WebSocket)
    |
Flask Server (server.py)
    |
    | Python threading
    |
CAN Worker (can_worker.py)
    |
    | python-can library
    |
Vector XL Driver
    |
    | USB
    |
Vector VN1640A (CH1)
    |
    | CAN bus (125 kbps, twisted pair)
    |
MCP2515 + TJA1050 (SBC-CAN01)
    |
    | SPI1 (P8 PMOD header)
    |
ADuCM3029 (EVAL-ADICUP3029)
    |
    | SPI0 (Arduino headers)
    |
AD5941 (EVAL-AD5941BATZ)
    |
    | Kelvin clips
    |
DUT (Device Under Test)
```

## Software Stack

### PC Side (`pc/`)

| Component | File | Role |
|-----------|------|------|
| Browser UI | `templates/index.html` | Single-page app: sweep config, Nyquist plot, measurement history |
| Web Server | `server.py` | Flask + SocketIO, serves UI, routes API calls to CAN worker |
| CAN Worker | `can_worker.py` | Background thread: sends CAN commands, receives impedance data |

### Data Flow: Start Sweep

1. User clicks **Start Sweep** in browser
2. Browser sends SocketIO `start_sweep` event with parameters
3. `server.py` validates parameters, calls `worker.start_sweep()`
4. `can_worker.py` opens VN1640A, sends config frames (0x100), then START
5. MCU receives commands via MCP2515, configures AD5941, begins sweep
6. MCU sends impedance data (0x200) and status (0x201) back via CAN
7. `can_worker.py` receives frames, emits SocketIO `data_point` events
8. Browser updates Nyquist plot in real-time
9. On COMPLETE (0x201/0x02), sweep data is saved as `.eis` JSON file

### Firmware Side (`firmware/mcp_integration/src/`)

| Component | Files | Role |
|-----------|-------|------|
| Main loop | `main.c` | Init hardware, CAN command dispatch, sweep orchestration |
| UART | `uart_hal.h/c` | Debug output to COM port (115200 baud) |
| SPI1 (MCP2515) | `spi_hal.h/c` | Low-level SPI1 driver on P8 PMOD |
| CAN controller | `mcp2515.h/c` | MCP2515 register read/write, frame send/receive |
| AD5941 driver | `ad5940.h/c` | AD5941 AFE register access via SPI0 |
| EIS application | `BATImpedance.h/c` | Sweep configuration, DFT, impedance calculation |
| Platform port | `ADICUP3029Port.c` | AD5941 SPI0/GPIO/interrupt bindings for ADICUP3029 |
