/**
 * @file    telemetry.h
 * @brief   CSV log of the control loop over USART1 TX (DMA), for PID tuning.
 *
 * Format (115200 8N1), one line per control period:
 *   ms;mode;target;tip;raw;duty%;power;vin;cj
 */
#ifndef TELEMETRY_H
#define TELEMETRY_H

void telemetry_init(void);
void telemetry_task(void);

#endif /* TELEMETRY_H */
