#ifndef _FAULT_H_
#define _FAULT_H_

#include "include/stdint.h"
#include "include/sensor_ntc.h"

/* Internal fault mask bits. E0 is reserved for an unknown/system fault. */
#define FAULT_E0        0x0001u
#define FAULT_E1        0x0002u
#define FAULT_E2        0x0004u
#define FAULT_E3        0x0008u
#define FAULT_E4        0x0010u
#define FAULT_E5        0x0020u
#define FAULT_E6        0x0040u
#define FAULT_E7        0x0080u   /* reserved */
#define FAULT_IN_HI     0x0400u
#define FAULT_IN_LO     0x0800u
#define FAULT_EXT_HI    0x1000u
#define FAULT_EXT_LO    0x2000u

#define FAULT_RANGE_NONE 0
#define FAULT_RANGE_UP   1
#define FAULT_RANGE_LO   2

void Fault_Init(void);
void Fault_UpdateSensor(uint8_t sensor, temp_status_t st, int16_t c100);
uint16_t Fault_Mask(void);
uint8_t Fault_Changed(void);
void Fault_ClearChanged(void);
uint8_t Fault_ReportNeeded(void);
void Fault_ReportDone(void);
void Fault_RequestReport(void);
uint8_t Fault_DisplayCode(void);
uint8_t Fault_DisplayRange(void);
uint8_t Fault_DisplayCodeFor(uint8_t temp_src);
uint8_t Fault_DisplayRangeFor(uint8_t temp_src);

#endif
