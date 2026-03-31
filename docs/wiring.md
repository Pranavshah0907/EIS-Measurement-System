# Wiring Diagram

## System Overview

```
PC (Windows)
|
| USB
v
Vector VN1640A (CH1, DB9)
|
| CAN bus (twisted pair, 125 kbps)
| CANH --- pin 7
| CANL --- pin 2
v
Joy-IT SBC-CAN01 (MCP2515 + TJA1050)
|
| SPI1 (directly wired, no level shifter)
v
EVAL-ADICUP3029 (ADuCM3029, P8 PMOD header)
|
| SPI0 (Arduino headers)
v
EVAL-AD5941BATZ (AD5941 impedance analyzer)
|
| Kelvin clips
v
DUT (Device Under Test)
```

---

## CAN Bus: VN1640A CH1 (DB9) -> SBC-CAN01

| VN1640A DB9 Pin | Signal | SBC-CAN01 Terminal |
|-----------------|--------|--------------------|
| Pin 7 | CANH | CANH (screw terminal) |
| Pin 2 | CANL | CANL (screw terminal) |

**Cable:** DB9 male -> twisted pair (~50 cm) -> bare wire into screw terminal.

**Termination:** 120 ohm at both ends:
- Vector end: DB9 120 ohm termination adapter
- SBC-CAN01 end: P1 jumper = ON (120 ohm active)

---

## SPI1: SBC-CAN01 -> ADICUP3029 (P8 PMOD Header)

The MCP2515 CAN controller connects to SPI1 on the P8 PMOD connector (NOT the Arduino headers — those are used by the AD5941).

| SBC-CAN01 Pin | Signal | ADICUP3029 P8 PMOD | GPIO |
|---------------|--------|--------------------|------|
| SCK | SPI Clock | P8 pin (SPI1_CLK) | P1.6 |
| SO (MISO) | SPI Data Out | P8 pin (SPI1_MISO) | P1.7 |
| SI (MOSI) | SPI Data In | P8 pin (SPI1_MOSI) | P1.8 |
| CS | Chip Select | P8 pin (GPIO) | P1.9 |
| VCC | 3.3V logic | 3.3V | -- |
| VCC1 | 5V transceiver | 5V (AVDD_5V) | -- |
| GND | Ground | GND | -- |
| NT (INT) | Interrupt | (not connected) | -- |

**No level shifter needed** — VCC = 3.3V keeps all SPI signals at 3.3V, compatible with ADuCM3029.

---

## SPI0: ADICUP3029 (Arduino Headers) -> EVAL-AD5941BATZ

The AD5941 impedance analyzer board plugs directly into the Arduino headers (P3, P4, P6, P7).

| Signal | ADICUP3029 Arduino Header | GPIO |
|--------|--------------------------|------|
| SPI0_CLK | D13 | P0.0 |
| SPI0_MOSI | D11 | P0.1 |
| SPI0_MISO | D12 | P0.2 |
| CS (AD5941) | -- | P1.10 (GPIO, active low) |
| RESET (AD5941) | -- | P2.6 (GPIO) |
| INT (AD5941) | -- | P0.15 (GPIO, active low) |

---

## Voltage Summary

| Point | Voltage | Source |
|-------|---------|--------|
| MCP2515 logic (VCC) | 3.3V | ADICUP3029 3.3V pin |
| TJA1050 transceiver (VCC1) | 5V | ADICUP3029 AVDD_5V pin |
| SPI1 signals | 3.3V | MCP2515 VCC = 3.3V |
| SPI0 signals | 3.3V | AD5941 powered from Arduino 3.3V |
| CAN bus (differential) | ~2.5V +/- 1V | TJA1050 at 5V |
| ADICUP3029 MCU | 3.3V | USB / onboard regulator |

**No level shifting needed anywhere in this design.**

---

## Important Notes

- **SPI0 and SPI1 are independent** — the AD5941 (SPI0, Arduino headers) and MCP2515 (SPI1, P8 PMOD) do not conflict.
- **Critical firmware bug fix:** The original ADI code in `ADICUP3029Port.c` had `pADI_GPIO1->CFG &= ~(3<<14)` which clears P1.7 (SPI1 MISO) instead of P1.10. Our firmware uses `pADI_GPIO1->CFG &= ~(3<<20)` to correctly target P1.10 for the AD5941 CS GPIO.
