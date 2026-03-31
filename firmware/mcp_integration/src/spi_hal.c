/*
 * spi_hal.c — SPI1 bare-metal master driver for ADuCM3029
 *
 * Uses P8 PMOD connector (SPI1 peripheral) — avoids conflict with
 * EVAL-AD5941BATZ which occupies Arduino DIO High header (P7/SPI0).
 *
 * Pin mapping (SPI1, P8 PMOD):
 *   P1_06 → SPI1_SCK   (AF1, CFG bits[13:12] = 1)
 *   P1_07 → SPI1_MISO  (AF1, CFG bits[15:14] = 1)
 *   P1_08 → SPI1_MOSI  (AF1, CFG bits[17:16] = 1)
 *   P1_09 → CS         (AF0 = GPIO output, active low, software controlled)
 *
 * Connect SBC-CAN01 to P8 PMOD (see SBC-CAN01-Anschlussplan.pdf for labels):
 *   P8 SCK  pin → SBC-CAN01 SCK
 *   P8 MOSI pin → SBC-CAN01 SI
 *   P8 MISO pin → SBC-CAN01 SO
 *   P8 CS   pin → SBC-CAN01 CS
 *
 * SPI: Mode 0,0 (CPOL=0, CPHA=0)
 * CLK = 26 MHz / (2*(DIV+1)) = 26/(2*2) = 6.5 MHz (DIV=1, safe for MCP2515 ≤10 MHz)
 */

#include "spi_hal.h"
#include <ADuCM3029.h>

/* CS pin: P1_09 — bit 9 of GPIO1 */
#define CS_BIT   (1u << 9u)

/* SPI1 CTL: master, OEN, RXOF, TIM=write-to-start, enable */
#define SPI1_CTL_VAL  (BITM_SPI_CTL_CSRST | \
                       BITM_SPI_CTL_MASEN  | \
                       BITM_SPI_CTL_OEN    | \
                       BITM_SPI_CTL_RXOF   | \
                       BITM_SPI_CTL_TIM    | \
                       BITM_SPI_CTL_SPIEN)

void spi_init(void)
{
    /* 1. Pin mux for P1 (CFG is 32-bit, 2 bits per pin):
     *   P1.6 (SCK):  bits[13:12] = 1 (AF1 = SPI1_SCK)
     *   P1.7 (MISO): bits[15:14] = 1 (AF1 = SPI1_MISO)
     *   P1.8 (MOSI): bits[17:16] = 1 (AF1 = SPI1_MOSI)
     *   P1.9 (CS):   bits[19:18] = 0 (AF0 = GPIO)
     */
    pADI_GPIO1->CFG = (pADI_GPIO1->CFG
                       & ~((3u << 12u) | (3u << 14u) | (3u << 16u) | (3u << 18u)))
                       | (1u << 12u) | (1u << 14u) | (1u << 16u);

    /* 2. CS: P1.9 → GPIO output, drive HIGH (deasserted) */
    pADI_GPIO1->OEN |= (uint16_t)CS_BIT;
    pADI_GPIO1->SET  = (uint16_t)CS_BIT;

    /* 3. SPI1 clock: DIV=1 → 6.5 MHz (26 MHz / (2*(1+1))) */
    pADI_SPI1->DIV = 1u;

    /* 4. SPI1 control: master, Mode 0,0, TIM=write-to-start, enable */
    pADI_SPI1->CTL = (uint16_t)SPI1_CTL_VAL;

    /* 5. Transfer count = 1 byte at a time */
    pADI_SPI1->CNT = 1u;
}

uint8_t spi_transfer(uint8_t tx)
{
    /* CNT must be set before each transfer — it decrements to 0 after completion */
    pADI_SPI1->CNT = 1u;

    /* Write TX — starts transfer (TIM=1 mode) */
    pADI_SPI1->TX = (uint16_t)tx;

    /* Wait for transfer done */
    while ((pADI_SPI1->STAT & BITM_SPI_STAT_XFRDONE) == 0u)
    {
        /* spin */
    }

    /* Clear XFRDONE (write-1-to-clear) */
    pADI_SPI1->STAT = BITM_SPI_STAT_XFRDONE;

    return (uint8_t)pADI_SPI1->RX;
}

void spi_cs_low(void)
{
    pADI_GPIO1->CLR = (uint16_t)CS_BIT;
}

void spi_cs_high(void)
{
    pADI_GPIO1->SET = (uint16_t)CS_BIT;
}
