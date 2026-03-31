/*
 * main.c — Sprint 5B: CAN-triggered EIS sweep
 *
 * State machine:
 *   IDLE     — polls MCP2515 for CAN 0x100 / byte0=0x10 (START)
 *   SWEEPING — runs AD5941 EIS; sends each impedance point via CAN 0x200
 *
 * CAN protocol:
 *   PC -> MCU: 0x100  byte0=0x10  trigger sweep
 *   MCU -> PC: 0x201  byte0=0x01  sweep started (RCAL running)
 *   MCU -> PC: 0x200  [float32 Re | float32 Im]  one frame per point
 *   MCU -> PC: 0x201  byte0=0x02  sweep complete
 *
 * UART also prints all progress (same format as Sprint 5A).
 *
 * Expected UART output:
 *   Sprint 5B: CAN-triggered EIS
 *   MCP2515 init OK
 *   Waiting for CAN START (ID=0x100, byte0=0x10)...
 *   CAN START received
 *   Measuring RCAL (~40s)...
 *   RCAL done.
 *   Sweep started
 *   Freq:1.000 Re=XX.XX Im=YY.YY mOhm
 *   ...
 *   Sweep complete (40 points)
 *   Waiting for CAN START (ID=0x100, byte0=0x10)...
 */

#include <ADuCM3029.h>
#include "uart_hal.h"
#include "spi_hal.h"
#include "mcp2515.h"
#include "ad5940.h"
#include "BATImpedance.h"

/* ------------------------------------------------------------------ */
/*  Application buffer                                                  */
/* ------------------------------------------------------------------ */
#define APPBUFF_SIZE  512u
static uint32_t AppBuff[APPBUFF_SIZE];

/* ------------------------------------------------------------------ */
/*  State machine                                                       */
/* ------------------------------------------------------------------ */
#define APP_IDLE     0u
#define APP_SWEEPING 1u
static uint8_t  s_state       = APP_IDLE;
static uint32_t s_point_count = 0u;
static uint32_t s_idle_hb     = 0u;   /* heartbeat counter — sends STAT_READY while IDLE */
#define IDLE_HB_PERIOD 100000u         /* ~1 s at 10µs/iteration; PC diagnostic window is 5 s */

/* ------------------------------------------------------------------ */
/*  Configurable sweep parameters (Sprint 6)                           */
/*  Defaults match bat_struct_init() — overridden by CAN config cmds.  */
/* ------------------------------------------------------------------ */
static float    s_start_hz     = 1.0f;
static float    s_stop_hz      = 1000.0f;
static uint16_t s_sweep_points = 40u;
static float    s_ac_volt_pp   = 300.0f;   /* mV peak-to-peak */
static float    s_dc_volt      = 1200.0f;  /* mV DC bias */

/* ------------------------------------------------------------------ */
/*  Float <-> bytes union (IEEE 754 little-endian)                     */
/* ------------------------------------------------------------------ */
typedef union { float f; uint8_t b[4]; } FloatBytes_t;

/* ------------------------------------------------------------------ */
/*  Simple float printer — avoids printf / semihosting entirely        */
/* ------------------------------------------------------------------ */
static void uart_print_uint(uint32_t val)
{
    char    buf[12];
    uint8_t idx = 0u;
    if (val == 0u) { uart_putchar('0'); return; }
    while (val > 0u) { buf[idx++] = (char)('0' + (val % 10u)); val /= 10u; }
    while (idx > 0u) { uart_putchar((uint8_t)buf[--idx]); }
}

static void uart_print_float(float val, uint8_t decimals)
{
    uint32_t mult = 1u;
    uint32_t i;
    int32_t  int_part;
    uint32_t dec_part;
    char     dbuf[8];

    for (i = 0u; i < (uint32_t)decimals; i++) { mult *= 10u; }

    if (val < 0.0f) { uart_putchar('-'); val = -val; }

    val     += 0.5f / (float)mult;           /* round */
    int_part = (int32_t)val;
    dec_part = (uint32_t)((val - (float)int_part) * (float)mult);

    uart_print_uint((uint32_t)int_part);
    uart_putchar('.');

    for (i = (uint32_t)decimals; i > 0u; i--)
    {
        dbuf[i - 1u] = (char)('0' + dec_part % 10u);
        dec_part /= 10u;
    }
    for (i = 0u; i < (uint32_t)decimals; i++) { uart_putchar((uint8_t)dbuf[i]); }
}

/* ------------------------------------------------------------------ */
/*  CAN TX helpers                                                      */
/* ------------------------------------------------------------------ */

/* Send a 1-byte status frame on ID=0x201 */
static void can_send_status(uint8_t status)
{
    MCP2515_Frame_t f;
    f.id     = 0x201u;
    f.dlc    = 1u;
    f.data[0] = status;
    mcp2515_send_frame(&f);
}

/*
 * Send one impedance point on ID=0x200.
 * Payload: bytes 0-3 = Re (float32 LE), bytes 4-7 = Im (float32 LE).
 */
static void can_send_impedance(float re, float im)
{
    MCP2515_Frame_t f;
    FloatBytes_t    rb, ib;
    rb.f = re;
    ib.f = im;
    f.id  = 0x200u;
    f.dlc = 8u;
    f.data[0] = rb.b[0]; f.data[1] = rb.b[1];
    f.data[2] = rb.b[2]; f.data[3] = rb.b[3];
    f.data[4] = ib.b[0]; f.data[5] = ib.b[1];
    f.data[6] = ib.b[2]; f.data[7] = ib.b[3];
    mcp2515_send_frame(&f);
}

/* ------------------------------------------------------------------ */
/*  AD5941 platform init (clocks, FIFO, interrupts)                   */
/* ------------------------------------------------------------------ */
static void ad5940_platform_init(void)
{
    CLKCfg_Type   clk_cfg;
    FIFOCfg_Type  fifo_cfg;
    AGPIOCfg_Type gpio_cfg;

    AD5940_HWReset();
    AD5940_Initialize();

    clk_cfg.ADCClkDiv      = ADCCLKDIV_1;
    clk_cfg.ADCCLkSrc      = ADCCLKSRC_HFOSC;
    clk_cfg.SysClkDiv      = SYSCLKDIV_1;
    clk_cfg.SysClkSrc      = SYSCLKSRC_HFOSC;
    clk_cfg.HfOSC32MHzMode = bFALSE;
    clk_cfg.HFOSCEn        = bTRUE;
    clk_cfg.HFXTALEn       = bFALSE;
    clk_cfg.LFOSCEn        = bTRUE;
    AD5940_CLKCfg(&clk_cfg);

    fifo_cfg.FIFOEn     = bFALSE;
    fifo_cfg.FIFOMode   = FIFOMODE_FIFO;
    fifo_cfg.FIFOSize   = FIFOSIZE_4KB;
    fifo_cfg.FIFOSrc    = FIFOSRC_DFT;
    fifo_cfg.FIFOThresh = 4;
    AD5940_FIFOCfg(&fifo_cfg);
    fifo_cfg.FIFOEn = bTRUE;
    AD5940_FIFOCfg(&fifo_cfg);

    AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT,         bTRUE);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);

    gpio_cfg.FuncSet     = GP0_INT | GP2_SYNC;
    gpio_cfg.InputEnSet  = AGPIO_Pin2;
    gpio_cfg.OutputEnSet = AGPIO_Pin0 | AGPIO_Pin2;
    gpio_cfg.OutVal      = 0;
    gpio_cfg.PullEnSet   = 0;
    AD5940_AGPIOCfg(&gpio_cfg);

    AD5940_SleepKeyCtrlS(SLPKEY_UNLOCK);
}

/* ------------------------------------------------------------------ */
/*  Sweep configuration                                                */
/* ------------------------------------------------------------------ */
static void bat_struct_init(void)
{
    AppBATCfg_Type *pCfg;
    AppBATGetCfg(&pCfg);

    pCfg->SeqStartAddr  = 0;
    pCfg->MaxSeqLen     = 512;
    pCfg->RcalVal       = 50.0f;
    pCfg->ACVoltPP      = 300.0f;
    pCfg->DCVolt        = 1200.0f;
    pCfg->DftNum        = DFTNUM_1024; /* ~3.3 s/pt at 1 Hz */
    pCfg->FifoThresh    = 2;

    pCfg->SweepCfg.SweepEn     = bTRUE;
    pCfg->SweepCfg.SweepStart  = 1.0f;
    pCfg->SweepCfg.SweepStop   = 1000.0f;
    pCfg->SweepCfg.SweepPoints = 40;
    pCfg->SweepCfg.SweepLog    = bTRUE;
}

/* ------------------------------------------------------------------ */
/*  Process one batch of impedance results — UART print + CAN TX      */
/* ------------------------------------------------------------------ */
static void show_result(uint32_t *pData, uint32_t count)
{
    fImpCar_Type *pImp = (fImpCar_Type *)pData;
    float    freq = 0.0f;
    uint32_t i;

    AppBATCtrl(BATCTRL_GETFREQ, &freq);

    for (i = 0u; i < count; i++)
    {
        s_point_count++;

        /* UART */
        uart_puts("Freq:");
        uart_print_float(freq, 3u);
        uart_puts(" Re=");
        uart_print_float(pImp[i].Real, 2u);
        uart_puts(" Im=");
        uart_print_float(pImp[i].Image, 2u);
        uart_puts(" mOhm\r\n");

        /* CAN */
        can_send_impedance(pImp[i].Real, pImp[i].Image);
    }
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */
int main(void)
{
    uint32_t       temp;
    AppBATCfg_Type *pCfg;
    MCP2515_Frame_t rxf;

    pADI_WDT0->CTL       = 0xC9u;   /* disable watchdog */
    pADI_CLKG0_CLK->CTL1 = 0u;      /* PCLK divider = 1 */

    /* 1. UART */
    uart_init();
    uart_puts("\r\nSprint 5B: CAN-triggered EIS\r\n");

    /* 2. AD5940 MCU resources (SPI0 + GPIO + ext interrupt) */
    AD5940_MCUResourceInit(0);

    /* 3. AD5940 AFE platform init — must complete before SPI1 init
     *    so any AD5941 register writes (SPI0) don't interfere with
     *    SPI1 GPIO configuration that follows. */
    uart_puts("Init AD5941...\r\n");
    ad5940_platform_init();

    /* 3b. Verify SPI0 communication — ADIID must be 0x4144 */
    {
        uint32_t adiid = AD5940_GetADIID();
        uart_puts("AD5941 ADIID=0x");
        uart_print_uint(adiid);
        uart_puts("\r\n");
        if (adiid != AD5940_ADIID)
        {
            uart_puts("ERROR: SPI0 to AD5941 failed. Check wiring.\r\n");
            while (1) { /* halt */ }
        }
        uart_puts("SPI0 OK\r\n");
    }

    /* 4. SPI1 for MCP2515 — initialized AFTER all AD5941 work is done */
    spi_init();

    /* 5. Sweep parameters (fixed for this sprint) */
    bat_struct_init();

    /* 6. MCP2515 diagnostic: verify SPI1 first, then enter Normal mode */
    {
        uint8_t canstat;

        /* Step 6a: reset MCP2515, read CANSTAT — expect 0x80 (Config mode).
         * 0xFF = MISO disconnected, 0x00 = CS/MOSI issue, 0x80 = SPI OK */
        mcp2515_reset();
        canstat = mcp2515_read_reg(0x0Eu);
        uart_puts("MCP2515 CANSTAT after reset=0x");
        uart_print_uint((uint32_t)canstat);
        uart_puts("\r\n");

        if ((canstat & 0xE0u) != 0x80u)
        {
            uart_puts("ERROR: SPI1 to MCP2515 failed (expected 0x80).\r\n");
            uart_puts("  Check SPI1 wiring: SCK=P1.6, MISO=P1.7, MOSI=P1.8, CS=P1.9\r\n");
            while (1) { /* halt */ }
        }
        uart_puts("SPI1 OK\r\n");

        /* Step 6b: enter Normal mode */
        if (mcp2515_init_normal() != 0)
        {
            canstat = mcp2515_read_reg(0x0Eu);
            uart_puts("ERROR: MCP2515 Normal mode failed. CANSTAT=0x");
            uart_print_uint((uint32_t)canstat);
            uart_puts(" (expected 0x00)\r\n");
            while (1) { /* halt */ }
        }
        uart_puts("MCP2515 init OK\r\n");
    }

    /* 7. Main loop — state machine */
    uart_puts("Waiting for CAN START (ID=0x100, byte0=0x10)...\r\n");

    while (1)
    {
        /* ── IDLE: wait for CAN trigger ── */
        if (s_state == APP_IDLE)
        {
            /* Periodic STAT_READY heartbeat so PC diagnostics can detect us */
            s_idle_hb++;
            if (s_idle_hb >= IDLE_HB_PERIOD)
            {
                can_send_status(0x00u);
                s_idle_hb = 0u;
            }

            /* Non-blocking poll (timeout=0 -> single CANINTF read) */
            if (mcp2515_recv_frame(&rxf, 0u) == 0)
            {
                if (rxf.id == 0x100u && rxf.dlc >= 1u)
                {
                    if (rxf.data[0] == 0x01u && rxf.dlc >= 5u)
                    {
                        /* SetStartHz: bytes 1-4 = float32 LE */
                        FloatBytes_t fb;
                        fb.b[0] = rxf.data[1]; fb.b[1] = rxf.data[2];
                        fb.b[2] = rxf.data[3]; fb.b[3] = rxf.data[4];
                        s_start_hz = fb.f;
                        uart_puts("Config: StartHz=");
                        uart_print_float(s_start_hz, 3u);
                        uart_puts("\r\n");
                    }
                    else if (rxf.data[0] == 0x02u && rxf.dlc >= 5u)
                    {
                        /* SetStopHz: bytes 1-4 = float32 LE */
                        FloatBytes_t fb;
                        fb.b[0] = rxf.data[1]; fb.b[1] = rxf.data[2];
                        fb.b[2] = rxf.data[3]; fb.b[3] = rxf.data[4];
                        s_stop_hz = fb.f;
                        uart_puts("Config: StopHz=");
                        uart_print_float(s_stop_hz, 3u);
                        uart_puts("\r\n");
                    }
                    else if (rxf.data[0] == 0x03u && rxf.dlc >= 3u)
                    {
                        /* SetPoints: bytes 1-2 = uint16 LE */
                        s_sweep_points = (uint16_t)rxf.data[1]
                                       | ((uint16_t)rxf.data[2] << 8u);
                        uart_puts("Config: Points=");
                        uart_print_uint((uint32_t)s_sweep_points);
                        uart_puts("\r\n");
                    }
                    else if (rxf.data[0] == 0x04u && rxf.dlc >= 5u)
                    {
                        /* SetACVoltPP: bytes 1-4 = float32 LE (mV) */
                        FloatBytes_t fb;
                        fb.b[0] = rxf.data[1]; fb.b[1] = rxf.data[2];
                        fb.b[2] = rxf.data[3]; fb.b[3] = rxf.data[4];
                        s_ac_volt_pp = fb.f;
                        uart_puts("Config: ACVoltPP=");
                        uart_print_float(s_ac_volt_pp, 1u);
                        uart_puts(" mV\r\n");
                    }
                    else if (rxf.data[0] == 0x05u && rxf.dlc >= 5u)
                    {
                        /* SetDCVolt: bytes 1-4 = float32 LE (mV) */
                        FloatBytes_t fb;
                        fb.b[0] = rxf.data[1]; fb.b[1] = rxf.data[2];
                        fb.b[2] = rxf.data[3]; fb.b[3] = rxf.data[4];
                        s_dc_volt = fb.f;
                        uart_puts("Config: DCVolt=");
                        uart_print_float(s_dc_volt, 1u);
                        uart_puts(" mV\r\n");
                    }
                    else if (rxf.data[0] == 0x10u)
                    {
                        /* START: apply current config then run sweep */
                        uart_puts("CAN START received\r\n");
                        uart_puts("  Start="); uart_print_float(s_start_hz, 3u);
                        uart_puts(" Stop=");  uart_print_float(s_stop_hz, 3u);
                        uart_puts(" Pts=");   uart_print_uint((uint32_t)s_sweep_points);
                        uart_puts(" AC=");  uart_print_float(s_ac_volt_pp, 1u);
                        uart_puts("mV DC="); uart_print_float(s_dc_volt, 1u);
                        uart_puts("mV\r\n");

                        /* Apply config to AppBAT before init */
                        AppBATGetCfg(&pCfg);
                        pCfg->SweepCfg.SweepStart  = s_start_hz;
                        pCfg->SweepCfg.SweepStop   = s_stop_hz;
                        pCfg->SweepCfg.SweepPoints = (uint32_t)s_sweep_points;
                        pCfg->ACVoltPP      = s_ac_volt_pp;
                        pCfg->DCVolt        = s_dc_volt;

                        s_point_count = 0u;
                        s_idle_hb     = 0u;   /* reset so heartbeat resumes promptly after sweep */

                        /* Re-load sequencer into AD5941 SRAM */
                        AppBATInit(AppBuff, APPBUFF_SIZE);

                        /* Notify PC: sweep started, RCAL running */
                        can_send_status(0x01u);

                        uart_puts("Measuring RCAL (~40s)...\r\n");
                        AppBATCtrl(BATCTRL_MRCAL, 0);
                        uart_puts("RCAL done.\r\n");

                        uart_puts("Sweep started\r\n");
                        AppBATCtrl(BATCTRL_START, 0);

                        s_state = APP_SWEEPING;
                    }
                }
            }
        }

        /* ── SWEEPING: collect AD5941 results, TX via CAN ── */
        else if (s_state == APP_SWEEPING)
        {
            if (AD5940_GetMCUIntFlag())
            {
                AD5940_ClrMCUIntFlag();
                temp = APPBUFF_SIZE;
                AppBATISR(AppBuff, &temp);
                show_result(AppBuff, temp);

                AppBATGetCfg(&pCfg);
                if (s_point_count >= pCfg->SweepCfg.SweepPoints)
                {
                    /* Sweep complete */
                    can_send_status(0x02u);
                    uart_puts("Sweep complete (");
                    uart_print_uint(s_point_count);
                    uart_puts(" points)\r\n");
                    uart_puts("Waiting for CAN START (ID=0x100, byte0=0x10)...\r\n");
                    s_state = APP_IDLE;
                }
                else
                {
                    /* Trigger next measurement */
                    AD5940_SEQMmrTrig(SEQID_0);
                }
            }
        }
    }
}
