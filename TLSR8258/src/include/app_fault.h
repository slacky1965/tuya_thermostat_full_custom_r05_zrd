#ifndef TLSR8258_SRC_INCLUDE_APP_FAULT_H_
#define TLSR8258_SRC_INCLUDE_APP_FAULT_H_

#include "tl_common.h"

extern u16 zcl_attr_alarmCount;

void App_Fault_Init(void);
void App_Link_FaultSet(uint16_t mask);
void App_Fault_Tick(void);

#endif
