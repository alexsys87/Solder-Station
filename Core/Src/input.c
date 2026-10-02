/**
 * @file    input.c
 * @brief   Encoder on TIM4 (x4 encoder mode, digital input filter) and the
 *          encoder push button sampled every 1 ms with debouncing.
 */
#include "input.h"
#include "board.h"
#include "config.h"
#include "sys.h"

#define EVQ_SIZE    8U

static volatile int16_t  s_rot;
static volatile int16_t  s_rot_acc;
static volatile int16_t  s_rot_pressed;
static volatile uint32_t s_activity;

static uint16_t s_last_cnt;
static int16_t  s_sub;
static uint32_t s_last_step_ms;
static bool     s_invert;

/* Button state */
static bool     s_btn_stable;
static uint8_t  s_btn_cnt;
static uint32_t s_btn_press_ms;
static uint32_t s_btn_click_ms;
static bool     s_btn_long_sent;
static bool     s_btn_rotated;
static bool     s_btn_click_pending;

static volatile btn_event_t s_evq[EVQ_SIZE];
static volatile uint8_t s_evq_head;
static volatile uint8_t s_evq_tail;

static void evq_push(btn_event_t ev)
{
    uint8_t next = (uint8_t)((s_evq_head + 1U) % EVQ_SIZE);
    if (next != s_evq_tail) {
        s_evq[s_evq_head] = ev;
        s_evq_head = next;
    }
}

void input_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
    (void)RCC->APB1ENR;

    /* Encoder A/B: TIM4_CH1/CH2, pull-ups (the module has its own too) */
    gpio_pull(ENC_PORT, ENC_A_PIN, GPIO_PULL_UP);
    gpio_pull(ENC_PORT, ENC_B_PIN, GPIO_PULL_UP);
    gpio_af(ENC_PORT, ENC_A_PIN, 2U);
    gpio_af(ENC_PORT, ENC_B_PIN, 2U);

    /* Button: plain input with pull-up, pressed = low */
    gpio_pull(ENC_SW_PORT, ENC_SW_PIN, GPIO_PULL_UP);
    gpio_mode(ENC_SW_PORT, ENC_SW_PIN, GPIO_MODE_IN);

    TIM4->CR1   = TIM_CR1_CKD_1;                          /* tDTS = 4 x tCK_INT */
    TIM4->CCMR1 = (1U << TIM_CCMR1_CC1S_Pos) | (1U << TIM_CCMR1_CC2S_Pos)
                | (15U << TIM_CCMR1_IC1F_Pos) | (15U << TIM_CCMR1_IC2F_Pos);
    TIM4->CCER  = 0U;
    TIM4->SMCR  = 3U << TIM_SMCR_SMS_Pos;                 /* encoder mode 3 (x4) */
    TIM4->ARR   = 0xFFFFU;
    TIM4->CNT   = 0U;
    TIM4->CR1  |= TIM_CR1_CEN;

    s_last_cnt = 0U;
    s_btn_stable = false;
}

void input_set_invert(bool invert)
{
    s_invert = invert;
}

static void encoder_step(int dir, uint32_t now)
{
    uint32_t dt = now - s_last_step_ms;
    int factor = 1;

    if (s_invert) dir = -dir;

    if (dt < 30U)       factor = 5;
    else if (dt < 60U)  factor = 2;
    s_last_step_ms = now;

    if (s_btn_stable) {
        s_rot_pressed += (int16_t)dir;
        s_btn_rotated = true;
    } else {
        s_rot     += (int16_t)dir;
        s_rot_acc += (int16_t)(dir * factor);
    }
    s_activity = now;
}

void input_tick_1ms(void)
{
    uint32_t now = sys_ms();
    uint16_t cnt = (uint16_t)TIM4->CNT;
    bool raw;

    /* ---- encoder ---- */
    s_sub += (int16_t)(cnt - s_last_cnt);
    s_last_cnt = cnt;
    while (s_sub >= ENC_COUNTS_PER_DETENT) {
        s_sub -= ENC_COUNTS_PER_DETENT;
        encoder_step(1, now);
    }
    while (s_sub <= -ENC_COUNTS_PER_DETENT) {
        s_sub += ENC_COUNTS_PER_DETENT;
        encoder_step(-1, now);
    }

    /* ---- button debounce ---- */
    raw = !gpio_read(ENC_SW_PORT, ENC_SW_PIN);
    if (raw != s_btn_stable) {
        if (++s_btn_cnt >= BTN_DEBOUNCE_MS) {
            s_btn_cnt = 0;
            s_btn_stable = raw;
            s_activity = now;
            if (raw) {
                /* pressed */
                s_btn_press_ms  = now;
                s_btn_long_sent = false;
                s_btn_rotated   = false;
            } else if (!s_btn_long_sent && !s_btn_rotated) {
                /* released after a short press */
                if (s_btn_click_pending && (now - s_btn_click_ms) < BTN_DOUBLE_MS) {
                    s_btn_click_pending = false;
                    evq_push(BTN_DOUBLE);
                } else {
                    s_btn_click_pending = true;
                    s_btn_click_ms = now;
                    evq_push(BTN_CLICK);
                }
            }
        }
    } else {
        s_btn_cnt = 0;
    }

    if (s_btn_stable && !s_btn_long_sent && !s_btn_rotated
        && (now - s_btn_press_ms) >= BTN_LONG_MS) {
        s_btn_long_sent = true;
        s_btn_click_pending = false;
        evq_push(BTN_LONG);
    }
}

void input_take_rotation(input_rot_t *rot)
{
    uint32_t st = sys_irq_save();
    rot->steps         = s_rot;
    rot->accel         = s_rot_acc;
    rot->pressed_steps = s_rot_pressed;
    s_rot = 0;
    s_rot_acc = 0;
    s_rot_pressed = 0;
    sys_irq_restore(st);
}

btn_event_t input_take_button(void)
{
    btn_event_t ev = BTN_NONE;
    uint32_t st = sys_irq_save();
    if (s_evq_tail != s_evq_head) {
        ev = s_evq[s_evq_tail];
        s_evq_tail = (uint8_t)((s_evq_tail + 1U) % EVQ_SIZE);
    }
    sys_irq_restore(st);
    return ev;
}

bool input_button_down(void)
{
    return s_btn_stable;
}

void input_flush(void)
{
    input_rot_t r;
    input_take_rotation(&r);
    while (input_take_button() != BTN_NONE) { }
}

uint32_t input_last_activity(void)
{
    return s_activity;
}
