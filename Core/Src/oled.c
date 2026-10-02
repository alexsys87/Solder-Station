/**
 * @file    oled.c
 * @brief   OLED driver. SPI1 in transmit-only mode, DMA2 Stream3 Channel3.
 *
 * The frame is sent page by page because SH1106 has no horizontal
 * addressing mode: for every page the 3 address commands are written by
 * the CPU (DC = 0), then 128 data bytes go out by DMA (DC = 1). The next
 * page is started from the DMA transfer-complete interrupt, so a full
 * refresh needs no CPU time apart from 8 short interrupts.
 */
#include "oled.h"
#include "board.h"
#include "config.h"
#include "sys.h"

#if (OLED_CONTROLLER == OLED_SH1106)
  #define OLED_COL_OFFSET   2U
#elif (OLED_CONTROLLER == OLED_SSD1306)
  #define OLED_COL_OFFSET   0U
#else
  #error "Unknown OLED_CONTROLLER"
#endif

#define OLED_DMA            DMA2_Stream3
#define OLED_DMA_CH         3U
#define OLED_DMA_IRQn       DMA2_Stream3_IRQn
#define OLED_DMA_TC_FLAG    DMA_LISR_TCIF3
#define OLED_DMA_CLR_ALL    (DMA_LIFCR_CTCIF3 | DMA_LIFCR_CHTIF3 | DMA_LIFCR_CTEIF3 \
                             | DMA_LIFCR_CDMEIF3 | DMA_LIFCR_CFEIF3)

uint8_t oled_fb[OLED_PAGES * OLED_W];

static volatile bool    s_busy;
static volatile uint8_t s_page;

static inline void cs_low(void)   { gpio_clr(OLED_CS_PORT, OLED_CS_PIN); }
static inline void cs_high(void)  { gpio_set(OLED_CS_PORT, OLED_CS_PIN); }
static inline void dc_cmd(void)   { gpio_clr(OLED_DC_PORT, OLED_DC_PIN); }
static inline void dc_data(void)  { gpio_set(OLED_DC_PORT, OLED_DC_PIN); }

static void spi_wait_idle(void)
{
    while (!(SPI1->SR & SPI_SR_TXE)) { }
    while (SPI1->SR & SPI_SR_BSY) { }
}

static void spi_write(const uint8_t *data, uint32_t len)
{
    while (len--) {
        while (!(SPI1->SR & SPI_SR_TXE)) { }
        *(volatile uint8_t *)&SPI1->DR = *data++;
    }
    spi_wait_idle();
}

static void write_cmds(const uint8_t *cmd, uint32_t len)
{
    cs_low();
    dc_cmd();
    spi_write(cmd, len);
    cs_high();
}

static void start_page(uint8_t page)
{
    uint8_t cmd[3];

    cmd[0] = (uint8_t)(0xB0U | page);
    cmd[1] = (uint8_t)(0x00U | (OLED_COL_OFFSET & 0x0FU));
    cmd[2] = (uint8_t)(0x10U | (OLED_COL_OFFSET >> 4));

    cs_low();
    dc_cmd();
    spi_write(cmd, 3U);
    dc_data();

    DMA2->LIFCR    = OLED_DMA_CLR_ALL;
    OLED_DMA->M0AR = (uint32_t)&oled_fb[(uint32_t)page * OLED_W];
    OLED_DMA->NDTR = OLED_W;
    OLED_DMA->CR  |= DMA_SxCR_EN;
}

void DMA2_Stream3_IRQHandler(void)
{
    if (DMA2->LISR & OLED_DMA_TC_FLAG) {
        DMA2->LIFCR = OLED_DMA_CLR_ALL;
        spi_wait_idle();                 /* last byte still in the shifter */
        if (++s_page < OLED_PAGES) {
            start_page(s_page);
        } else {
            cs_high();
            s_busy = false;
        }
    } else {
        DMA2->LIFCR = OLED_DMA_CLR_ALL;  /* error: abort the frame */
        cs_high();
        s_busy = false;
    }
}

void oled_init(void)
{
    static const uint8_t init_seq[] = {
        0xAE,               /* display off                       */
        0xD5, 0x80,         /* clock divider                     */
        0xA8, 0x3F,         /* multiplex 1/64                    */
        0xD3, 0x00,         /* display offset                    */
        0x40,               /* start line 0                      */
#if (OLED_CONTROLLER == OLED_SH1106)
        0xAD, 0x8B,         /* SH1106: DC-DC converter on        */
#else
        0x8D, 0x14,         /* SSD1306: charge pump on           */
        0x20, 0x02,         /* SSD1306: page addressing mode     */
#endif
        0xA1,               /* segment remap                     */
        0xC8,               /* COM scan direction remapped       */
        0xDA, 0x12,         /* COM pins configuration            */
        0x81, 0x80,         /* contrast                          */
        0xD9, 0xF1,         /* pre-charge period                 */
        0xDB, 0x40,         /* VCOMH level                       */
        0xA4,               /* display RAM content               */
        0xA6,               /* normal (not inverted)             */
    };
    static const uint8_t on = 0xAF;

    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR;

    /* Control pins */
    cs_high();
    gpio_set(OLED_RES_PORT, OLED_RES_PIN);
    gpio_mode(OLED_CS_PORT,  OLED_CS_PIN,  GPIO_MODE_OUT);
    gpio_mode(OLED_DC_PORT,  OLED_DC_PIN,  GPIO_MODE_OUT);
    gpio_mode(OLED_RES_PORT, OLED_RES_PIN, GPIO_MODE_OUT);
    gpio_speed(OLED_CS_PORT, OLED_CS_PIN, GPIO_SPEED_FAST);
    gpio_speed(OLED_DC_PORT, OLED_DC_PIN, GPIO_SPEED_FAST);

    /* SPI pins */
    gpio_speed(OLED_SCK_PORT,  OLED_SCK_PIN,  GPIO_SPEED_FAST);
    gpio_speed(OLED_MOSI_PORT, OLED_MOSI_PIN, GPIO_SPEED_FAST);
    gpio_af(OLED_SCK_PORT,  OLED_SCK_PIN,  5U);
    gpio_af(OLED_MOSI_PORT, OLED_MOSI_PIN, 5U);

    /* SPI1: master, mode 0, 8 bit, MSB first, transmit-only (BIDI out) */
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI
              | SPI_CR1_BIDIMODE | SPI_CR1_BIDIOE
              | ((uint32_t)OLED_SPI_BR << SPI_CR1_BR_Pos);
    SPI1->CR2 = SPI_CR2_TXDMAEN;
    SPI1->CR1 |= SPI_CR1_SPE;

    /* DMA2 Stream3 Channel3: memory -> SPI1_DR, byte size */
    OLED_DMA->CR  = 0U;
    while (OLED_DMA->CR & DMA_SxCR_EN) { }
    OLED_DMA->PAR = (uint32_t)&SPI1->DR;
    OLED_DMA->FCR = 0U;                               /* direct mode   */
    OLED_DMA->CR  = (OLED_DMA_CH << DMA_SxCR_CHSEL_Pos)
                  | DMA_SxCR_MINC | DMA_SxCR_DIR_0
                  | DMA_SxCR_TCIE | DMA_SxCR_TEIE;
    NVIC_SetPriority(OLED_DMA_IRQn, IRQ_PRIO_OLED_DMA);
    NVIC_EnableIRQ(OLED_DMA_IRQn);

    /* Hardware reset */
    sys_delay_ms(5U);
    gpio_clr(OLED_RES_PORT, OLED_RES_PIN);
    sys_delay_ms(10U);
    gpio_set(OLED_RES_PORT, OLED_RES_PIN);
    sys_delay_ms(10U);

    write_cmds(init_seq, sizeof(init_seq));

    /* Clear the RAM before switching the panel on */
    oled_flush();
    oled_wait();
    write_cmds(&on, 1U);
}

bool oled_busy(void)
{
    return s_busy;
}

void oled_wait(void)
{
    while (s_busy) { }
}

bool oled_flush(void)
{
    if (s_busy) return false;
    s_busy = true;
    s_page = 0U;
    start_page(0U);
    return true;
}

void oled_set_contrast(uint8_t value)
{
    uint8_t cmd[2];
    cmd[0] = 0x81U;
    cmd[1] = value;
    oled_wait();
    write_cmds(cmd, 2U);
}

void oled_set_flip(bool flip)
{
    uint8_t cmd[2];
    cmd[0] = flip ? 0xA0U : 0xA1U;
    cmd[1] = flip ? 0xC0U : 0xC8U;
    oled_wait();
    write_cmds(cmd, 2U);
}

void oled_set_power(bool on)
{
    uint8_t cmd = on ? 0xAFU : 0xAEU;
    oled_wait();
    write_cmds(&cmd, 1U);
}
