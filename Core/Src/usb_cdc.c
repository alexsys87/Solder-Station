/**
 * @file    usb_cdc.c
 * @brief   Minimal USB full-speed CDC ACM device for the STM32F401 OTG FS core.
 *
 * Endpoints:
 *   EP0        control, 64 bytes
 *   EP1 IN/OUT bulk data, 64 bytes
 *   EP2 IN     interrupt notification, 8 bytes (never used)
 *
 * The OTG core runs in slave (non-DMA) mode: received packets are popped
 * from the shared RX FIFO in the RXFLVL interrupt, transmitted packets are
 * written into the per-endpoint TX FIFOs. VBUS sensing is disabled (the
 * BlackPill does not route VBUS to PA9; PA9 is used by USART1).
 *
 * The 48 MHz USB clock comes from PLLQ (336 MHz / 7), see sys.c.
 */
#include "usb_cdc.h"
#include "board.h"
#include "irq.h"
#include "sys.h"
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Identification                                                            */
/* ------------------------------------------------------------------------- */
#define USB_VID             0x0483U     /* STMicroelectronics              */
#define USB_PID             0x5740U     /* Virtual COM Port                */
#define USB_BCD_DEVICE      0x0110U

#define EP0_SIZE            64U
#define CDC_DATA_SIZE       64U
#define CDC_CMD_SIZE        8U
#define CDC_DATA_EP         1U
#define CDC_CMD_EP          2U

#define TX_RING_SIZE        2048U       /* power of two */
#define RX_RING_SIZE        512U        /* power of two */

/* ------------------------------------------------------------------------- */
/* Register access                                                           */
/* ------------------------------------------------------------------------- */
#define OTG         USB_OTG_FS
#define DEV         ((USB_OTG_DeviceTypeDef *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_DEVICE_BASE))
#define INEP(i)     ((USB_OTG_INEndpointTypeDef *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_IN_ENDPOINT_BASE \
                                                   + (i) * USB_OTG_EP_REG_SIZE))
#define OUTEP(i)    ((USB_OTG_OUTEndpointTypeDef *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_OUT_ENDPOINT_BASE \
                                                    + (i) * USB_OTG_EP_REG_SIZE))
#define FIFO(i)     (*(volatile uint32_t *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_FIFO_BASE \
                                            + (i) * USB_OTG_FIFO_SIZE))
#define PCGCCTL     (*(volatile uint32_t *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_PCGCCTL_BASE))

/* RX status packet types */
#define PKTSTS_OUT_DATA     2U
#define PKTSTS_SETUP_DATA   6U

/* ------------------------------------------------------------------------- */
/* Descriptors                                                               */
/* ------------------------------------------------------------------------- */
static const uint8_t s_dev_desc[18] = {
    18, 0x01,                   /* bLength, DEVICE                         */
    0x00, 0x02,                 /* USB 2.0                                 */
    0x02, 0x00, 0x00,           /* class CDC                               */
    EP0_SIZE,
    (uint8_t)(USB_VID & 0xFFU), (uint8_t)(USB_VID >> 8),
    (uint8_t)(USB_PID & 0xFFU), (uint8_t)(USB_PID >> 8),
    (uint8_t)(USB_BCD_DEVICE & 0xFFU), (uint8_t)(USB_BCD_DEVICE >> 8),
    1, 2, 3,                    /* manufacturer, product, serial strings   */
    1                           /* one configuration                       */
};

#define CFG_DESC_LEN    67U
static const uint8_t s_cfg_desc[CFG_DESC_LEN] = {
    /* configuration */
    9, 0x02, CFG_DESC_LEN, 0, 2, 1, 0, 0x80, 50,
    /* interface 0: communication class */
    9, 0x04, 0, 0, 1, 0x02, 0x02, 0x01, 0,
    /* CDC header, call management, ACM, union */
    5, 0x24, 0x00, 0x10, 0x01,
    5, 0x24, 0x01, 0x00, 0x01,
    4, 0x24, 0x02, 0x02,
    5, 0x24, 0x06, 0x00, 0x01,
    /* notification endpoint */
    7, 0x05, 0x80 | CDC_CMD_EP, 0x03, CDC_CMD_SIZE, 0, 0x10,
    /* interface 1: data class */
    9, 0x04, 1, 0, 2, 0x0A, 0x00, 0x00, 0,
    /* data OUT / IN endpoints */
    7, 0x05, CDC_DATA_EP, 0x02, CDC_DATA_SIZE, 0, 0,
    7, 0x05, 0x80 | CDC_DATA_EP, 0x02, CDC_DATA_SIZE, 0, 0,
};

static const char s_str_manuf[]   = "DIY";
static const char s_str_product[] = "T12 Soldering Station";

/* ------------------------------------------------------------------------- */
/* State                                                                     */
/* ------------------------------------------------------------------------- */
typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} setup_t;

static union { uint32_t w[2]; setup_t s; } s_setup;
static uint8_t  s_ep0_buf[EP0_SIZE];
static uint32_t s_ep0_rx_len;
static uint8_t  s_ep0_pending;          /* class request waiting for data  */
static uint8_t  s_desc_buf[66];         /* string descriptors are built here */

static volatile bool s_configured;
static volatile bool s_dtr;
static uint8_t  s_line_coding[7] = { 0x00, 0xC2, 0x01, 0x00, 0, 0, 8 };   /* 115200 8N1 */

static uint8_t  s_tx_ring[TX_RING_SIZE];
static volatile uint32_t s_tx_head;
static volatile uint32_t s_tx_tail;
static volatile bool s_tx_busy;
static volatile bool s_tx_zlp;

static uint8_t  s_rx_ring[RX_RING_SIZE];
static volatile uint32_t s_rx_head;
static volatile uint32_t s_rx_tail;
static volatile bool s_rx_blocked;

/* ------------------------------------------------------------------------- */
/* FIFO helpers                                                              */
/* ------------------------------------------------------------------------- */
static void fifo_read(uint8_t *dst, uint32_t len)
{
    uint32_t i;
    uint32_t w = 0;
    for (i = 0; i < len; i++) {
        if ((i & 3U) == 0U) w = FIFO(0);
        dst[i] = (uint8_t)(w >> ((i & 3U) * 8U));
    }
}

static void fifo_discard(uint32_t len)
{
    uint32_t i;
    for (i = 0; i < (len + 3U) / 4U; i++) (void)FIFO(0);
}

static void fifo_write(uint32_t ep, const uint8_t *src, uint32_t len)
{
    uint32_t i;
    uint32_t w;
    for (i = 0; i < len; i += 4U) {
        w = src[i];
        if (i + 1U < len) w |= (uint32_t)src[i + 1U] << 8;
        if (i + 2U < len) w |= (uint32_t)src[i + 2U] << 16;
        if (i + 3U < len) w |= (uint32_t)src[i + 3U] << 24;
        FIFO(ep) = w;
    }
}

static void flush_tx_fifo(uint32_t num)
{
    OTG->GRSTCTL = USB_OTG_GRSTCTL_TXFFLSH | (num << USB_OTG_GRSTCTL_TXFNUM_Pos);
    while (OTG->GRSTCTL & USB_OTG_GRSTCTL_TXFFLSH) { }
}

static void flush_rx_fifo(void)
{
    OTG->GRSTCTL = USB_OTG_GRSTCTL_RXFFLSH;
    while (OTG->GRSTCTL & USB_OTG_GRSTCTL_RXFFLSH) { }
}

/* ------------------------------------------------------------------------- */
/* Endpoint 0                                                                */
/* ------------------------------------------------------------------------- */
static void ep0_out_arm(void)
{
    OUTEP(0)->DOEPTSIZ = (3U << USB_OTG_DOEPTSIZ_STUPCNT_Pos)
                       | (1U << USB_OTG_DOEPTSIZ_PKTCNT_Pos) | EP0_SIZE;
    OUTEP(0)->DOEPCTL |= USB_OTG_DOEPCTL_CNAK | USB_OTG_DOEPCTL_EPENA;
}

static void ep0_send(const uint8_t *data, uint32_t len)
{
    uint32_t pkts;

    if (len > s_setup.s.wLength) len = s_setup.s.wLength;
    pkts = (len + EP0_SIZE - 1U) / EP0_SIZE;
    /* a short answer that is a multiple of 64 needs a zero length packet */
    if (len == 0U || (len < s_setup.s.wLength && (len % EP0_SIZE) == 0U)) pkts++;

    INEP(0)->DIEPTSIZ = (pkts << USB_OTG_DIEPTSIZ_PKTCNT_Pos) | len;
    INEP(0)->DIEPCTL |= USB_OTG_DIEPCTL_CNAK | USB_OTG_DIEPCTL_EPENA;
    if (len != 0U) fifo_write(0U, data, len);
}

static void ep0_zlp(void)
{
    INEP(0)->DIEPTSIZ = 1U << USB_OTG_DIEPTSIZ_PKTCNT_Pos;
    INEP(0)->DIEPCTL |= USB_OTG_DIEPCTL_CNAK | USB_OTG_DIEPCTL_EPENA;
}

static void ep0_stall(void)
{
    INEP(0)->DIEPCTL  |= USB_OTG_DIEPCTL_STALL;
    OUTEP(0)->DOEPCTL |= USB_OTG_DOEPCTL_STALL;
}

/* ------------------------------------------------------------------------- */
/* CDC data endpoints                                                        */
/* ------------------------------------------------------------------------- */
static uint32_t rx_free(void)
{
    return RX_RING_SIZE - 1U - ((s_rx_head - s_rx_tail) & (RX_RING_SIZE - 1U));
}

static void data_out_arm(void)
{
    OUTEP(CDC_DATA_EP)->DOEPTSIZ = (1U << USB_OTG_DOEPTSIZ_PKTCNT_Pos) | CDC_DATA_SIZE;
    OUTEP(CDC_DATA_EP)->DOEPCTL |= USB_OTG_DOEPCTL_CNAK | USB_OTG_DOEPCTL_EPENA;
}

/* Start the next IN packet if the endpoint is idle (interrupts disabled) */
static void tx_kick(void)
{
    uint8_t pkt[CDC_DATA_SIZE];
    uint32_t n = 0;

    if (!s_configured || s_tx_busy) return;

    while (n < CDC_DATA_SIZE && s_tx_tail != s_tx_head) {
        pkt[n++] = s_tx_ring[s_tx_tail];
        s_tx_tail = (s_tx_tail + 1U) & (TX_RING_SIZE - 1U);
    }
    if (n == 0U && !s_tx_zlp) return;

    /* a full packet without more data must be followed by a ZLP */
    s_tx_zlp = (n == CDC_DATA_SIZE) && (s_tx_tail == s_tx_head);
    s_tx_busy = true;
    INEP(CDC_DATA_EP)->DIEPTSIZ = (1U << USB_OTG_DIEPTSIZ_PKTCNT_Pos) | n;
    INEP(CDC_DATA_EP)->DIEPCTL |= USB_OTG_DIEPCTL_CNAK | USB_OTG_DIEPCTL_EPENA;
    if (n != 0U) fifo_write(CDC_DATA_EP, pkt, n);
}

static void configure_endpoints(void)
{
    INEP(CDC_DATA_EP)->DIEPCTL = USB_OTG_DIEPCTL_USBAEP | (2U << USB_OTG_DIEPCTL_EPTYP_Pos)
                               | (CDC_DATA_EP << USB_OTG_DIEPCTL_TXFNUM_Pos)
                               | USB_OTG_DIEPCTL_SD0PID_SEVNFRM | CDC_DATA_SIZE;
    OUTEP(CDC_DATA_EP)->DOEPCTL = USB_OTG_DOEPCTL_USBAEP | (2U << USB_OTG_DOEPCTL_EPTYP_Pos)
                                | USB_OTG_DOEPCTL_SD0PID_SEVNFRM | CDC_DATA_SIZE;
    INEP(CDC_CMD_EP)->DIEPCTL  = USB_OTG_DIEPCTL_USBAEP | (3U << USB_OTG_DIEPCTL_EPTYP_Pos)
                               | (CDC_CMD_EP << USB_OTG_DIEPCTL_TXFNUM_Pos)
                               | USB_OTG_DIEPCTL_SD0PID_SEVNFRM | CDC_CMD_SIZE;
    DEV->DAINTMSK |= (1UL << CDC_DATA_EP) | (1UL << (16U + CDC_DATA_EP));

    s_tx_busy = false;
    s_tx_zlp = false;
    s_rx_blocked = false;
    data_out_arm();
}

/* ------------------------------------------------------------------------- */
/* Control requests                                                          */
/* ------------------------------------------------------------------------- */
static uint32_t make_string(uint8_t index)
{
    const char *s = 0;
    char serial[25];
    uint32_t i;
    uint32_t n;

    if (index == 0U) {
        s_desc_buf[0] = 4; s_desc_buf[1] = 0x03; s_desc_buf[2] = 0x09; s_desc_buf[3] = 0x04;
        return 4U;
    }
    if (index == 1U) s = s_str_manuf;
    else if (index == 2U) s = s_str_product;
    else if (index == 3U) {
        /* 96-bit unique device ID as hex */
        static const char hex[] = "0123456789ABCDEF";
        const uint8_t *uid = (const uint8_t *)UID_BASE;
        for (i = 0; i < 12U; i++) {
            serial[i * 2U]      = hex[uid[11U - i] >> 4];
            serial[i * 2U + 1U] = hex[uid[11U - i] & 0x0FU];
        }
        serial[24] = '\0';
        s = serial;
    } else {
        return 0U;
    }

    n = (uint32_t)strlen(s);
    if (n > 32U) n = 32U;
    s_desc_buf[0] = (uint8_t)(2U + n * 2U);
    s_desc_buf[1] = 0x03;
    for (i = 0; i < n; i++) {
        s_desc_buf[2U + i * 2U] = (uint8_t)s[i];
        s_desc_buf[3U + i * 2U] = 0U;
    }
    return 2U + n * 2U;
}

static void handle_standard(const setup_t *rq)
{
    static uint8_t buf[2];
    uint32_t len;

    switch (rq->bRequest) {
    case 0x06:  /* GET_DESCRIPTOR */
        switch (rq->wValue >> 8) {
        case 1: ep0_send(s_dev_desc, sizeof(s_dev_desc)); return;
        case 2: ep0_send(s_cfg_desc, sizeof(s_cfg_desc)); return;
        case 3:
            len = make_string((uint8_t)(rq->wValue & 0xFFU));
            if (len != 0U) { ep0_send(s_desc_buf, len); return; }
            break;
        default: break;
        }
        ep0_stall();
        return;

    case 0x05:  /* SET_ADDRESS: applied immediately, status stage uses it */
        DEV->DCFG = (DEV->DCFG & ~USB_OTG_DCFG_DAD) | ((uint32_t)(rq->wValue & 0x7FU) << USB_OTG_DCFG_DAD_Pos);
        ep0_zlp();
        return;

    case 0x09:  /* SET_CONFIGURATION */
        if (rq->wValue == 1U) {
            configure_endpoints();
            s_configured = true;
        } else {
            s_configured = false;
        }
        ep0_zlp();
        return;

    case 0x08:  /* GET_CONFIGURATION */
        buf[0] = s_configured ? 1U : 0U;
        ep0_send(buf, 1U);
        return;

    case 0x00:  /* GET_STATUS */
        buf[0] = 0U; buf[1] = 0U;
        ep0_send(buf, 2U);
        return;

    case 0x0A:  /* GET_INTERFACE */
        buf[0] = 0U;
        ep0_send(buf, 1U);
        return;

    case 0x01:  /* CLEAR_FEATURE */
        if ((rq->bmRequestType & 0x1FU) == 2U && rq->wValue == 0U) {
            /* ENDPOINT_HALT: clear stall, reset data toggle */
            uint32_t ep = rq->wIndex & 0x0FU;
            if (ep != 0U && ep < 4U) {
                if (rq->wIndex & 0x80U) {
                    INEP(ep)->DIEPCTL = (INEP(ep)->DIEPCTL & ~USB_OTG_DIEPCTL_STALL) | USB_OTG_DIEPCTL_SD0PID_SEVNFRM;
                } else {
                    OUTEP(ep)->DOEPCTL = (OUTEP(ep)->DOEPCTL & ~USB_OTG_DOEPCTL_STALL) | USB_OTG_DOEPCTL_SD0PID_SEVNFRM;
                }
            }
        }
        ep0_zlp();
        return;

    case 0x03:  /* SET_FEATURE */
    case 0x0B:  /* SET_INTERFACE */
        ep0_zlp();
        return;

    default:
        ep0_stall();
        return;
    }
}

static void handle_class(const setup_t *rq)
{
    switch (rq->bRequest) {
    case 0x20:  /* SET_LINE_CODING: 7 data bytes follow */
        s_ep0_pending = 0x20U;
        s_ep0_rx_len = 0U;
        return;
    case 0x21:  /* GET_LINE_CODING */
        ep0_send(s_line_coding, sizeof(s_line_coding));
        return;
    case 0x22:  /* SET_CONTROL_LINE_STATE */
        s_dtr = (rq->wValue & 1U) != 0U;
        ep0_zlp();
        return;
    case 0x23:  /* SEND_BREAK */
        ep0_zlp();
        return;
    default:
        ep0_stall();
        return;
    }
}

static void handle_setup(void)
{
    setup_t rq = s_setup.s;
    s_ep0_pending = 0U;

    switch ((rq.bmRequestType >> 5) & 3U) {
    case 0:  handle_standard(&rq); break;
    case 1:  handle_class(&rq);    break;
    default: ep0_stall();          break;
    }
}

/* OUT transfer on EP0 finished: data stage of a class request or a status ZLP */
static void ep0_out_done(void)
{
    if (s_ep0_pending == 0x20U) {
        if (s_ep0_rx_len >= sizeof(s_line_coding)) {
            memcpy(s_line_coding, s_ep0_buf, sizeof(s_line_coding));
        }
        s_ep0_pending = 0U;
        ep0_zlp();
    }
}

/* ------------------------------------------------------------------------- */
/* Interrupt                                                                 */
/* ------------------------------------------------------------------------- */
static void bus_reset(void)
{
    uint32_t i;

    DEV->DCTL &= ~USB_OTG_DCTL_RWUSIG;
    flush_tx_fifo(0x10U);
    for (i = 0; i < 4U; i++) {
        INEP(i)->DIEPINT  = 0xFFU;
        OUTEP(i)->DOEPINT = 0xFFU;
        if (i != 0U) {
            INEP(i)->DIEPCTL  &= ~USB_OTG_DIEPCTL_USBAEP;
            OUTEP(i)->DOEPCTL &= ~USB_OTG_DOEPCTL_USBAEP;
        }
    }
    DEV->DAINT    = 0xFFFFFFFFUL;
    DEV->DAINTMSK = (1UL << 0) | (1UL << 16);
    DEV->DOEPMSK  = USB_OTG_DOEPMSK_STUPM | USB_OTG_DOEPMSK_XFRCM;
    DEV->DIEPMSK  = USB_OTG_DIEPMSK_XFRCM;
    DEV->DCFG    &= ~USB_OTG_DCFG_DAD;

    s_configured = false;
    s_dtr = false;
    s_tx_busy = false;
    s_tx_zlp = false;
    s_tx_head = s_tx_tail = 0U;
    s_rx_head = s_rx_tail = 0U;
    ep0_out_arm();
}

static void rx_fifo_level(void)
{
    uint32_t st = OTG->GRXSTSP;
    uint32_t ep = st & USB_OTG_GRXSTSP_EPNUM;
    uint32_t bcnt = (st & USB_OTG_GRXSTSP_BCNT) >> USB_OTG_GRXSTSP_BCNT_Pos;
    uint32_t pktsts = (st & USB_OTG_GRXSTSP_PKTSTS) >> USB_OTG_GRXSTSP_PKTSTS_Pos;
    uint8_t tmp[CDC_DATA_SIZE];
    uint32_t i;

    if (pktsts == PKTSTS_SETUP_DATA) {
        s_setup.w[0] = FIFO(0);
        s_setup.w[1] = FIFO(0);
    } else if (pktsts == PKTSTS_OUT_DATA && bcnt != 0U) {
        if (ep == 0U) {
            if (bcnt > EP0_SIZE) bcnt = EP0_SIZE;
            fifo_read(s_ep0_buf, bcnt);
            s_ep0_rx_len = bcnt;
        } else if (ep == CDC_DATA_EP && bcnt <= CDC_DATA_SIZE) {
            fifo_read(tmp, bcnt);
            for (i = 0; i < bcnt; i++) {
                uint32_t next = (s_rx_head + 1U) & (RX_RING_SIZE - 1U);
                if (next == s_rx_tail) break;           /* cannot happen, see arm */
                s_rx_ring[s_rx_head] = tmp[i];
                s_rx_head = next;
            }
        } else {
            fifo_discard(bcnt);
        }
    }
}

void OTG_FS_IRQHandler(void)
{
    uint32_t gint = OTG->GINTSTS & OTG->GINTMSK;
    uint32_t daint;
    uint32_t epint;

    if (gint & USB_OTG_GINTSTS_USBRST) {
        OTG->GINTSTS = USB_OTG_GINTSTS_USBRST;
        bus_reset();
    }

    if (gint & USB_OTG_GINTSTS_ENUMDNE) {
        OTG->GINTSTS = USB_OTG_GINTSTS_ENUMDNE;
        INEP(0)->DIEPCTL &= ~USB_OTG_DIEPCTL_MPSIZ;     /* 64 bytes */
        DEV->DCTL |= USB_OTG_DCTL_CGINAK;
        ep0_out_arm();
    }

    while (OTG->GINTSTS & USB_OTG_GINTSTS_RXFLVL) {
        rx_fifo_level();
    }

    if (gint & USB_OTG_GINTSTS_OEPINT) {
        daint = DEV->DAINT & DEV->DAINTMSK;
        if (daint & (1UL << 16)) {
            epint = OUTEP(0)->DOEPINT;
            OUTEP(0)->DOEPINT = epint;
            if (epint & USB_OTG_DOEPINT_XFRC) {
                ep0_out_done();
                ep0_out_arm();
            }
            if (epint & USB_OTG_DOEPINT_STUP) {
                handle_setup();
                ep0_out_arm();
            }
        }
        if (daint & (1UL << (16U + CDC_DATA_EP))) {
            epint = OUTEP(CDC_DATA_EP)->DOEPINT;
            OUTEP(CDC_DATA_EP)->DOEPINT = epint;
            if (epint & USB_OTG_DOEPINT_XFRC) {
                if (rx_free() >= CDC_DATA_SIZE) data_out_arm();
                else s_rx_blocked = true;               /* NAK until read */
            }
        }
    }

    if (gint & USB_OTG_GINTSTS_IEPINT) {
        daint = DEV->DAINT & DEV->DAINTMSK;
        if (daint & 1UL) {
            epint = INEP(0)->DIEPINT;
            INEP(0)->DIEPINT = epint;
        }
        if (daint & (1UL << CDC_DATA_EP)) {
            epint = INEP(CDC_DATA_EP)->DIEPINT;
            INEP(CDC_DATA_EP)->DIEPINT = epint;
            if (epint & USB_OTG_DIEPINT_XFRC) {
                s_tx_busy = false;
                tx_kick();
            }
        }
    }

    if (gint & USB_OTG_GINTSTS_USBSUSP) {
        OTG->GINTSTS = USB_OTG_GINTSTS_USBSUSP;
    }
    if (gint & USB_OTG_GINTSTS_WKUINT) {
        OTG->GINTSTS = USB_OTG_GINTSTS_WKUINT;
    }
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */
void usb_cdc_init(void)
{
    uint32_t i;

    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
    (void)RCC->AHB2ENR;

    /* PA11 = DM, PA12 = DP */
    gpio_speed(GPIOA, 11U, GPIO_SPEED_HIGH);
    gpio_speed(GPIOA, 12U, GPIO_SPEED_HIGH);
    gpio_af(GPIOA, 11U, 10U);
    gpio_af(GPIOA, 12U, 10U);

    /* Core reset */
    OTG->GAHBCFG = 0U;
    OTG->GUSBCFG |= USB_OTG_GUSBCFG_PHYSEL;
    while (!(OTG->GRSTCTL & USB_OTG_GRSTCTL_AHBIDL)) { }
    OTG->GRSTCTL |= USB_OTG_GRSTCTL_CSRST;
    while (OTG->GRSTCTL & USB_OTG_GRSTCTL_CSRST) { }
    for (i = 0; i < 100U; i++) __NOP();

    /* Embedded FS PHY on, no VBUS sensing */
    OTG->GCCFG = USB_OTG_GCCFG_PWRDWN | USB_OTG_GCCFG_NOVBUSSENS;

    /* Forced device mode, turnaround time for AHB > 32 MHz */
    OTG->GUSBCFG = (OTG->GUSBCFG & ~(USB_OTG_GUSBCFG_TRDT | USB_OTG_GUSBCFG_FHMOD))
                 | USB_OTG_GUSBCFG_FDMOD | USB_OTG_GUSBCFG_PHYSEL
                 | (6U << USB_OTG_GUSBCFG_TRDT_Pos);
    sys_delay_ms(30U);

    PCGCCTL = 0U;
    DEV->DCTL |= USB_OTG_DCTL_SDIS;                         /* disconnected   */
    DEV->DCFG = (DEV->DCFG & ~(USB_OTG_DCFG_DSPD | USB_OTG_DCFG_DAD))
              | USB_OTG_DCFG_DSPD;                          /* full speed     */

    /* FIFO RAM (320 words): RX 128, EP0 TX 64, EP1 TX 64, EP2 TX 16 */
    OTG->GRXFSIZ = 128U;
    OTG->DIEPTXF0_HNPTXFSIZ = (64UL << 16) | 128U;
    OTG->DIEPTXF[0] = (64UL << 16) | 192U;                  /* EP1 IN */
    OTG->DIEPTXF[1] = (16UL << 16) | 256U;                  /* EP2 IN */
    flush_tx_fifo(0x10U);
    flush_rx_fifo();

    DEV->DIEPMSK = 0U;
    DEV->DOEPMSK = 0U;
    DEV->DAINTMSK = 0U;
    DEV->DAINT = 0xFFFFFFFFUL;
    for (i = 0; i < 4U; i++) {
        INEP(i)->DIEPINT  = 0xFFU;
        OUTEP(i)->DOEPINT = 0xFFU;
    }

    OTG->GINTSTS = 0xFFFFFFFFUL;
    OTG->GINTMSK = USB_OTG_GINTMSK_USBRST | USB_OTG_GINTMSK_ENUMDNEM
                 | USB_OTG_GINTMSK_RXFLVLM | USB_OTG_GINTMSK_IEPINT
                 | USB_OTG_GINTMSK_OEPINT | USB_OTG_GINTMSK_USBSUSPM
                 | USB_OTG_GINTMSK_WUIM;
    OTG->GAHBCFG = USB_OTG_GAHBCFG_GINT;

    NVIC_SetPriority(OTG_FS_IRQn, IRQ_PRIO_USB);
    NVIC_EnableIRQ(OTG_FS_IRQn);

    DEV->DCTL &= ~USB_OTG_DCTL_SDIS;                        /* connect (D+ pull-up) */
}

bool usb_cdc_configured(void)
{
    return s_configured;
}

bool usb_cdc_dtr(void)
{
    return s_dtr;
}

uint32_t usb_cdc_tx_free(void)
{
    return TX_RING_SIZE - 1U - ((s_tx_head - s_tx_tail) & (TX_RING_SIZE - 1U));
}

uint32_t usb_cdc_write(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t n = 0;
    uint32_t st;

    if (!s_configured) return 0U;

    st = sys_irq_save();
    while (n < len) {
        uint32_t next = (s_tx_head + 1U) & (TX_RING_SIZE - 1U);
        if (next == s_tx_tail) break;
        s_tx_ring[s_tx_head] = p[n++];
        s_tx_head = next;
    }
    tx_kick();
    sys_irq_restore(st);
    return n;
}

uint32_t usb_cdc_read(void *data, uint32_t max)
{
    uint8_t *p = (uint8_t *)data;
    uint32_t n = 0;
    uint32_t st;

    while (n < max && s_rx_tail != s_rx_head) {
        p[n++] = s_rx_ring[s_rx_tail];
        s_rx_tail = (s_rx_tail + 1U) & (RX_RING_SIZE - 1U);
    }

    st = sys_irq_save();
    if (s_rx_blocked && s_configured && rx_free() >= CDC_DATA_SIZE) {
        s_rx_blocked = false;
        data_out_arm();
    }
    sys_irq_restore(st);
    return n;
}
