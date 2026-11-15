/********************************************************************************/
/* LED module: two families of LED drivers.                                     */
/*                                                                              */
/*  1) REG-COM matrix on the internal LED/LCD driver (LXCON=LED mode):          */
/*     r05: COM0..COM7 = P0.0..P0.7, SEG0..SEG7 = P6.2..P6.5 + P1.0..P1.3.     */
/*     INDEX = position, COM bit = segment (A..G/DP); LDRV[2:0] brightness.     */
/*     Bench recipe: PxxF=3 on COM+SEG, PxxC|=0x80 on COM (high sink current),  */
/*     LXDIV=0, LXCFG=COM_L|SEG_H|BLNK(0)|LDRV(n), LXCON=LEN_IRCH|LMOD_led,     */
/*     buffer writes via INDEX/LXDAT.                                           */
/*                                                                              */
/*  2) Direct LEDs: P5.4 = red flame; P5.5 = green weekday common; the seven    */
/*     weekdays are active high on P5.7/P3.4/P3.5/P5.6/P5.0/P5.1/P5.2.          */
/********************************************************************************/
#include "include/stdint.h"
#include "include/config.h"
#include "include/ca51f2sfr.h"
#include "include/ca51f2xsfr.h"
#include "include/gpiodef_f2.h"
#include "include/delay.h"
#include "include/debug.h"
#include "include/uart.h"                 /* Uart1_Poll: service the peer from    */
                                          /* the boot-test delay slices           */
#include "include/led.h"
#include "include/led_format.h"
#include "include/chars.h"
#include "include/modes.h"          /* lock_flash for the LOCK_TRACE_EN probe */
/*******************************************************************************/
/* REG-COM display shadow buffer (one byte per SEG line; bit n = COM n).       */
/* Software draws symbols here, Led_Flush() pushes the buffer to the driver.   */
static xdata uint8_t led_buf[LED_BUF_N];

/* Symbol map: SYM_* -> (SEG index, COM mask), one row per LED cell.            */
/* Values match the confirmed table in doc_local (LED map).                     */
static code const uint8_t sym_map[SYM_COUNT][2] = {
	{  0, 0x00 },   /* SYM_HEAT   - direct P5.4 flame, handled in Led_SymSet */
	{  1, 0x80 },   /* SYM_DEGC   1.7 */
	{  7, 0x40 },   /* SYM_NET    7.6 */
	{  0, 0x01 },   /* SYM_SP1    0.0 */
	{  0, 0x02 },   /* SYM_SP2    0.1 */
	{  0, 0x04 },   /* SYM_SP3    0.2 */
	{  0, 0x08 },   /* SYM_SP4    0.3 */
	{  0, 0x10 },   /* SYM_SP5    0.4 */
	{  0, 0x20 },   /* SYM_SP6    0.5 */
	{  7, 0x10 },   /* SYM_CLOCK  7.4 */
	{  7, 0x08 },   /* SYM_HAND   7.3 */
	{  7, 0x04 },   /* SYM_HEATER 7.2 external sensor */
	{  7, 0x02 },   /* SYM_SET    7.1 */
	{  7, 0x01 },   /* SYM_HOME   7.0 */
	{  4, 0x80 },   /* SYM_LOCK   4.7 */
	{  7, 0x20 },   /* SYM_ECO    7.5 */
};
/**********************************/
void LedDrv_Init(void) {
	/* all COM lines P0.0..P0.7 (pins 61..54) as LED COM, high sink current */
	P00F = P00_COM0_SETTING;  P00C |= 0x80;
	P01F = P01_COM1_SETTING;  P01C |= 0x80;
	P02F = P02_COM2_SETTING;  P02C |= 0x80;
	P03F = P03_COM3_SETTING;  P03C |= 0x80;
	P04F = P04_COM4_SETTING;  P04C |= 0x80;
	P05F = P05_COM5_SETTING;  P05C |= 0x80;
	P06F = P06_COM6_SETTING;  P06C |= 0x80;
	P07F = P07_COM7_SETTING;  P07C |= 0x80;

	/* SEG0..SEG7: pin34..pin37 = P6.2..P6.5, pin38..pin41 = P1.0..P1.3 */
	P62F = P62_SEG12_SETTING;
	P63F = P63_SEG13_SETTING;
	P64F = P64_SEG14_SETTING;
	P65F = P65_SEG15_SETTING;
	P10F = P10_SEG16_SETTING;
	P11F = P11_SEG17_SETTING;
	P12F = P12_SEG18_SETTING;
	P13F = P13_SEG19_SETTING;

	/* Direct weekday LEDs (active high) and their common line (P5.5 high).    */
	P57F = OUTPUT;
	P56F = OUTPUT;
	P50F = OUTPUT;
	P51F = OUTPUT;
	P52F = OUTPUT;
	P34F = OUTPUT;
	P35F = OUTPUT;
	P55F = OUTPUT;                 /* green weekday common */
	P54F = OUTPUT;                 /* red flame           */
	P5  = (P5 & (uint8_t)~0xC7) | 0x20;
	P3  = P3 & (uint8_t)~0x30;
	P5 &= (uint8_t)~0x10;          /* flame off */

	LXDIVH = 0;
	LXDIVL = 0;

	/* COM active low, SEG active high, no blink, LDRV = max brightness */
	LXCFG = COMHV(COM_L) | SEGHV(SEG_H) | BLNK(0) | LDRV(LDRV_DEFAULT);

	/* clock from IRCH, LED mode */
	LXCON = LEN(LEN_IRCH) | LMOD(LMOD_led);
}
/**********************************/
void Led_SetBrightness(uint8_t level) {
	if(level > LED_BRIGHTNESS_MAX) {
		level = LED_BRIGHTNESS_MAX;
	}
	DEBUG(LOCK_TRACE_EN, { if(lock_flash || net_flash || trace_tail) debug_putc((uint8_t)('0' + level)); });

	if(level == LED_BRIGHTNESS_OFF) {
		/* REG-COM: stop the LED/LCD clock -> display fully off */
		LXCON &= 0x0F;
		return;
	}

	/* REG-COM: LDRV 0..7 = level 1..8. Mask 0xF0 (not 0xF8) so a brightness    */
	/* write also CLEARS BLNK: turning the display on always unblanks it.       */
	LXCON = (LXCON & 0x0F) | LEN(LEN_IRCH) | LMOD(LMOD_led);
	LXCFG = (LXCFG & 0xF0) | (level - 1);
}
/*******************************************************************************/
/*******************************************************************************/
void Led_SymInit(void) {
	uint8_t i;

	DEBUG(LOCK_TRACE_EN, { if(lock_flash || net_flash || trace_tail) debug_putc('='); });
	for(i = 0; i < LED_BUF_N; i++) {
		led_buf[i] = 0x00;
	}
	Led_Flush();
}
/*******************************************************************************/
void Led_SymSet(uint8_t id, uint8_t on) {
	uint8_t seg, mask;

	if(id == SYM_HEAT) {
		/* Red flame on P5.4 is a direct LED, not a matrix cell. */
		if(on) {
			P5 |= 0x10;
		}
		else {
			P5 &= (uint8_t)~0x10;
		}
		return;
	}
	if(id >= SYM_COUNT) {
		return;
	}
	seg  = sym_map[id][0];
	mask = sym_map[id][1];
	if((seg >= LED_BUF_N) || (mask == 0x00)) {
		return;                 /* not mapped yet */
	}
	if(on) {
		led_buf[seg] |= mask;
	}
	else {
		led_buf[seg] &= (uint8_t)~mask;
	}
}
/*******************************************************************************/
void Led_Flush(void) {
	uint8_t i;

	DEBUG(LOCK_TRACE_EN, { if(lock_flash || net_flash || trace_tail) debug_putc('.'); });
	for(i = 0; i < LED_BUF_N; i++) {
		INDEX = i;
		LXDAT = led_buf[i];
	}
}
/********************************************************************************/
/* 7-segment patterns: a=bit0, b=bit1, c=bit2, d=bit3, e=bit4, f=bit5, g=bit6.  */
static code const uint8_t seg_7[10] = {
	0x3F, 0x06, 0x5B, 0x4F, 0x66,   /* 0 1 2 3 4 */
	0x6D, 0x7D, 0x07, 0x7F, 0x6F    /* 5 6 7 8 9 */
};

/* Big temperature digits: INDEX5 = tens, INDEX6 = units. The hundreds '1' is */
/* SEG12/COM5+COM6 (buffer word 0, bits 5+6). '.5' = buffer word 3, bit 7.    */
/* degree C = buffer word 2, bit 7.                                           */
#define TEMP_DIG_T      5
#define TEMP_DIG_U      6

void Led_Digit(uint8_t idx, uint8_t digit) {
	if(idx >= LED_BUF_N) {
		return;
	}
	if(digit > 9) {
		led_buf[idx] &= 0x80;           /* blank (keeps the column icon, bit7) */
		return;
	}
	led_buf[idx] = (led_buf[idx] & 0x80) | seg_7[digit];
}

/* Raw 7-seg character: write bits 0-6, keep bit 7 (column icon). Used by the */
/* settings menu for letters ('-', IN/OU/AL).                                 */
void Led_Char(uint8_t idx, uint8_t seg7) {
	if(idx >= LED_BUF_N) {
		return;
	}
	led_buf[idx] = (uint8_t)((led_buf[idx] & 0x80) | (seg7 & 0x7F));
}

void Led_HalfDot(uint8_t on) {
	if(on) {
		led_buf[7] |= 0x80;            /* tenths icon at 7.7 */
	}
	else {
		led_buf[7] &= (uint8_t)~0x80;
	}
}

/* Green weekday LEDs: active high, common P5.5 held high. Week 1..7 = Mon..Sun. */
/* P5 day mask 0xC7 = P5.7/P5.6/P5.2/P5.1/P5.0; P3 day mask 0x30 = P3.5/P3.4.    */
void Led_Weekday(uint8_t week) {
	uint8_t p5 = (uint8_t)(P5 & (uint8_t)~0xC7) | 0x20;   /* common on, days off */
	uint8_t p3 = (uint8_t)(P3 & (uint8_t)~0x30);

	switch(week) {
	case 1: p5 |= 0x80; break;    /* Monday    P5.7 */
	case 2: p3 |= 0x10; break;    /* Tuesday   P3.4 */
	case 3: p3 |= 0x20; break;    /* Wednesday P3.5 */
	case 4: p5 |= 0x40; break;    /* Thursday  P5.6 */
	case 5: p5 |= 0x01; break;    /* Friday    P5.0 */
	case 6: p5 |= 0x02; break;    /* Saturday  P5.1 */
	case 7: p5 |= 0x04; break;    /* Sunday    P5.2 */
	default: break;               /* 0 or invalid: all days off */
	}
	P5 = p5;
	P3 = p3;
}
/*********************************************************************************/
/* Clock digits: hours on INDEX4/5, minutes on INDEX6/7; ':' = buffer word 6,    */
/* bit 7; AM (bit7) / PM (bit6) on buffer word 9 (11h style reserved for later). */
#define CLK_DIG_H10  1
#define CLK_DIG_H1   2
#define CLK_DIG_M10  3
#define CLK_DIG_M1   4

void Led_Clock(uint8_t hour, uint8_t min, uint8_t colon_on) {
	uint8_t h_t;
	uint8_t h_o;
	uint8_t m_t;
	uint8_t m_o;


	h_t = (uint8_t)(hour / 10);
	h_o = (uint8_t)(hour % 10);
	m_t = (uint8_t)(min / 10);
	m_o = (uint8_t)(min % 10);

	if(h_t) {
		Led_Digit(CLK_DIG_H10, h_t);
	}
	else {
		led_buf[CLK_DIG_H10] &= 0x80;   /* blank leading zero, keep °C */
	}
	Led_Digit(CLK_DIG_H1, h_o);
	Led_Digit(CLK_DIG_M10, m_t);
	Led_Digit(CLK_DIG_M1, m_o);

	if(colon_on) {
		led_buf[CLK_DIG_M10] |= 0x80;   /* ':' at 3.7 */
	}
	else {
		led_buf[CLK_DIG_M10] &= (uint8_t)~0x80;
	}
}
/********************************************************************************/
/* Blank one clock field for the clock-set blink: field 0 = hours (words 4/5),  */
/* field 1 = minutes (words 6/7). Segments A-G are bits 0..6, so mask 0x80      */
/* clears them and preserves bit 7 (':' in word 6 / decimal point).             */
void Led_Clock_Blank(uint8_t field) {
	if(field == 0) {
		led_buf[1] &= 0x80;
		led_buf[2] &= 0x80;
	}
	else {
		led_buf[3] &= 0x80;
		led_buf[4] &= 0x80;
	}
}
/********************************************************************************/
/* Two 7-seg letters in the big digits (a=bit0 ... g=bit6). Used by the menu    */
/* (IN/OU/AL), the range markers (Lo/UP) and the error text (Er).               */
void Led_Letters(uint8_t tens_seg7, uint8_t units_seg7) {
	Led_Char(TEMP_DIG_T, tens_seg7);
	Led_Char(TEMP_DIG_U, units_seg7);
}

/* IN / OU / AL in the big digits: one shared table (was duplicated as two     */
/* identical `code` arrays in main.c and menu_mode.c - a change to one half    */
/* silently moved the other). src is the sensor enum, callers clamp it.        */
void Led_SensorLetters(uint8_t src) {
	static const uint8_t code letters[3][2] = {
		{ CH_I, CH_N }, { CH_O, CH_U }, { CH_A, CH_L }
	};

	Led_Letters(letters[src][0], letters[src][1]);
}

/* menu=0 adds the temperature unit; menu=1 leaves flush to the caller. */
static void Led_DrawValue(int value_c100, uint8_t menu) {
	XDATA_TMP(led_value_parts_t, parts);

	Led_FormatC100((int16_t)value_c100, &parts);
	led_buf[TEMP_DIG_T] &= (uint8_t)~0x80;   /* no hundreds '1' upper */
	led_buf[TEMP_DIG_U] &= (uint8_t)~0x80;   /* no hundreds '1' lower */
	if(parts.range) {
		/* outside the displayable range: say so, never show a wrong number */
		if(parts.range == LED_RANGE_UP) Led_Letters(CH_U, CH_P);
		else                            Led_Letters(CH_L, CH_O);
		Led_HalfDot(0);
	}
	else {
		if(parts.negative) {
			Led_Char(TEMP_DIG_T, CH_MINUS);
		}
		else if(parts.tens) {
			Led_Digit(TEMP_DIG_T, parts.tens);
		}
		else {
			Led_Digit(TEMP_DIG_T, 10);
		}
		Led_Digit(TEMP_DIG_U, parts.units);
		Led_HalfDot(parts.half);
	}
	if(!menu) {
		Led_SymSet(SYM_DEGC, 1);       /* °C at 1.7 */
	}
}

void Led_ErrorCode(uint8_t err_code) {
	Led_Char(TEMP_DIG_T, CH_E);
	Led_Digit(TEMP_DIG_U, (uint8_t)(err_code % 10));
	Led_SymSet(SYM_DEGC, 0);
	Led_HalfDot(0);
	Led_Flush();
}

void Led_Temp(int value_c100) {
	Led_DrawValue(value_c100, 0);
	Led_Flush();
}

void Led_MenuValue(int16_t value_c100) {
	Led_DrawValue(value_c100, 1);
}
/********************************************************************************/
/* Draw "Er" in the big temperature digits (sensor error / no reading).         */
void Led_TempErr(void) {
	Led_Letters(CH_E, CH_R);             /* Led_Char keeps the column icons     */
	Led_SymSet(SYM_DEGC, 1);             /* the unit must show even on a cold   */
	                                     /* buffer, not only survive a warm one */
	led_buf[TEMP_DIG_T] &= (uint8_t)~0x80;   /* no hundreds '1' upper */
	led_buf[TEMP_DIG_U] &= (uint8_t)~0x80;   /* no hundreds '1' lower */
	Led_HalfDot(0);                      /* no stale tenths icon                */
	Led_Flush();
}
/********************************************************************************/
/* Blank the temperature digits (words 2/3 segments only, keeps '.5'/degree C). */
void Led_Temp_Blank(void) {
	led_buf[TEMP_DIG_T] &= 0x80;
	led_buf[TEMP_DIG_U] &= 0x80;
}
/********************************************************************************/
/* Bench tool: scan the REG-COM matrix one LED at a time.                       */
/* One symbol = one LED = one cell (INDEX x COM bit). r05 uses INDEX 0..7.      */
/* Each step lights a single cell ~0.4 s and prints                            */
/* "cell idx.bit" over UART; write down which icon lights per cell.             */
#if UART_DEBUG && LED_SCAN_EN
void Led_CellScan(void) {
	uint8_t idx, cb, t;

	/* ensure the display is running before writing buffer words */
	LedDrv_Init();
	LXCFG = (LXCFG & 0xF8) | LDRV(LDRV_7);
	LXCON = LEN(LEN_IRCH) | LMOD(LMOD_led);
	Led_SymInit();

	for(idx = 0; idx < 8; idx++) {
		for(cb = 0; cb < 8; cb++) {
			led_buf[idx] = (uint8_t)(1 << cb);
			Led_Flush();
			DEBUG(LED_EN, { debug_puts("cell "); debug_i16((uint16_t)idx);
				debug_putc('.'); debug_i16((uint16_t)cb);
				debug_puts("\r\n"); });
			for(t = 0; t < 16; t++)   /* ~0.4 s, feeding the watchdog */
			{
				WDFLG = 0xA5;
				Delay_ms(25);
			}
		}
		led_buf[idx] = 0x00;
		Led_Flush();
	}

	/* leave the display clean and off; main switches brightness normally */
	LXCON &= 0x0F;
}
#endif
/********************************************************************************/
/* Boot test: 3 s with all LEDs on (white matrix + green weekdays + red flame),
   then 2 s showing the firmware version in the big temperature
   digits (APP_VERSION is BCD: 0x0102 -> "1" "0" "2" = 102). */
#define LED_BOOT_ALL_MS   3000         /* all-LED self-test time                 */
#define LED_BOOT_VER_MS   2000         /* version display time                   */

void Led_BootTest(void) {
	uint16_t i, t;
	uint8_t  minor;

	/* Phase 1: REG-COM matrix - every segment on every COM, max LDRV */
	LedDrv_Init();
	LXCFG = (LXCFG & 0xF8) | LDRV(LDRV_7);
	LXCON = LEN(LEN_IRCH) | LMOD(LMOD_led);
	for(i = 0; i < LED_BUF_N; i++) {
		INDEX = (uint8_t)i;
		LXDAT = 0xFF;
	}
	/* all green weekday LEDs and the red flame on, same 3 s window */
	P5 |= 0xD7;                    /* P5.7/P5.6/P5.2/P5.1/P5.0 + P5.4 flame */
	P3 |= 0x30;                    /* P3.5/P3.4                             */

	for(t = 0; t < (LED_BOOT_ALL_MS / 100); t++) {
		WDFLG = 0xA5;
		Delay_ms(100);
		/* the main loop is blocked for ~5 s: parse+answer the peer right       */
		/* here so a request arriving during the test is not deferred to the    */
		/* end of it (TX queue stays untouched - see below).                    */
		Uart1_Poll();
		Debug_Poll();                  /* and ship the pending log line too    */
	}

	/* Phase 2: dark screen, only the version in the big digits.              */
	Led_Weekday(0);                        /* all green weekday LEDs off */
	Led_SymSet(SYM_HEAT, 0);               /* red flame off              */
	Led_SymInit();                           /* clear the shadow buffer + flush       */
	minor = (uint8_t)(APP_VERSION & 0xFF);   /* BCD: high nibble = tens               */
	if((APP_VERSION >> 8) != 0) {
		led_buf[TEMP_DIG_T] |= 0x80;   /* hundreds '1' upper */
		led_buf[TEMP_DIG_U] |= 0x80;   /* hundreds '1' lower */
	}
	Led_Digit(TEMP_DIG_T, (uint8_t)((minor >> 4) & 0x0F));
	Led_Digit(TEMP_DIG_U, (uint8_t)(minor & 0x0F));
	Led_Flush();

	for(t = 0; t < (LED_BOOT_VER_MS / 100); t++) {
		WDFLG = 0xA5;
		Delay_ms(100);
		Uart1_Poll();                        /* same servicing in the 2 s phase  */
		Debug_Poll();
	}

	/* clean up: clear shadow buffer + display, stop the display clock; the  */
	/* caller switches to normal brightness (Led_SetBrightness) in the main  */
	/* loop.                                                                 */
	Led_SymInit();
	LXCON &= 0x0F;
}
