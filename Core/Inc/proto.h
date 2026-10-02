/**
 * @file    proto.h
 * @brief   Text control protocol, served on USB CDC and on USART1.
 *          See README ("Протокол") for the command list.
 */
#ifndef PROTO_H
#define PROTO_H

void proto_init(void);
void proto_task(void);

/* Jump to the STM32 system bootloader if requested before the last reset.
 * Must be called first thing in main(), before any clock setup. */
void proto_check_bootloader(void);

#endif /* PROTO_H */
