#ifndef LED_H
#define LED_H
#include "include/stdint.h"
/********************************************************************************/
/* LED display and direct LEDs.                                                 */
/*                                                                              */
/*  REG-COM matrix (on-board LED driver):                                       */
/*    COM0..COM7 = P0.0..P0.7 (pins 61..54), high sink current (PxxC|=0x80)     */
/*    SEG12..SEG19 = P6.2..P6.5 + P1.0..P1.3 (pins 34..41)                      */
/*    brightness: LDRV[2:0] in LXCFG (8 levels), display buffer via INDEX/LXDAT */
/*                                                                              */
/*  Direct LEDs: P5.4 red flame, P5.5 green weekday common, weekdays active      */
/*  high on P5.7/P3.4/P3.5/P5.6/P5.0/P5.1/P5.2.                                  */
/********************************************************************************/
// LXCON register bits
#define LEN(N)		(N<<5) 	//N=0~6
#define LMOD(N)		(N<<4)	//N=0~1


// LXCFG register bits
#define DMOD(N)		(N<<6)
#define BIAS(N)		(N<<4)

#define LDRV(N)		(N) //N=0-7

#define COMHV(N)	(N<<5)	//N=0~1
#define SEGHV(N)	(N<<4)	//N=0~1
#define BLNK(N)		(N<<3)	//N=0~1

enum 
{
  LEN_DISABLE	= 0,
  LEN_IRCL  	= 1,
  LEN_IRCH  	= 2,
  LEN_XOSCL  	= 3,
  LEN_PLL  		= 5,
  LEN_TF  		= 6,
};

enum 
{
  LMOD_lcd	= 0,
  LMOD_led  = 1,
};

enum 
{
  DMOD_5ua		= 0,
  DMOD_40ua  	= 1,
  DMOD_80ua 	= 2,
  DMOD_130ua 	= 3,
};

enum 
{
  BIAS_1_4		= 0,
  BIAS_1_2  	= 1,
  BIAS_1_3 		= 2,
};

enum 
{
  COM_L  		= 0,
  COM_H		= 1,
};

enum 
{
  SEG_L  		= 0,
  SEG_H		= 1,
};

enum 
{
  LDRV_0		= 0,
  LDRV_1		= 1,
  LDRV_2		= 2,
  LDRV_3		= 3,
  LDRV_4		= 4,
  LDRV_5		= 5,
  LDRV_6		= 6,
  LDRV_7		= 7,
};
/* Unified brightness: level 0 = off, 1-8 = brightness (LDRV 0-7).              */
#define LED_BRIGHTNESS_OFF  0
#define LED_BRIGHTNESS_MAX  8

/* On-screen symbols. One symbol = one LED = one cell (INDEX x COM bit) of the  */
/* r05 REG-COM matrix. The authoritative map is doc_local/hardware_r05.md;      */
/* indexes below ARE the row numbers of sym_map[] in led.c, keep in sync.       */

enum
{
	SYM_HEAT    = 0,  /* red flame             P5.4                           */
	SYM_DEGC    = 1,  /* degree C unit        1.7                             */
	SYM_NET     = 2,  /* network              7.6                             */
	SYM_SP1     = 3,  /* schedule period 1    0.0                             */
	SYM_SP2     = 4,  /* schedule period 2    0.1                             */
	SYM_SP3     = 5,  /* schedule period 3    0.2                             */
	SYM_SP4     = 6,  /* schedule period 4    0.3                             */
	SYM_SP5     = 7,  /* schedule period 5    0.4                             */
	SYM_SP6     = 8,  /* schedule period 6    0.5                             */
	SYM_CLOCK   = 9,  /* schedule mode        7.4                             */
	SYM_HAND    = 10, /* manual mode          7.3                             */
	SYM_HEATER  = 11, /* external probe       7.2                             */
	SYM_SET     = 12, /* Set                  7.1                             */
	SYM_HOME    = 13, /* house+thermometer    7.0                             */
	SYM_LOCK    = 14, /* padlock              4.7                             */
	SYM_ECO     = 15, /* Eco                  7.5                             */
	SYM_COUNT   = 16
};

void LedDrv_Init(void);                        /* REG-COM LEDs: pins + peripheral (all off until LXDAT) */
void Led_SetBrightness(uint8_t level);         /* REG-COM display: 0=off, 1-8=LDRV brightness           */
void Led_BootTest(void);                       /* 3 s all on, then 2 s the version in the big digits    */
/* Shadow-buffer symbol API: draw into led_buf[], Led_Flush() pushes to display */
void Led_SymInit(void);                    /* clear shadow buffer + flush (display off)        */
void Led_SymSet(uint8_t id, uint8_t on);   /* turn symbol id on/off (no flush)                 */
void Led_Flush(void);                      /* write the whole shadow buffer to the display     */
void Led_CellScan(void);                   /* bench: light one cell at a time (map the matrix) */
/* Digit/temperature rendering (7-seg). Digits on the big positions:            */
/* INDEX5 = tens, INDEX6 = units; hundreds '1' = 5.7 + 6.7.                     */
void Led_Digit(uint8_t idx, uint8_t digit);                               /* 7-seg digit 0..9 into buffer (keeps bit7 icons)           */
void Led_Char(uint8_t idx, uint8_t seg7);                                 /* raw 7-seg pattern bits0-6 (bit7 kept)                     */
void Led_Letters(uint8_t tens_seg7, uint8_t units_seg7);                  /* two letters in the big digits                             */
void Led_SensorLetters(uint8_t src);                                      /* IN/OU/AL word, shared table in led.c                      */
void Led_HalfDot(uint8_t on);                                             /* tenths icon at 7.7                                       */
void Led_Weekday(uint8_t week);                                           /* green weekday LED: 1..7 = Mon..Sun, 0 = all off          */
void Led_Clock(uint8_t hour, uint8_t min, uint8_t colon_on);              /* 24-hour clock only                                       */
void Led_Clock_Blank(uint8_t field);                                      /* blank hours(0)/minutes(1) field, keeps ':' and icons     */
void Led_Temp(int value_c100);                                            /* draw temp: two digits + hundreds '1' + .5 + degree C      */
void Led_MenuValue(int16_t value_c100);                                   /* menu value without unit, hundreds, or flush               */
void Led_Temp_Blank(void);                                                /* blank the temperature digits (keeps '.5' / degree C)      */
void Led_TempErr(void);                                                   /* draw "Er" in the big digits (sensor error)                */
void Led_ErrorCode(uint8_t err_code);                                         /* draw E0..E7 in the big digits                              */

#endif
