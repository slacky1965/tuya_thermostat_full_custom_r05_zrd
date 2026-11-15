#ifndef _HW_TEST_H_
#define _HW_TEST_H_
#include "include/config.h"

#if KEY_TEST_EN
void Key_TestMain(void);              /* key bench; never returns */
#endif

#if LED_TEST_EN
void Led_TestMain(void);              /* LED matrix mapping bench; never returns */
#endif

#if LED_GPIO_TEST_EN
void Led_Gpio_TestMain(void);         /* direct GPIO LED mapping bench; never returns */
#endif

#endif
