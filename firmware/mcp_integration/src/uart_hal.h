#ifndef UART_HAL_H
#define UART_HAL_H

#include <stdint.h>

/*
 * uart_hal.h — UART0 driver for ADICUP3029
 * Pins: P0_10 = TX (Arduino D0), P0_11 = RX (Arduino D1)
 * Baud: 115200, 8N1
 * Clock: 26 MHz PCLK, OSR=3 (32x), DIVC=4, DIVM=1, DIVN=1563
 */

void uart_init(void);
void uart_putchar(uint8_t c);
void uart_puts(const char *s);
void uart_print_hex8(uint8_t val);
void uart_print_hex16(uint16_t val);

#endif /* UART_HAL_H */
