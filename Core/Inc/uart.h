/**
 * @file    uart.h
 * @brief   USART1 (PA9 TX / PA10 RX): TX ring buffer sent by DMA2 Stream7,
 *          RX ring buffer filled by the RXNE interrupt.
 */
#ifndef UART_H
#define UART_H

#include <stdint.h>

void     uart_init(uint32_t baud);
uint32_t uart_write(const void *data, uint32_t len);
uint32_t uart_tx_free(void);
uint32_t uart_read(void *data, uint32_t max);

#endif /* UART_H */
