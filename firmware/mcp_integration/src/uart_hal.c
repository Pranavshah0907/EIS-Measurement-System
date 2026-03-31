/*
 * uart_hal.c -- UART0 driver for ADuCM3029, mirroring the proven
 *               ADICUP3029Port / main.c from the AD5940_BATImpedance example.
 *
 * Key differences from previous bare-metal attempt:
 *   - Uses pADI_UART0 struct (same as working example)
 *   - Reads actual PCLK divider from CTL1 rather than assuming 1
 *   - Enables TX/RX FIFO and clears it before use
 *   - fputc-style blocking TX: write then poll THRE
 */

#include "uart_hal.h"
#include <ADuCM3029.h>

void uart_init(void)
{
    int iDiv, iOSR;
    unsigned long long ullRtClk = 26000000uLL;
    int iBaud = 115200;
    int i1;

    /* 1. Pin mux: P0_10 = UART0_TX (AF1), P0_11 = UART0_RX (AF1) */
    pADI_GPIO0->CFG = (1u << 22) | (1u << 20) |
                      (pADI_GPIO0->CFG & ~((3u << 22) | (3u << 20)));

    /* 2. Read actual PCLK divider (bits [10:8] of CTL1) */
    iDiv = (int)(pADI_CLKG0_CLK->CTL1 & BITM_CLKG_CLK_CTL1_PCLKDIVCNT);
    iDiv >>= 8;
    if (iDiv == 0)
        iDiv = 1;

    /* 3. Determine root clock (CTL0 CLKMUX) */
    switch (pADI_CLKG0_CLK->CTL0 & BITM_CLKG_CLK_CTL0_CLKMUX)
    {
        case 0:  ullRtClk = 26000000uLL; break;   /* HFOSC */
        case 1:
            ullRtClk = ((pADI_CLKG0_CLK->CTL0 & 0x200u) == 0x200u)
                       ? 26000000uLL : 16000000uLL;
            break;
        default: ullRtClk = 26000000uLL; break;
    }

    /* 4. Baud rate dividers -- same formula as working example */
    pADI_UART0->COMLCR2 = 0x3;   /* OSR = 32x */
    iOSR = 32;

    i1 = (int)((ullRtClk / (unsigned long long)(iOSR * iDiv)) / (unsigned long long)iBaud) - 1;
    pADI_UART0->COMDIV = (uint16_t)i1;

    pADI_UART0->COMFBR = (uint16_t)(0x8800u |
        ((((2048 / (iOSR * iDiv)) * ullRtClk) / (unsigned long long)i1)
         / (unsigned long long)iBaud - 2048u));

    /* 5. Line control: 8N1, no parity */
    pADI_UART0->COMIEN  = 0;
    pADI_UART0->COMLCR  = 0x0003u;   /* WLS=11 (8 bits), 1 stop, no parity */

    /* 6. Enable FIFO, clear TX and RX FIFOs */
    pADI_UART0->COMFCR  = BITM_UART_COMFCR_FIFOEN;
    pADI_UART0->COMFCR |= BITM_UART_COMFCR_RFCLR | BITM_UART_COMFCR_TFCLR;
    pADI_UART0->COMFCR &= (uint16_t)~(BITM_UART_COMFCR_RFCLR | BITM_UART_COMFCR_TFCLR);
}

void uart_putchar(uint8_t c)
{
    pADI_UART0->COMTX = (uint16_t)c;
    while ((pADI_UART0->COMLSR & 0x20u) == 0u)
    {
        /* wait for THRE (TX holding register empty) */
    }
}

void uart_puts(const char *s)
{
    while (*s)
    {
        uart_putchar((uint8_t)*s++);
    }
}

void uart_print_hex8(uint8_t val)
{
    static const char hex[] = "0123456789ABCDEF";
    uart_putchar((uint8_t)hex[(val >> 4u) & 0x0Fu]);
    uart_putchar((uint8_t)hex[val & 0x0Fu]);
}

void uart_print_hex16(uint16_t val)
{
    uart_print_hex8((uint8_t)(val >> 8u));
    uart_print_hex8((uint8_t)(val & 0xFFu));
}
