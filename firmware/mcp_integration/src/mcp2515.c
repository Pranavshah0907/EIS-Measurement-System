/*
 * mcp2515.c — MCP2515 CAN controller SPI driver
 *
 * Sprint 2: reset + read_reg (SPI verification)
 * Sprint 3: write_reg, bit_modify, init_loopback, send_frame, recv_frame
 *
 * Uses spi_hal for all bus transfers.
 */

#include "mcp2515.h"
#include "spi_hal.h"

/* ------------------------------------------------------------------ */
/*  SPI instruction bytes (datasheet Table 12-1)                       */
/* ------------------------------------------------------------------ */
#define MCP_RESET       0xC0u
#define MCP_READ        0x03u
#define MCP_WRITE       0x02u
#define MCP_BIT_MODIFY  0x05u
#define MCP_RTS_TX0     0x81u   /* Request-to-send TXB0 */
#define MCP_READ_RX0    0x90u   /* Read RXB0 from SIDH; auto-clears RX0IF on CS↑ */

/* ------------------------------------------------------------------ */
/*  Register addresses                                                  */
/* ------------------------------------------------------------------ */
#define REG_CANSTAT   0x0Eu
#define REG_CANCTRL   0x0Fu
#define REG_CNF3      0x28u
#define REG_CNF2      0x29u
#define REG_CNF1      0x2Au
#define REG_CANINTF   0x2Cu
#define REG_RXB0CTRL  0x60u
#define REG_TXB0SIDH  0x31u   /* TX Buffer 0 — first of sequential block */

/* ------------------------------------------------------------------ */
/*  Bit masks                                                           */
/* ------------------------------------------------------------------ */
#define CANINTF_RX0IF  0x01u    /* CANINTF bit 0: RXB0 full */
#define OPMOD_MASK     0xE0u    /* CANSTAT bits [7:5] = OPMOD */

/* CANCTRL REQOP values (upper 3 bits; lower 5 bits kept 0) */
#define CANCTRL_LOOPBACK  0x40u   /* REQOP = 010 */
#define CANCTRL_NORMAL    0x00u   /* REQOP = 000 */

/* ------------------------------------------------------------------ */
/*  Busy-wait delay                                                     */
/*  At 26 MHz core, ~4 cycles/iter ≈ 154 ns → 6500 iters ≈ 1 ms.     */
/* ------------------------------------------------------------------ */
static void delay_1ms(uint32_t ms)
{
    volatile uint32_t i;
    while (ms--)
    {
        for (i = 6500u; i > 0u; i--)
        {
            /* busy wait */
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Low-level SPI commands                                              */
/* ------------------------------------------------------------------ */

void mcp2515_reset(void)
{
    spi_cs_low();
    spi_transfer(MCP_RESET);
    spi_cs_high();
    delay_1ms(2u);   /* MCP2515 reset ≤128 µs; PLL needs ~2 ms margin */
}

uint8_t mcp2515_read_reg(uint8_t addr)
{
    uint8_t val;
    spi_cs_low();
    spi_transfer(MCP_READ);
    spi_transfer(addr);
    val = spi_transfer(0x00u);   /* clock out register byte */
    spi_cs_high();
    return val;
}

void mcp2515_write_reg(uint8_t addr, uint8_t val)
{
    spi_cs_low();
    spi_transfer(MCP_WRITE);
    spi_transfer(addr);
    spi_transfer(val);
    spi_cs_high();
}

void mcp2515_bit_modify(uint8_t addr, uint8_t mask, uint8_t val)
{
    spi_cs_low();
    spi_transfer(MCP_BIT_MODIFY);
    spi_transfer(addr);
    spi_transfer(mask);
    spi_transfer(val);
    spi_cs_high();
}

/* ------------------------------------------------------------------ */
/*  High-level driver                                                   */
/* ------------------------------------------------------------------ */

/*
 * mcp2515_init_loopback — configure 125 kbps, enter Loopback mode.
 *
 * Baud config for 125 kbps at 16 MHz crystal (from CLAUDE.md):
 *   CNF1 = 0x07, CNF2 = 0xB5, CNF3 = 0x01
 *
 * Returns 0 if CANSTAT confirms loopback mode (OPMOD = 010 → 0x40),
 *        -1 on failure.
 */
int mcp2515_init_loopback(void)
{
    uint8_t stat;

    /* Reset to known state (Config mode) */
    mcp2515_reset();

    /* Baud rate: 125 kbps @ 16 MHz */
    mcp2515_write_reg(REG_CNF1, 0x03u);
    mcp2515_write_reg(REG_CNF2, 0xB5u);
    mcp2515_write_reg(REG_CNF3, 0x01u);

    /* RXB0: accept all messages, no filter, no rollover */
    mcp2515_write_reg(REG_RXB0CTRL, 0x60u);

    /* Switch to Loopback mode (REQOP = 010) */
    mcp2515_write_reg(REG_CANCTRL, CANCTRL_LOOPBACK);
    delay_1ms(1u);   /* wait for mode switch */

    /* Confirm mode via OPMOD bits in CANSTAT */
    stat = mcp2515_read_reg(REG_CANSTAT);
    return ((stat & OPMOD_MASK) == CANCTRL_LOOPBACK) ? 0 : -1;
}

/*
 * mcp2515_init_normal — configure 125 kbps, enter Normal mode (real CAN bus).
 *
 * Identical to init_loopback except REQOP = 000.
 * CANSTAT OPMOD = 000 → (stat & 0xE0) == 0x00.
 *
 * Returns 0 on success, -1 on failure.
 */
int mcp2515_init_normal(void)
{
    uint8_t stat;

    /* Reset to known state (Config mode) */
    mcp2515_reset();

    /* Baud rate: 125 kbps @ 16 MHz */
    mcp2515_write_reg(REG_CNF1, 0x03u);
    mcp2515_write_reg(REG_CNF2, 0xB5u);
    mcp2515_write_reg(REG_CNF3, 0x01u);

    /* RXB0: accept all messages, no filter */
    mcp2515_write_reg(REG_RXB0CTRL, 0x60u);

    /* Switch to Normal mode (REQOP = 000) */
    mcp2515_write_reg(REG_CANCTRL, CANCTRL_NORMAL);
    delay_1ms(1u);

    /* Confirm mode: OPMOD bits [7:5] must be 000 */
    stat = mcp2515_read_reg(REG_CANSTAT);
    return ((stat & OPMOD_MASK) == CANCTRL_NORMAL) ? 0 : -1;
}

/*
 * mcp2515_send_frame — load a standard-frame CAN message into TXB0
 * and issue Request-to-Send.
 *
 * Uses a single multi-byte WRITE starting at TXB0SIDH (MCP2515 address
 * counter auto-increments): SIDH, SIDL, EID8, EID0, DLC, D0..Dn.
 * Then a one-byte RTS command triggers transmission.
 */
/* TXB0CTRL register and TXREQ bit */
#define REG_TXB0CTRL  0x30u
#define TXREQ_BIT     (1u << 3)

void mcp2515_send_frame(const MCP2515_Frame_t *frame)
{
    uint8_t i;
    uint8_t sidh = (uint8_t)((frame->id >> 3) & 0xFFu);    /* ID[10:3] */
    uint8_t sidl = (uint8_t)((frame->id & 0x07u) << 5u);   /* ID[2:0] in bits[7:5], EXIDE=0 */

    /* Wait for previous TX to finish (TXREQ clear) before loading new frame.
     * At 125 kbps an 8-byte frame takes ~1 ms; without this wait the next
     * send_frame call can overwrite TXB0 mid-transmission and lose the frame. */
    while (mcp2515_read_reg(REG_TXB0CTRL) & TXREQ_BIT) { /* spin */ }

    /* Load TXB0 registers in one burst starting at TXB0SIDH */
    spi_cs_low();
    spi_transfer(MCP_WRITE);
    spi_transfer(REG_TXB0SIDH);
    spi_transfer(sidh);             /* TXB0SIDH */
    spi_transfer(sidl);             /* TXB0SIDL (standard frame, EXIDE=0) */
    spi_transfer(0x00u);            /* TXB0EID8 (not used, extended ID bytes) */
    spi_transfer(0x00u);            /* TXB0EID0 */
    spi_transfer(frame->dlc & 0x0Fu); /* TXB0DLC */
    for (i = 0u; i < frame->dlc; i++)
    {
        spi_transfer(frame->data[i]);
    }
    spi_cs_high();

    /* Request-to-Send: transmit TXB0 */
    spi_cs_low();
    spi_transfer(MCP_RTS_TX0);
    spi_cs_high();
}

/*
 * mcp2515_recv_frame — poll for a received frame in RXB0.
 *
 * Polls CANINTF.RX0IF up to timeout_ms times (1 ms per poll).
 * On receipt, uses READ RX BUFFER 0 (0x90) which reads SIDH…DLC…data
 * and automatically clears RX0IF when CS goes high.
 *
 * Returns 0 on success, -1 on timeout.
 */
int mcp2515_recv_frame(MCP2515_Frame_t *frame, uint32_t timeout_ms)
{
    uint32_t t;
    uint8_t  canintf;
    uint8_t  sidh, sidl, dlc;
    uint8_t  i;

    /* Poll until RX0IF is set or timeout */
    for (t = 0u; t < timeout_ms; t++)
    {
        canintf = mcp2515_read_reg(REG_CANINTF);
        if (canintf & CANINTF_RX0IF)
        {
            break;
        }
        delay_1ms(1u);
    }

    /* Final check (covers the break path and last-iteration timing) */
    canintf = mcp2515_read_reg(REG_CANINTF);
    if (!(canintf & CANINTF_RX0IF))
    {
        return -1;   /* timeout — nothing received */
    }

    /*
     * READ RX BUFFER 0 (0x90): reads RXB0SIDH, SIDL, EID8, EID0, DLC, D0..D7
     * CS↑ automatically clears RX0IF (datasheet section 12.8).
     */
    spi_cs_low();
    spi_transfer(MCP_READ_RX0);
    sidh = spi_transfer(0x00u);                 /* RXB0SIDH */
    sidl = spi_transfer(0x00u);                 /* RXB0SIDL */
    (void)spi_transfer(0x00u);                  /* RXB0EID8 (not used) */
    (void)spi_transfer(0x00u);                  /* RXB0EID0 (not used) */
    dlc  = spi_transfer(0x00u) & 0x0Fu;         /* RXB0DLC  */
    for (i = 0u; i < dlc && i < 8u; i++)
    {
        frame->data[i] = spi_transfer(0x00u);
    }
    spi_cs_high();   /* RX0IF auto-cleared here */

    /* Reconstruct 11-bit standard ID */
    frame->id  = ((uint16_t)sidh << 3u) | ((uint16_t)(sidl >> 5u) & 0x07u);
    frame->dlc = dlc;

    return 0;
}
