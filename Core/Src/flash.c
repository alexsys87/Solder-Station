/**
 * @file    flash.c
 * @brief   Flash programming with 32-bit parallelism (VDD 2.7..3.6 V).
 *
 * The CPU stalls while the flash is busy (single bank), interrupts are
 * delayed too. The caller must switch the heater off before erasing.
 */
#include "flash.h"
#include "board.h"
#include "sys.h"

#define FLASH_KEY1      0x45670123UL
#define FLASH_KEY2      0xCDEF89ABUL
#define FLASH_ERR_MASK  (FLASH_SR_PGSERR | FLASH_SR_PGPERR | FLASH_SR_PGAERR \
                         | FLASH_SR_WRPERR | FLASH_SR_OPERR)

static void flash_unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
}

static void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

/* The ART accelerator caches may hold stale copies of modified lines */
static void flash_cache_flush(void)
{
    uint32_t acr = FLASH->ACR;
    FLASH->ACR = acr & ~(FLASH_ACR_DCEN | FLASH_ACR_ICEN);
    FLASH->ACR |= FLASH_ACR_DCRST | FLASH_ACR_ICRST;
    FLASH->ACR &= ~(FLASH_ACR_DCRST | FLASH_ACR_ICRST);
    FLASH->ACR = acr;
}

static bool flash_wait(void)
{
    while (FLASH->SR & FLASH_SR_BSY) { }
    return (FLASH->SR & FLASH_ERR_MASK) == 0U;
}

bool flash_erase_sector(uint32_t sector)
{
    bool ok;

    sys_wdg_feed();
    flash_unlock();
    while (FLASH->SR & FLASH_SR_BSY) { }
    FLASH->SR = FLASH_ERR_MASK | FLASH_SR_EOP;

    FLASH->CR = (FLASH->CR & ~(FLASH_CR_PSIZE | FLASH_CR_SNB))
              | FLASH_CR_PSIZE_1                        /* x32 */
              | (sector << FLASH_CR_SNB_Pos)
              | FLASH_CR_SER;
    FLASH->CR |= FLASH_CR_STRT;
    ok = flash_wait();
    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);
    flash_lock();
    flash_cache_flush();
    sys_wdg_feed();
    return ok;
}

bool flash_program(uint32_t addr, const void *data, uint32_t len_words)
{
    const uint32_t *src = (const uint32_t *)data;
    volatile uint32_t *dst = (volatile uint32_t *)addr;
    uint32_t i;
    bool ok = true;

    flash_unlock();
    while (FLASH->SR & FLASH_SR_BSY) { }
    FLASH->SR = FLASH_ERR_MASK | FLASH_SR_EOP;

    FLASH->CR = (FLASH->CR & ~FLASH_CR_PSIZE) | FLASH_CR_PSIZE_1 | FLASH_CR_PG;
    for (i = 0; i < len_words; i++) {
        dst[i] = src[i];
        if (!flash_wait()) {
            ok = false;
            break;
        }
    }
    FLASH->CR &= ~FLASH_CR_PG;
    flash_lock();
    flash_cache_flush();

    /* Verify after the cache flush */
    for (i = 0; ok && i < len_words; i++) {
        if (dst[i] != src[i]) ok = false;
    }
    return ok;
}
