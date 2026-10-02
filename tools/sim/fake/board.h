/* Host stand-in for board.h: just enough for proto.c in the simulator */
#ifndef BOARD_H
#define BOARD_H
#include <stdint.h>
#include <stdbool.h>
typedef struct { volatile uint32_t APB1ENR, APB2ENR; } RCC_TypeDef;
typedef struct { volatile uint32_t CR; } PWR_TypeDef;
typedef struct { volatile uint32_t BKP1R; } RTC_TypeDef;
typedef struct { volatile uint32_t MEMRMP; } SYSCFG_TypeDef;
extern RCC_TypeDef sim_rcc; extern PWR_TypeDef sim_pwr; extern RTC_TypeDef sim_rtc; extern SYSCFG_TypeDef sim_syscfg;
#define RCC (&sim_rcc)
#define PWR (&sim_pwr)
#define RTC (&sim_rtc)
#define SYSCFG (&sim_syscfg)
#define RCC_APB1ENR_PWREN 1U
#define RCC_APB2ENR_SYSCFGEN 1U
#define PWR_CR_DBP 1U
extern uint8_t sim_uid[12];
#define UID_BASE ((uintptr_t)sim_uid)
void sim_reset(void);
#define NVIC_SystemReset() sim_reset()
#define __WFI() ((void)0)
#define __set_MSP(x) ((void)(x))
#endif
