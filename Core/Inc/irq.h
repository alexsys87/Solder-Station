/**
 * @file    irq.h
 * @brief   Prototypes of the interrupt handlers implemented by the firmware.
 *          The names must match the vector table in the startup file.
 */
#ifndef IRQ_H
#define IRQ_H

void SysTick_Handler(void);             /* sys.c      */
void DMA2_Stream0_IRQHandler(void);     /* heater.c   */
void ADC_IRQHandler(void);              /* heater.c   */
void DMA2_Stream3_IRQHandler(void);     /* oled.c     */
void DMA2_Stream7_IRQHandler(void);     /* uart.c     */
void USART1_IRQHandler(void);           /* uart.c     */
void EXTI9_5_IRQHandler(void);          /* motion.c   */
void OTG_FS_IRQHandler(void);           /* usb_cdc.c  */

#endif /* IRQ_H */
