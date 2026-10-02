/**
 * @file    uart.c
 * @brief   USART1 driver: DMA transmit from a ring buffer, interrupt receive.
 */
#include "uart.h"
#include "board.h"
#include "config.h"
#include "sys.h"

#define TX_DMA          DMA2_Stream7
#define TX_DMA_CH       4U
#define TX_RING_SIZE    2048U       /* power of two */
#define RX_RING_SIZE    256U        /* power of two */

static uint8_t           s_tx[TX_RING_SIZE];
static volatile uint32_t s_tx_head;
static volatile uint32_t s_tx_tail;
static volatile uint32_t s_tx_dma_len;      /* bytes in the running DMA transfer */

static uint8_t           s_rx[RX_RING_SIZE];
static volatile uint32_t s_rx_head;
static volatile uint32_t s_rx_tail;

/* Start a DMA transfer of the contiguous part of the ring (IRQs disabled) */
static void tx_kick(void)
{
    uint32_t len;

    if (s_tx_dma_len != 0U || s_tx_head == s_tx_tail) return;

    len = (s_tx_head > s_tx_tail) ? (s_tx_head - s_tx_tail) : (TX_RING_SIZE - s_tx_tail);
    s_tx_dma_len = len;

    DMA2->HIFCR = DMA_HIFCR_CTCIF7 | DMA_HIFCR_CHTIF7 | DMA_HIFCR_CTEIF7
                | DMA_HIFCR_CDMEIF7 | DMA_HIFCR_CFEIF7;
    TX_DMA->M0AR = (uint32_t)&s_tx[s_tx_tail];
    TX_DMA->NDTR = len;
    TX_DMA->CR  |= DMA_SxCR_EN;
}

void DMA2_Stream7_IRQHandler(void)
{
    DMA2->HIFCR = DMA_HIFCR_CTCIF7 | DMA_HIFCR_CHTIF7 | DMA_HIFCR_CTEIF7
                | DMA_HIFCR_CDMEIF7 | DMA_HIFCR_CFEIF7;
    s_tx_tail = (s_tx_tail + s_tx_dma_len) & (TX_RING_SIZE - 1U);
    s_tx_dma_len = 0U;
    tx_kick();
}

void USART1_IRQHandler(void)
{
    uint32_t sr = USART1->SR;
    uint8_t c;

    if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) {
        c = (uint8_t)USART1->DR;            /* also clears ORE / FE / NE */
        if (sr & USART_SR_RXNE) {
            uint32_t next = (s_rx_head + 1U) & (RX_RING_SIZE - 1U);
            if (next != s_rx_tail) {
                s_rx[s_rx_head] = c;
                s_rx_head = next;
            }
        }
    }
}

void uart_init(uint32_t baud)
{
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;

    gpio_pull(UART_PORT, UART_TX_PIN, GPIO_PULL_UP);
    gpio_pull(UART_PORT, UART_RX_PIN, GPIO_PULL_UP);
    gpio_af(UART_PORT, UART_TX_PIN, 7U);
    gpio_af(UART_PORT, UART_RX_PIN, 7U);

    USART1->BRR = (SYSCLK_HZ + baud / 2U) / baud;           /* APB2 = 84 MHz */
    USART1->CR3 = USART_CR3_DMAT;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;

    TX_DMA->CR = 0U;
    while (TX_DMA->CR & DMA_SxCR_EN) { }
    TX_DMA->PAR = (uint32_t)&USART1->DR;
    TX_DMA->FCR = 0U;
    TX_DMA->CR  = (TX_DMA_CH << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_MINC | DMA_SxCR_DIR_0
                | DMA_SxCR_TCIE;

    NVIC_SetPriority(DMA2_Stream7_IRQn, IRQ_PRIO_UART);
    NVIC_EnableIRQ(DMA2_Stream7_IRQn);
    NVIC_SetPriority(USART1_IRQn, IRQ_PRIO_UART);
    NVIC_EnableIRQ(USART1_IRQn);
}

uint32_t uart_tx_free(void)
{
    return TX_RING_SIZE - 1U - ((s_tx_head - s_tx_tail) & (TX_RING_SIZE - 1U));
}

uint32_t uart_write(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t n = 0;
    uint32_t st = sys_irq_save();

    while (n < len) {
        uint32_t next = (s_tx_head + 1U) & (TX_RING_SIZE - 1U);
        if (next == s_tx_tail) break;
        s_tx[s_tx_head] = p[n++];
        s_tx_head = next;
    }
    tx_kick();
    sys_irq_restore(st);
    return n;
}

uint32_t uart_read(void *data, uint32_t max)
{
    uint8_t *p = (uint8_t *)data;
    uint32_t n = 0;
    while (n < max && s_rx_tail != s_rx_head) {
        p[n++] = s_rx[s_rx_tail];
        s_rx_tail = (s_rx_tail + 1U) & (RX_RING_SIZE - 1U);
    }
    return n;
}
