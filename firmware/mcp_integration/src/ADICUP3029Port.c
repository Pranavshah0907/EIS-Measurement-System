/*!
 * ADICUP3029Port.c
 * Port layer for AD5940/AD5941 on EVAL-ADICUP3029.
 *
 * Adapted from ADI example. Changes vs original:
 *  - Bug fix: P1.10 CS GPIO cfg uses (3<<20) not (3<<14).
 *    Original ~(3<<14) cleared P1.7 (our SPI1 MISO) instead of P1.10.
 *  - No UART init here — handled by uart_hal.c at 115200 baud.
 *
 * SPI0 pin assignment (AD5941 on Arduino headers):
 *   P0.0 = SCK   P0.1 = MOSI   P0.2 = MISO   P1.10 = CS
 *   P2.6 = RESET (A3)           P0.15 = INT (GP0 falling edge)
 */

#include "ad5940.h"
#include "ADuCM3029.h"

#define SYSTICK_MAXCOUNT  ((1L<<24)-1)
#define SYSTICK_CLKFREQ   26000000L

volatile static uint32_t ucInterrupted = 0;

void AD5940_ReadWriteNBytes(unsigned char *pSendBuffer,
                            unsigned char *pRecvBuff,
                            unsigned long  length)
{
    uint32_t tx_count = 0, rx_count = 0;
    pADI_SPI0->CNT = length;
    while (1)
    {
        uint32_t fifo_sta = pADI_SPI0->FIFO_STAT;
        if (rx_count < length)
        {
            if (fifo_sta & 0xf00)           /* data in RX FIFO */
            {
                *pRecvBuff++ = pADI_SPI0->RX;
                rx_count++;
            }
        }
        if (tx_count < length)
        {
            if ((fifo_sta & 0xf) < 8)       /* space in TX FIFO */
            {
                pADI_SPI0->TX = *pSendBuffer++;
                tx_count++;
            }
        }
        if (rx_count == length && tx_count == length)
            break;
    }
    while ((pADI_SPI0->STAT & BITM_SPI_STAT_XFRDONE) == 0);
}

void AD5940_CsClr(void) { pADI_GPIO1->CLR = (1u << 10); }
void AD5940_CsSet(void) { pADI_GPIO1->SET = (1u << 10); }
void AD5940_RstSet(void){ pADI_GPIO2->SET = (1u << 6);  }
void AD5940_RstClr(void){ pADI_GPIO2->CLR = (1u << 6);  }

void AD5940_Delay10us(uint32_t time)
{
    if (time == 0) return;
    if (time * 10 < SYSTICK_MAXCOUNT / (SYSTICK_CLKFREQ / 1000000))
    {
        SysTick->LOAD = time * 10 * (SYSTICK_CLKFREQ / 1000000);
        SysTick->CTRL = (1u << 2) | (1u << 0);
        while (!((SysTick->CTRL) & (1u << 16)));
        SysTick->CTRL = 0;
    }
    else
    {
        AD5940_Delay10us(time / 2);
        AD5940_Delay10us(time / 2 + (time & 1));
    }
}

uint32_t AD5940_GetMCUIntFlag(void) { return ucInterrupted; }

uint32_t AD5940_ClrMCUIntFlag(void)
{
    pADI_XINT0->CLR = BITM_XINT_CLR_IRQ0;
    ucInterrupted = 0;
    return 1;
}

void Arduino_WriteDn(uint32_t Dn, BoolFlag bHigh)
{
    if (Dn & (1u << 3))
    {
        pADI_GPIO0->OEN |= (1u << 13);
        if (bHigh) pADI_GPIO0->SET = (1u << 13);
        else       pADI_GPIO0->CLR = (1u << 13);
    }
    if (Dn & (1u << 4))
    {
        pADI_GPIO0->OEN |= (1u << 9);
        if (bHigh) pADI_GPIO0->SET = (1u << 9);
        else       pADI_GPIO0->CLR = (1u << 9);
    }
}

uint32_t AD5940_MCUResourceInit(void *pCfg)
{
    (void)pCfg;

    /* Enable pull resistors on all GPIO ports */
    pADI_GPIO0->PE = 0xFFFF;
    pADI_GPIO1->PE = 0xFFFF;
    pADI_GPIO2->PE = 0xFFFF;

    /* P2.6 = AD5940 RESET output, default high */
    pADI_GPIO2->OEN |= (1u << 6);
    pADI_GPIO2->SET  = (1u << 6);

    /* SPI0 pins: P0.0=SCK(AF1), P0.1=MOSI(AF1), P0.2=MISO(AF1) */
    pADI_GPIO0->CFG = (1u << 0) | (1u << 2) | (1u << 4) |
                      (pADI_GPIO0->CFG & ~((3u << 0) | (3u << 2) | (3u << 4)));

    /* P1.10 = SPI0 CS — configure as GPIO output (FIXED: was 3<<14 = P1.7) */
    pADI_GPIO1->CFG &= ~(3u << 20);
    pADI_GPIO1->OEN |=  (1u << 10);

    /* SPI0: master, mode 0,0, ~4.3 MHz (PCLK/6) — AD5941 max is 10 MHz */
    pADI_SPI0->DIV = 2;
    pADI_SPI0->CTL = BITM_SPI_CTL_CSRST  |
                     BITM_SPI_CTL_MASEN   |
                     BITM_SPI_CTL_OEN     |
                     BITM_SPI_CTL_RXOF    |
                     BITM_SPI_CTL_TIM     |
                     BITM_SPI_CTL_SPIEN;
    pADI_SPI0->CNT = 1;

    /* P0.15 = AD5940 GP0 interrupt input, falling edge */
    pADI_GPIO0->IEN |= (1u << 15);
    pADI_XINT0->CFG0 = (0x1u << 0) | (1u << 3);
    pADI_XINT0->CLR  = BITM_XINT_CLR_IRQ0;
    NVIC_EnableIRQ(XINT_EVT0_IRQn);

    AD5940_CsSet();
    AD5940_RstSet();
    return 0;
}

void Ext_Int0_Handler(void)
{
    pADI_XINT0->CLR = BITM_XINT_CLR_IRQ0;
    ucInterrupted = 1;
}
