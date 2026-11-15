#include "include/stdint.h"
#include "include/config.h"
#include "include/ca51f2sfr.h"
#include "include/ca51f2xsfr.h"
#include "include/gpiodef_f2.h"
#include "include/system_clock.h"
#include "include/delay.h"
#include "include/settings.h"
#include "include/buttons.h"
#include "include/debug.h"

/********************************************************************************/
/* GPIO key driver (r05).
 *
 * The r05 front panel has four keys wired to the SC05B parallel outputs:
 *   P3.7 = Power, P3.6 = Menu, P3.3 = Up, P3.2 = Down.
 * The lines are active low; the internal pull-ups are enabled here.
 *
 * Events: CLICK/HOLD/HOLD_REPEAT/RELEASE and the two chords are unchanged:
 *   UP+DOWN    -> keypad lock
 *   POWER+MENU -> Eco toggle
 ********************************************************************************/

#define KEY_SCAN_10MS_TICKS   1       /* scan grid: one 10 ms tick               */
#define KEY_DEBOUNCE_TICKS    2       /* 2 x 10 ms stable state = 20 ms          */

/* event queue: fixed size, events are only drained by BTN_GetEvent()           */
#define EVQ_SIZE        8

/* debounced press state, event timing and chord machinery live in xdata on     */
/* purpose: it would otherwise push the 128-byte DATA bank over the top.        */
static uint8_t  xdata pdown[BTN_COUNT];
static uint8_t  xdata on[BTN_COUNT];
static uint8_t  xdata stable[BTN_COUNT];
static uint8_t  xdata cnt[BTN_COUNT];
static uint16_t xdata down_tick10[BTN_COUNT];  /* tick10ms when key went down */
static uint16_t xdata hold_tick10[BTN_COUNT];  /* tick10ms when HOLD fired    */
static uint8_t  xdata was_hold[BTN_COUNT];     /* HOLD already reported       */

/* Two-button chords, either press order:
 *   chord 1: UP+DOWN     -> keypad lock
 *   chord 2: POWER+MENU  -> Eco toggle
 * While both keys of a pair are down they emit no individual events; a single
 * event fires after the pair's hold time (total, from the FIRST press).       */
static uint8_t  xdata chord_on;
static uint8_t  xdata chord_fired;
static uint16_t xdata chord_tick;
static uint8_t  xdata chord2_on;
static uint8_t  xdata chord2_fired;
static uint16_t xdata chord2_tick;

static uint8_t xdata evq_key[EVQ_SIZE];
static uint8_t xdata evq_ev[EVQ_SIZE];
static uint8_t xdata evq_in, evq_out;
/****************************************************************************/
static void evq_push(btn_id_t b, btn_evt_t ev) {
	uint8_t next = (uint8_t)((evq_in + 1) & (EVQ_SIZE - 1));   /* EVQ_SIZE is a power of 2 */

	if(next == evq_out) return;      /* full: drop the incoming event (queue is a diagnostic aid) */
	evq_key[evq_in] = (uint8_t)b;
	evq_ev[evq_in]  = (uint8_t)ev;
	evq_in = next;
}
/****************************************************************************/
/* Physical key state: 1 = pressed (SC05B output low).                        */
/* Read the P3 port as a byte: LTO on this backend cannot select an sbit       */
/* GlobalAddress once the scan is inlined into main().                         */
static uint8_t Key_Pressed(btn_id_t b) {
	uint8_t p3 = P3;

	switch(b) {
		case BTN_POWER: return (p3 & 0x80) ? 0 : 1;   /* P3.7 */
		case BTN_MENU:  return (p3 & 0x40) ? 0 : 1;   /* P3.6 */
		case BTN_UP:    return (p3 & 0x08) ? 0 : 1;   /* P3.3 */
		case BTN_DOWN:  return (p3 & 0x04) ? 0 : 1;   /* P3.2 */
		default:        return 0;
	}
}
/*****************************************************************************/
void BTN_Init(void) {
	uint8_t i;

	P37F = INPUT | PU_EN;            /* SC05B OUT lines: active low, pull-up    */
	P36F = INPUT | PU_EN;
	P33F = INPUT | PU_EN;
	P32F = INPUT | PU_EN;

	for(i = 0; i < BTN_COUNT; i++) {
		pdown[i] = 0;
		on[i] = 0;
		stable[i] = 0;
		cnt[i] = 0;
		down_tick10[i] = 0;
		hold_tick10[i] = 0;
		was_hold[i] = 0;
	}
	chord_on = chord_fired = 0;
	chord_tick = 0;
	chord2_on = chord2_fired = 0;
	chord2_tick = 0;
	evq_in = evq_out = 0;
}

void BTN_Scan(void) {
	static uint16_t xdata last_scan_tick;
	uint16_t now = BTN_Tick10ms();
	uint8_t i;
	uint8_t pair_start;
	uint8_t pair_start2;

	if((uint16_t)(now - last_scan_tick) < KEY_SCAN_10MS_TICKS) return;
	last_scan_tick = now;

	/* Debounce every logical key on the 10 ms grid. */
	for(i = 0; i < BTN_COUNT; i++) {
		uint8_t raw = Key_Pressed((btn_id_t)i);

		if(raw != stable[i]) {
			if(++cnt[i] >= KEY_DEBOUNCE_TICKS) {
				stable[i] = raw;
				cnt[i] = 0;
			}
		}
		else {
			cnt[i] = 0;
		}
		pdown[i] = stable[i];
	}

	/* Debounced press-state snapshot (all keys) and the real press reference  */
	/* tick of this scan, so every long-press threshold is total time from the */
	/* press (HOLD/REPEAT/chord) instead of from the previous scan.            */
	pair_start = !on[BTN_CHORD_A] && !on[BTN_CHORD_B];
	pair_start2 = !on[BTN_CHORD2_A] && !on[BTN_CHORD2_B];

	for(i = 0; i < BTN_COUNT; i++) {
		if(pdown[i] && !on[i]) {
			down_tick10[i] = BTN_Tick10ms();
			if(pair_start && (i == BTN_CHORD_A || i == BTN_CHORD_B)) {
				chord_tick = down_tick10[i];
			}
			if(pair_start2 && (i == BTN_CHORD2_A || i == BTN_CHORD2_B)) {
				chord2_tick = down_tick10[i];
			}
		}
	}

	/* chord state machine: UP+DOWN both down -> hold timer -> EVENT_CHORD     */
	{
		uint8_t ca = pdown[BTN_CHORD_A];
		uint8_t cb = pdown[BTN_CHORD_B];

		if(ca && cb) {
			if(!chord_on) {
				chord_on = 1;
				chord_fired = 0;
				/* 2 s of TOTAL hold from the first key of the pair (both press
				   orders). A partner that arrives after the grace window - or a
				   stale reference - re-anchors the start, so a key that was held
				   for seconds cannot toggle the lock on the next key down.       */
				if((uint16_t)(BTN_Tick10ms() - chord_tick) > BTN_CHORD_GRACE_10MS_TICKS) {
					chord_tick = BTN_Tick10ms();
				}
			}
			else if(!chord_fired &&
			        (uint16_t)(BTN_Tick10ms() - chord_tick) >= BTN_CHORD_10MS_TICKS) {
				chord_fired = 1;
				evq_push((btn_id_t)BTN_CHORD, BTN_EVT_CHORD);
			}
		}
		else if(chord_on && !ca && !cb) {
			chord_on = 0;            /* both up: gesture complete, next re-arms */
			chord_fired = 0;
			/* both released in the SAME scan: swallow the release edges so the */
			/* chord never leaks a lone CLICK on UP/DOWN.                       */
			on[BTN_CHORD_A] = 0;
			on[BTN_CHORD_B] = 0;
			was_hold[BTN_CHORD_A] = 0;
			was_hold[BTN_CHORD_B] = 0;
		}
	}

	/* second chord state machine: POWER+MENU both down -> EVENT_CHORD2 (Eco). */
	{
		uint8_t ca2 = pdown[BTN_CHORD2_A];
		uint8_t cb2 = pdown[BTN_CHORD2_B];

		if(ca2 && cb2) {
			if(!chord2_on) {
				chord2_on = 1;
				chord2_fired = 0;
				if((uint16_t)(BTN_Tick10ms() - chord2_tick) > BTN_CHORD_GRACE_10MS_TICKS) {
					chord2_tick = BTN_Tick10ms();
				}
			}
			else if(!chord2_fired &&
			        (uint16_t)(BTN_Tick10ms() - chord2_tick) >= BTN_CHORD2_10MS_TICKS) {
				chord2_fired = 1;
				evq_push((btn_id_t)BTN_CHORD2, BTN_EVT_CHORD2);
			}
		}
		else if(chord2_on && !ca2 && !cb2) {
			chord2_on = 0;
			chord2_fired = 0;
			on[BTN_CHORD2_A] = 0;
			on[BTN_CHORD2_B] = 0;
			was_hold[BTN_CHORD2_A] = 0;
			was_hold[BTN_CHORD2_B] = 0;
		}
	}

	for(i = 0; i < BTN_COUNT; i++) {
		uint8_t pressed = pdown[i];

		/* while a chord is in progress its keys emit no individual events,     */
		/* but IsDown/AnyDown must still see the true hardware state.           */
		if((chord_on && (i == BTN_CHORD_A || i == BTN_CHORD_B)) ||
		   (chord2_on && (i == BTN_CHORD2_A || i == BTN_CHORD2_B))) {
			on[i] = pressed;
			continue;
		}

		if(pressed) {
			if(!on[i])                       /* press edge */
			{
				on[i] = 1;
				was_hold[i]  = 0;
			}
			else {
				/* a lone chord key does not count as an individual hold until  */
				/* the partner window elapsed: a slightly-staggered press never  */
				/* leaks a lone HOLD or repeat.                                  */
				if(((i == BTN_CHORD_A || i == BTN_CHORD_B) ||
				    (i == BTN_CHORD2_A || i == BTN_CHORD2_B)) &&
				   (uint16_t)(BTN_Tick10ms() - down_tick10[i]) < BTN_CHORD_GRACE_10MS_TICKS) {
					continue;
				}
				uint16_t t = BTN_Tick10ms() - down_tick10[i];

				if(!was_hold[i] && t >= BTN_HOLD_10MS_TICKS) {
					was_hold[i] = 1;
					hold_tick10[i] = BTN_Tick10ms();
					evq_push((btn_id_t)i, BTN_EVT_HOLD);
				}
				else if(was_hold[i] && (BTN_Tick10ms() - hold_tick10[i]) >= BTN_REPEAT_10MS_TICKS) {
					hold_tick10[i] = BTN_Tick10ms();
					evq_push((btn_id_t)i, BTN_EVT_HOLD_REPEAT);
				}
			}
		}
		else {
			if(on[i])                        /* release edge */
			{
				on[i] = 0;
				down_tick10[i] = 0;
				evq_push((btn_id_t)i, was_hold[i] ? BTN_EVT_RELEASE : BTN_EVT_CLICK);
				was_hold[i] = 0;
			}
		}
	}
}

uint8_t BTN_IsDown(btn_id_t b) {
	if(b >= BTN_COUNT) return 0;
	return on[b];
}

/* Keypad-lock policy for ONE key, evaluated against the CURRENT lock state:   */
/*   FREE   -> every key,                                                      */
/*   EX_OFF -> the power leaf only (the local unlock stays the chord),         */
/*   FULL   -> no ordinary key at all (the chord is handled before this call). */
/* Delayed long-press arms re-run this right before they act, so a remote lock */
/* change cannot be overtaken by an arm that was raised while the keys were    */
/* still free.                                                                 */
uint8_t Btn_ActionAllowed(btn_id_t key) {
	uint8_t lock = settings.keypadLockout;

	if(lock == KEYPAD_LOCKOUT_FREE) return 1;
	if(lock == KEYPAD_LOCKOUT_EX_OFF) return (key == BTN_POWER) ? 1 : 0;
	return 0;   /* KEYPAD_LOCKOUT_FULL */
}

uint8_t BTN_GetEvent(btn_id_t xdata *b) {
	uint8_t ev;

	if(evq_in == evq_out) return BTN_EVT_NONE;
	ev = evq_ev[evq_out];
	if(b) *b = (btn_id_t)evq_key[evq_out];
	evq_out = (uint8_t)((evq_out + 1) & (EVQ_SIZE - 1));   /* EVQ_SIZE is a power of 2 */
	return ev;
}

#if UART_DEBUG && BTN_BENCH_EN

uint8_t BTN_AnyDown(void) {
	uint8_t i;

	for(i = 0; i < BTN_COUNT; i++) if(on[i]) return 1;
	return 0;
}

int16_t BTN_Delta(btn_id_t b) {
	(void)b;
	return 0;
}

uint16_t BTN_Baseline(btn_id_t b) {
	(void)b;
	return 0;
}

#endif /* UART_DEBUG && BTN_BENCH_EN */

#if UART_DEBUG && RAW_EN

uint16_t BTN_Raw(btn_id_t b) {
	return (uint16_t)((b < BTN_COUNT) ? pdown[b] : 0);
}

uint16_t BTN_Ref(void) {
	return 0;
}

#endif /* UART_DEBUG && RAW_EN */
