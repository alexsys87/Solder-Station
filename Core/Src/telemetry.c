/**
 * @file    telemetry.c
 * @brief   USART1 transmitter fed by DMA2 Stream7 Channel4.
 */
#include "telemetry.h"
#include "config.h"

#if USE_TELEMETRY

#include "board.h"
#include "iron.h"
#include "sys.h"
#include <stdio.h>

#define TX_DMA      DMA2_Stream7
#define TX_DMA_CH   4U

static char     s_buf[96];
static uint32_t s_last_cycle;

void telemetry_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;

    gpio_pull(UART_TX_PORT, UART_TX_PIN, GPIO_PULL_UP);
    gpio_af(UART_TX_PORT, UART_TX_PIN, 7U);

    USART1->BRR = (SYSCLK_HZ + TELEMETRY_BAUD / 2U) / TELEMETRY_BAUD;   /* APB2 = 84 MHz */
    USART1->CR3 = USART_CR3_DMAT;
    USART1->CR1 = USART_CR1_TE | USART_CR1_UE;

    TX_DMA->CR = 0U;
    while (TX_DMA->CR & DMA_SxCR_EN) { }
    TX_DMA->PAR = (uint32_t)&USART1->DR;
    TX_DMA->FCR = 0U;
    TX_DMA->CR  = (TX_DMA_CH << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_MINC | DMA_SxCR_DIR_0;
}

static bool tx_busy(void)
{
    return (TX_DMA->CR & DMA_SxCR_EN) != 0U;
}

static void tx_start(uint32_t len)
{
    DMA2->HIFCR = DMA_HIFCR_CTCIF7 | DMA_HIFCR_CHTIF7 | DMA_HIFCR_CTEIF7
                | DMA_HIFCR_CDMEIF7 | DMA_HIFCR_CFEIF7;
    USART1->SR  = ~USART_SR_TC;
    TX_DMA->M0AR = (uint32_t)s_buf;
    TX_DMA->NDTR = len;
    TX_DMA->CR  |= DMA_SxCR_EN;
}

void telemetry_task(void)
{
    const iron_status_t *st = iron_status();
    int len;

    if (st->cycles == s_last_cycle || tx_busy()) return;
    s_last_cycle = st->cycles;

    len = snprintf(s_buf, sizeof(s_buf), "%lu;%d;%u;%d;%d;%d;%d;%d;%d\r\n",
                   (unsigned long)sys_ms(),
                   (int)iron_mode(),
                   (unsigned)iron_target(),
                   (int)(st->tip_c * 10.0f),
                   (int)st->tip_raw,
                   (int)(st->duty * 1000.0f),
                   (int)(st->power_w * 10.0f),
                   (int)(st->vin * 100.0f),
                   (int)(st->cj_c * 10.0f));
    if (len > 0) {
        if (len >= (int)sizeof(s_buf)) len = (int)sizeof(s_buf) - 1;
        tx_start((uint32_t)len);
    }
}

#else

void telemetry_init(void) { }
void telemetry_task(void) { }

#endif /* USE_TELEMETRY */
