#ifndef SPI_HAL_H
#define SPI_HAL_H

#include <stdint.h>

/*
 * spi_hal.h — SPI1 master driver for ADuCM3029
 *
 * Uses P8 PMOD connector (SPI1 peripheral — P7/SPI0 blocked by EVAL-BATZ):
 *   SCK  = P1_06 (SPI1_SCK,  AF1)
 *   MISO = P1_07 (SPI1_MISO, AF1)
 *   MOSI = P1_08 (SPI1_MOSI, AF1)
 *   CS   = P1_09 (GPIO output, software-controlled, active low)
 *
 * SPI mode 0,0 (CPOL=0, CPHA=0) ~6.5 MHz
 */

void     spi_init(void);
uint8_t  spi_transfer(uint8_t tx);
void     spi_cs_low(void);
void     spi_cs_high(void);

#endif /* SPI_HAL_H */
