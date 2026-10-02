/**
 * @file    flash.h
 * @brief   Internal flash erase / program (register level).
 */
#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>
#include <stdbool.h>

/* Settings area: sectors 1 and 2, 16 KB each (reserved in the linker file) */
#define FLASH_SETTINGS_SECTOR_A     1U
#define FLASH_SETTINGS_ADDR_A       0x08004000UL
#define FLASH_SETTINGS_SECTOR_B     2U
#define FLASH_SETTINGS_ADDR_B       0x08008000UL
#define FLASH_SETTINGS_SECTOR_SIZE  0x4000UL

bool flash_erase_sector(uint32_t sector);
bool flash_program(uint32_t addr, const void *data, uint32_t len_words);

#endif /* FLASH_H */
