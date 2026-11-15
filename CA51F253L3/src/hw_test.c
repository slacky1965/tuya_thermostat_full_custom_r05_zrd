/******************************************************************************/
/* Hardware bench tests. Each mode is compiled only while its config.h gate is
   ON; main() diverts into the matching Xxx_TestMain() and never returns to the
   normal UI.                                                                 */
/******************************************************************************/
#include "include/stdint.h"
#include "include/config.h"

#if KEY_TEST_EN || LED_TEST_EN || LED_GPIO_TEST_EN

#include "include/ca51f2sfr.h"
#include "include/ca51f2xsfr.h"
#include "include/gpiodef_f2.h"
#include "include/system_clock.h"
#include "include/debug.h"
#include "include/delay.h"
#include "include/led.h"
#include "include/hw_test.h"

#if KEY_TEST_EN

static uint8_t Key_Raw(void) {
	uint8_t v = 0;

	if(!P37) v |= 0x01;                /* Power */
	if(!P36) v |= 0x02;                /* Menu  */
	if(!P33) v |= 0x04;                /* Up    */
	if(!P32) v |= 0x08;                /* Down  */
	return v;
}

static void Key_Print(uint8_t raw) {
	DEBUG(BUTTONS_EN, {
		debug_puts("key ");
		debug_putc((raw & 0x01) ? '1' : '0');
		debug_putc((raw & 0x02) ? '1' : '0');
		debug_putc((raw & 0x04) ? '1' : '0');
		debug_putc((raw & 0x08) ? '1' : '0');
		debug_puts("\r\n");
	});
}

void Key_TestMain(void) {
	uint8_t last;

	debug_init();

	P37F = INPUT | PU_EN;              /* SC05B OUT lines: active low, pull-up */
	P36F = INPUT | PU_EN;
	P33F = INPUT | PU_EN;
	P32F = INPUT | PU_EN;

	last = Key_Raw();
	Key_Print(last);

	for(;;) {
		uint8_t raw = Key_Raw();

		if(raw != last) {
			last = raw;
			Key_Print(raw);
		}
		Delay_ms(10);
	}
}

#endif /* KEY_TEST_EN */

#if LED_TEST_EN

static void Led_Write(uint8_t idx, uint8_t val) {
	INDEX = idx;
	LXDAT = val;
}

static void Led_AllOn(void) {
	uint8_t i;

	for(i = 0; i < 8; i++) {
		Led_Write(i, 0xFF);
	}
}

static void Led_Clear8(void) {
	uint8_t i;

	for(i = 0; i < 20; i++) {
		Led_Write(i, 0x00);
	}
}

static void Led_BlinkFrame(uint8_t idx, uint8_t com, uint8_t on) {
	Led_Clear8();
	if(on) {
		Led_Write(idx, (uint8_t)(1u << com));
	}
}

static void Led_Print(uint8_t idx, uint8_t com) {
	DEBUG(BUTTONS_EN, {
		debug_puts("cell ");
		debug_i16((uint16_t)idx);
		debug_putc('.');
		debug_i16((uint16_t)com);
		debug_puts("\r\n");
	});
}

/* Up = next cell, Down = previous. Every other LED stays on; the selected
   cell blinks, so it is easy to see in context. */
static uint8_t Led_KeyState(void) {
	uint8_t p3 = P3;
	uint8_t v = 0;

	if(!(p3 & 0x08)) v |= 0x01;        /* Up:   P3.3 */
	if(!(p3 & 0x04)) v |= 0x02;        /* Down: P3.2 */
	return v;
}

/* All segments on; one selected cell blinks. COM0..COM7 = P0.0..P0.7,
   SEG0..SEG7 = P6.2..P6.5 + P1.0..P1.3 (the r05 pin set used by stock). */
void Led_TestMain(void) {
	uint8_t idx = 0;
	uint8_t com = 0;
	uint8_t last = 0;
	uint8_t cur;
	uint8_t blink_on = 1;
	uint8_t blink_cnt = 0;

	debug_init();

	P00F = P00_COM0_SETTING;  P00C |= 0x80;
	P01F = P01_COM1_SETTING;  P01C |= 0x80;
	P02F = P02_COM2_SETTING;  P02C |= 0x80;
	P03F = P03_COM3_SETTING;  P03C |= 0x80;
	P04F = P04_COM4_SETTING;  P04C |= 0x80;
	P05F = P05_COM5_SETTING;  P05C |= 0x80;
	P06F = P06_COM6_SETTING;  P06C |= 0x80;
	P07F = P07_COM7_SETTING;  P07C |= 0x80;

	P62F = P62_SEG12_SETTING;
	P63F = P63_SEG13_SETTING;
	P64F = P64_SEG14_SETTING;
	P65F = P65_SEG15_SETTING;
	P10F = P10_SEG16_SETTING;
	P11F = P11_SEG17_SETTING;
	P12F = P12_SEG18_SETTING;
	P13F = P13_SEG19_SETTING;
	P14F = P14_SEG20_SETTING;
	P15F = P15_SEG21_SETTING;
	P16F = P16_SEG22_SETTING;
	P17F = P17_SEG23_SETTING;
	P20F = P20_SEG31_SETTING;
	P21F = P21_SEG30_SETTING;
	P22F = P22_SEG29_SETTING;
	P23F = P23_SEG28_SETTING;
	P24F = P24_SEG27_SETTING;
	P25F = P25_SEG26_SETTING;
	P26F = P26_SEG25_SETTING;
	P27F = P27_SEG24_SETTING;

	P33F = INPUT | PU_EN;              /* Up/Down step the scan */
	P32F = INPUT | PU_EN;

	LXDIVH = 0;
	LXDIVL = 0;
	LXCFG = COMHV(COM_L) | SEGHV(SEG_H) | BLNK(0) | LDRV(LDRV_7);
	LXCON = LEN(LEN_IRCH) | LMOD(LMOD_led);

	P54F = OUTPUT;          /* red flame / green weekday common */
	P5 |= 0x10;             /* P5.4 high */
	Led_Clear8();
	Led_BlinkFrame(idx, com, blink_on);
	Led_Print(idx, com);

	for(;;) {
		cur = Led_KeyState();

		if((cur & 0x01) && !(last & 0x01)) {       /* Up: next cell */
			if(com < 7) {
				com++;
			}
			else if(idx < 19) {
				idx++;
				com = 0;
			}
			else {
				idx = 0;
				com = 0;
			}
			blink_on = 1;
			blink_cnt = 0;
			Led_BlinkFrame(idx, com, blink_on);
			Led_Print(idx, com);
		}
		else if((cur & 0x02) && !(last & 0x02)) {  /* Down: previous cell */
			if(com > 0) {
				com--;
			}
			else if(idx > 0) {
				idx--;
				com = 7;
			}
			else {
				idx = 7;
				com = 7;
			}
			blink_on = 1;
			blink_cnt = 0;
			Led_BlinkFrame(idx, com, blink_on);
			Led_Print(idx, com);
		}

		last = cur;
		WDFLG = 0xA5;
		Delay_ms(10);
		blink_cnt++;
		if(blink_cnt >= 30) {
			blink_cnt = 0;
			blink_on ^= 1;
			Led_BlinkFrame(idx, com, blink_on);
		}
	}
}

#endif /* LED_TEST_EN */

#if LED_GPIO_TEST_EN

static void Gpio_Set(uint8_t idx, uint8_t on) {
	/* P5.4 is the red flame and the common line for the green weekday LEDs.
	   The weekday cathodes are active low: one low = that day on. */
	uint8_t p5 = 0xF7;      /* P5.4 flame + P5.5 green common high; day pins high */
	uint8_t p3 = P3 | 0x30; /* P3.4/P3.5 high = off */

	if(on) {
		switch(idx) {
			case 0: p5 &= (uint8_t)~0x80; break;   /* P5.7 */
			case 1: p3 &= (uint8_t)~0x10; break;   /* P3.4 */
			case 2: p3 &= (uint8_t)~0x20; break;   /* P3.5 */
			case 3: p5 &= (uint8_t)~0x40; break;   /* P5.6 */
			case 4: p5 &= (uint8_t)~0x01; break;   /* P5.0 */
			case 5: p5 &= (uint8_t)~0x02; break;   /* P5.1 */
			case 6: p5 &= (uint8_t)~0x04; break;   /* P5.2 */
		}
	}
	P5 = p5;
	P3 = p3;
}

static void Gpio_Print(uint8_t idx) {
	DEBUG(BUTTONS_EN, {
		debug_puts("gpio ");
		debug_dec(idx);
		debug_puts(": ");
		switch(idx) {
			case 0: debug_puts("P5.7"); break;
			case 1: debug_puts("P3.4"); break;
			case 2: debug_puts("P3.5"); break;
			case 3: debug_puts("P5.6"); break;
			case 4: debug_puts("P5.0"); break;
			case 5: debug_puts("P5.1"); break;
			case 6: debug_puts("P5.2"); break;
		}
		debug_puts("\r\n");
	});
}

static uint8_t Gpio_KeyState(void) {
	uint8_t p3 = P3;
	uint8_t v = 0;

	if(!(p3 & 0x08)) v |= 0x01;        /* Up:   P3.3 */
	if(!(p3 & 0x04)) v |= 0x02;        /* Down: P3.2 */
	return v;
}

/* One direct GPIO at a time: P5.7, P3.4, P3.5, P5.6, P5.0, P5.1, P5.2.
   The selected pin blinks; the others stay off. Up/Down step the scan. */
void Led_Gpio_TestMain(void) {
	uint8_t idx = 0;
	uint8_t last = 0;
	uint8_t cur;
	uint8_t on = 1;
	uint8_t blink_cnt = 0;

	debug_init();

	P57F = OUTPUT;
	P56F = OUTPUT;
	P54F = OUTPUT;
	P55F = OUTPUT;   /* green weekday common */
	P52F = OUTPUT;
	P51F = OUTPUT;
	P50F = OUTPUT;
	P34F = OUTPUT;
	P35F = OUTPUT;

	P33F = INPUT | PU_EN;              /* Up/Down step the scan */
	P32F = INPUT | PU_EN;

	Gpio_Set(idx, on);
	Gpio_Print(idx);

	for(;;) {
		cur = Gpio_KeyState();

		if((cur & 0x01) && !(last & 0x01)) {
			if(idx < 6) idx++; else idx = 0;
			on = 1;
			blink_cnt = 0;
			Gpio_Set(idx, on);
			Gpio_Print(idx);
		}
		else if((cur & 0x02) && !(last & 0x02)) {
			if(idx > 0) idx--; else idx = 6;
			on = 1;
			blink_cnt = 0;
			Gpio_Set(idx, on);
			Gpio_Print(idx);
		}

		last = cur;
		WDFLG = 0xA5;
		Delay_ms(10);
		blink_cnt++;
		if(blink_cnt >= 50) {          /* ~0.5 s blink */
			blink_cnt = 0;
			on ^= 1;
			Gpio_Set(idx, on);
		}
	}
}

#endif /* LED_GPIO_TEST_EN */

#endif /* KEY_TEST_EN || LED_TEST_EN || LED_GPIO_TEST_EN */
