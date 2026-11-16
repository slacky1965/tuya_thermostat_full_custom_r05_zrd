#ifndef CONFIG_H
#define CONFIG_H
/**************************************************************************************************/
/* CONFIG.H - single place for all board/chip settings.                                           */
/*                                                                                                */
/* Section:                                                                                       */
/*   1. Device identity: model string, firmware version, chip id                                  */
/*   2. System clock source and frequency                                                         */
/*   3. UART links: enable switches, baudrates, buffer sizes, pins                                */
/*   4. GPIO map: full pin assignment of this board (reference)                                   */
/*   5. REG-COM LED matrix: brightness, buffer size                                               */
/*   6. Heating relay                                                                             */
/*   7. NTC inputs                                                                                */
/*   8. Optional code switches (ROM size)                                                         */
/*                                                                                                */
/* The vendor SFR/GPIO bit registers (ca51f2sfr.h, gpiodef_f2.h) stay in the                      */
/* SDK headers; this file only holds project-level choices and the pin map.                       */
/* Include config.h FIRST in every .c file, before module headers.                                */
/**************************************************************************************************/

#define FLASH_TOTAL_SIZE       (32UL * 1024UL)
#define FLASH_DATA_SIZE        512
#define FLASH_CODE_SIZE        32256
#define FLASH_PADRD            0x7E
#if (FLASH_CODE_SIZE + FLASH_DATA_SIZE != FLASH_TOTAL_SIZE) || \
    (FLASH_PADRD * 256 != FLASH_CODE_SIZE)
#error Flash layout constants are inconsistent
#endif

/*** 1. Device identity (reported via LNK_CMD_INFO link handshake) ********************************/
/* Model string is sent without a terminator; keep it <= 16 chars so the                          */
/* Info frame stays within LNK_INFO_MAX_LEN (see link_proto.h).                                   */
#define DEVICE_MODEL        "osrctherm_r05" /* name, revision suffix _rNN                         */
#define APP_VERSION         0x101           /* firmware version, BCD u16 BE                       */
                                            /* (hi byte = major, lo byte = minor; 0x100 = v1.00)  */
#define CHIP_ID             0x20A0          /* hardware chip ID (u16 BE), CACHIP                  */
                                            /* 0x20A0 = CA51F253L3; exact ID in flash 0x2B area   */

/*** 2. System clock ******************************************************************************/
#define IRCH                0
#define IRCL                1
#define PLL                 2
#define XOSCL               3

#define SYSCLK_SRC          IRCH  /* chip system clock select                 */
                                  /* (IRCH = 0, IRCL = 1, PLL = 2, XOSCL = 3) */
#if (SYSCLK_SRC == PLL)
    #define PLL_Multiple    6                          /* PLL multiplier    */
    #define FOSC            (3686400L * PLL_Multiple)  /* synthesized clock */
#else
    #define FOSC            (3686400L)  /* IRCH clock */
#endif

/*** 3. UART links ********************************************************************************/
/* UART1 = ZT3L link bus, TX = P6.6, RX = P6.7, 115200 8N1                                      */
#define UART1_EN
#define UART1_BAUTRATE      115200

/* UART0 = debug console (polled TX P3.0/P3.1). UART2 = spare.                                    */
/*#define UART0_EN*/  /* uncomment to enable UART0                                                */
/*#define UART2_EN*/  /* uncomment to enable UART2                                                */
/*#define PRINT_EN*/  /* vendor uart_printf() helper (UART0/1/2)                                  */

#ifdef UART0_EN
    #define UART0_BAUTRATE   115200
#endif
#ifdef UART2_EN
    #define UART2_BAUTRATE   115200
#endif
#if SYSCLK_SRC == PLL
    #define UART1_RELOAD_VALUE     0x03FA
    #define UART0_RELOAD_FALLBACK  0xFFFA
    #define UART2_RELOAD_FALLBACK  0x03FA
#else
    #define UART1_RELOAD_VALUE     0x03FF
    #define UART0_RELOAD_FALLBACK  0xFFFF
    #define UART2_RELOAD_FALLBACK  0x03FF
#endif

/* RX/TX ring buffer sizes (bytes each)                                                           */
#define UART0_TX_BUF_SIZE   20
#define UART0_RX_BUF_SIZE   100
#define UART1_TX_BUF_SIZE   128
#define UART1_RX_BUF_SIZE   100
#define UART2_TX_BUF_SIZE   20
#define UART2_RX_BUF_SIZE   100

/*** 4. GPIO map (this board) *********************************************************************/
/*                                                                                                */
/* LED matrix (REG-COM, internal driver in LED mode):                                             */
/*   COM0..COM7  = P0.0..P0.7 (pins 61..54), high sink current                                    */
/*   SEG12..SEG19 = P6.2..P6.5 + P1.0..P1.3 (pins 34..41)                                         */
/* Direct LEDs:                                                                                   */
/*   P5.7/P3.4/P3.5/P5.6/P5.0/P5.1/P5.2 = green weekdays Mon..Sun, active high                    */
/*   P5.5 = green weekday common, P5.4 = red flame                                                */
/* Keys (r05): SC05B parallel outputs, active low, internal pull-ups:                             */
/*   P3.7 = Power, P3.6 = Menu, P3.3 = Up, P3.2 = Down (confirmed by bench log).                  */
/*   P4.7 is not used by SC05B on r05.                                                            */
/* NTC sensors (ADC inputs, always powered; read on demand):                                      */
/*   TEMP_INTERNAL = AD_CH[0] = P4.0 (chip pin 21)                                                */
/*   TEMP_EXTERNAL = AD_CH[1] = P4.1 (chip pin 20)                                                */
/* Heating relay:                                                                                 */
/*   RELAY = P4.4 (chip pin 17), high = ON, low = OFF                                             */
/* UART0 debug: TX = P3.0, RX = P3.1 (polled)                                                     */
/* UART1 link:  TX = P6.6, RX = P6.7                                                              */
/* UART2 spare: TX = P6.1, RX = P6.0                                                              */

/*** 5. REG-COM LED matrix ************************************************************************/
#define LDRV_DEFAULT        7   /* startup display brightness, 0..7 */
#define LED_BUF_N           8   /* shadow buffer bytes (INDEX positions) */

/*** 6. Heating relay *****************************************************************************/
/* Relay driver on P4.4 (chip pin 17, r05):                                                       */
/*   high level = relay ON, low level = relay OFF.                                                */
/* The r05 stock firmware drives P4.4; P4.3 is not the heating relay on this board.               */
#define RELAY_PIN           P44
#define RELAY_FUNC          P44F
#define RELAY_ON_LEVEL      1
#define RELAY_OFF_LEVEL     0
/* Control: symmetric hysteresis, heater ON when t <= SP - deadBand, OFF when     */
/* t >= SP + deadBand. OFF is always immediate; the only time restriction is the  */
/* minimum time spent OFF (anti-short-cycle start protection): the ON condition   */
/* is ignored until the relay has been off for RELAY_OFF_WINDOW_SECS.             */
#define RELAY_OFF_WINDOW_SECS 30

/* Emergency cut-off ceiling = maxHeatSetpointLimit + ABS_MAX_DEADBAND + margin   */
/* (c100). The margin is measured from the HIGHEST legal OFF point of the law     */
/* (SP + deadBand <= maxHeatSetpointLimit + ABS_MAX_DEADBAND), so the cut-off     */
/* never fires during normal operation - it only reacts to a genuine runaway.     */
#define RELAY_TRIP_MARGIN_C100 100

/* Economy mode (ZCL ProgrammingOperationMode bit 2): fixed control-only         */
/* setpoint offset in c100, subtracted from the active setpoint BEFORE the       */
/* [min..max] clamp, so Eco still respects the user limits. The stored setpoint  */
/* and the schedule slots never change (data != policy). Placeholder for a       */
/* future setpoint-dependent formula (bigger setpoint -> bigger offset).         */
#define ECO_SETPOINT_OFFSET_C100 300

/* Wireless climate sensor (WS) freshness window: while the last WS temperature   */
/* report is younger than this, the WS sensor drives BOTH the relay and the big   */
/* digits; after that the thermostat falls back to the configured probes          */
/* (IN/OU/AL) and BOTH WS values count as invalid. Seconds.                       */
#define WS_FRESH_SECS         300

/* AL "Er" alternation: with one probe of the AL pair dead the big digits       */
/* alternate between the survivor's temperature and "Er" so the working value   */
/* stays visible. Phase lengths in 100 ms ticks (main-loop grid).               */
#define TEMP_ER_TEMP_10MS_TICKS 500    /* temperature phase, 5.0 s              */
#define TEMP_ER_WARN_10MS_TICKS 150    /* "Er" phase, 1.5 s                     */

/* After an invalid NTC read (short/open/timeout) the input capacitor needs time
   to settle. Hold the last fault and pipeline for this many seconds of stable
   OK before resuming temperature/relay processing. Seconds. */
#define TEMP_SETTLE_SECS      20

/*** 7. NTC / relay notes *******************************************************   */
/* Both NTCs are plain ADC inputs (no external-NTC power switch). P4.4 is the     */
/* heating relay on r05, so Temp_Init() must not park P44F in HIGH_Z.             */

/*** 8. Optional code switches (ROM size) ***********************************************************/
/* The linker does no dead-code elimination, so helpers that nothing calls still occupy ROM.        */
#define SYSCLK_EXTRA_FUNCS  0   /* Sys_Clk_Set_IRCL/XOSCL/PLL/TFRC/XOSCH helpers                    */
                                /* (the real crystal wait/fallback lives in RTC_Init, rtc.c)        */
#define SENSOR_LEGACY_READ  0   /* Temp_ReadC100() wrapper, superseded by Temp_ReadC100Checked()    */
#define FACTORY_RESET_TEST_EN 0 /* 1 = OFF+DOWN only BLINKS the NET symbol: no LNK_CMD_FACTORY_     */
                                /* RESET (0x27) is sent and the steady-NET guard in Net_Flash() is   */
                                /* lifted, so the gesture works while the module STAYS joined.      */
                                /* 0 = full provisioning functionality (send + steady guard).        */
/****************************************************************************************************/

/*** 9. Bench tests (compile-time) *****************************************************************/
#define KEY_TEST_EN     0   /* 1 = main() runs Key_TestMain(); normal UI never starts           */
#define LED_TEST_EN     0   /* 1 = main() runs Led_TestMain(); normal UI never starts           */
#define LED_GPIO_TEST_EN 0  /* 1 = main() runs Led_Gpio_TestMain(); normal UI never starts      */
#define LED_PWM_TEST_EN 0   /* 1 = main() runs Led_Pwm_TestMain(); normal UI never starts       */
/****************************************************************************************************/
#endif
