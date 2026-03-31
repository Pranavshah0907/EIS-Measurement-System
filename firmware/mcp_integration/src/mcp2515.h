#ifndef MCP2515_H
#define MCP2515_H

#include <stdint.h>

/*
 * mcp2515.h — MCP2515 SPI command driver
 *
 * MCP2515 SPI commands (datasheet Table 12-1):
 *   0xC0 — RESET
 *   0x03 — READ       (cmd, addr, data_out)
 *   0x02 — WRITE      (cmd, addr, data_in...)
 *   0x05 — BIT_MODIFY (cmd, addr, mask, data)
 *   0x81 — RTS TXB0   (request-to-send)
 *   0x90 — READ RX0   (read RXB0 from SIDH, auto-clears RX0IF)
 *
 * Key registers:
 *   CANSTAT  0x0E — CAN Status (after reset: 0x80 = Config mode)
 *   CANCTRL  0x0F — CAN Control (REQOP bits [7:5])
 *   CNF3     0x28, CNF2 0x29, CNF1 0x2A — baud rate config
 *   CANINTF  0x2C — interrupt flags (bit 0 = RX0IF)
 *   RXB0CTRL 0x60 — RX buffer 0 control
 *   TXB0SIDH 0x31 — TX buffer 0 start (for sequential write)
 *
 * CANCTRL REQOP modes (bits [7:5]):
 *   0x00 — Normal
 *   0x40 — Loopback (TX feeds back to RX internally, no bus needed)
 *   0x80 — Config  (after reset)
 */

/* CANSTAT OPMOD masks */
#define MCP2515_CANSTAT_RESET_VAL    0x80u   /* Config mode */
#define MCP2515_CANSTAT_LOOPBACK_VAL 0x40u   /* Loopback mode */
#define MCP2515_CANSTAT_NORMAL_VAL   0x00u   /* Normal mode */

/* CAN frame: standard (11-bit) ID only */
typedef struct {
    uint16_t id;        /* 11-bit standard ID */
    uint8_t  dlc;       /* data length 0-8 */
    uint8_t  data[8];
} MCP2515_Frame_t;

/* Low-level SPI commands */
void    mcp2515_reset(void);
uint8_t mcp2515_read_reg(uint8_t addr);
void    mcp2515_write_reg(uint8_t addr, uint8_t val);
void    mcp2515_bit_modify(uint8_t addr, uint8_t mask, uint8_t val);

/* High-level driver */
/* Returns 0 on success, -1 if CANSTAT does not confirm loopback mode */
int  mcp2515_init_loopback(void);

/* Returns 0 on success, -1 if CANSTAT does not confirm normal mode */
int  mcp2515_init_normal(void);

/* Load frame into TXB0 and trigger transmission */
void mcp2515_send_frame(const MCP2515_Frame_t *frame);

/* Poll for received frame; timeout_ms = max wait in ms.
 * Returns 0 on success, -1 on timeout. */
int  mcp2515_recv_frame(MCP2515_Frame_t *frame, uint32_t timeout_ms);

#endif /* MCP2515_H */
