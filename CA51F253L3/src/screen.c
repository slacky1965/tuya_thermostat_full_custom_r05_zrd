/* Screen state: brightness policy, composition of the ON/OFF screens, standby
   phases, padlock indication and the NET symbol. Extracted from main.c in
   waves; main() drives these helpers from its event drain and 1 s tick. */
#include "include/config.h"
#include "include/stdint.h"
#include "include/buttons.h"
#include "include/debug.h"
#include "include/link_proto.h"
#include "include/led.h"
#include "include/chars.h"
#include "include/modes.h"
#include "include/relay.h"
#include "include/rtc.h"
#include "include/settings.h"
#include "include/fault.h"
#include "include/temp_pipeline.h"

uint8_t xdata bright_hold;                /* seconds left of the max-brightness window */
/* xdata initialisers are useless: xdata_clear() zeroes XISEG at boot and      */
/* Thermo_Apply() re-arms bright_level. Kept commented for reference.          */
static uint8_t xdata bright_level /* = 0xFF */;  /* last level applied to the LEDs */

/* Base brightness for the current time of day: day 06:00-22:00, night the rest. */
uint8_t Bright_LevelBase(void) {
	XDATA_TMP(uint8_t, h);

	RTC_ReadTime(&h, 0, 0, 0);
	if((h >= 6) && (h < 22)) {
		return settings.currentLevel_day;
	}
	return settings.currentLevel_night;
}

/* Apply the display brightness (0=off, 1-8). Every LED on r05 is driven by   */
/* the display brightness; there is no separate key/leaf backlight.           */
void Bright_Apply(uint8_t level) {
	if(level == bright_level) return;   /* already applied (per-event drain refresh) */
	bright_level = level;
	Led_SetBrightness((uint8_t)level);
}

/* Re-arm the max-brightness window and apply it. One definition for every      */
/* place that must spike the display: a lock change, the NET blink, a mode      */
/* that owns the display, and any key event.                                    */
void Bright_MaxHold(void) {
	bright_hold = BRIGHTNESS_HOLD_SECS;
	Bright_Apply(LED_BRIGHTNESS_MAX);
}

/* OFF: display at MAX for a short phase so a lock change is visible.          */
uint8_t xdata standby_hold;             /* seconds left of the max-brightness phase */
/* xdata initialiser dropped: xdata_clear() zeroes it, Thermo_Apply re-arms it. */

void Standby_BrightApply(void) {
	Led_SymInit();
	Lock_Sym();
	Led_SetBrightness(settings.keypadLockout ?
	                  LED_BRIGHTNESS_MAX : LED_BRIGHTNESS_OFF);
	Led_Flush();
}

const temp_sample_t xdata *Temp_DisplaySample(void) {
	return (temp_src == SENSOR_SRC_IN) ?
		Temp_Pipeline_Get(TEMP_INTERNAL) : Temp_Pipeline_Get(TEMP_EXTERNAL);
}

/* One place that turns a pipeline snapshot into the big digits: an invalid    */
/* sample shows "Er" (the degree C stays lit) instead of a stale number.       */
/* In AL a dead probe of EITHER kind shows "Er": the pair drives the relay, so */
/* a half-dead pair must not look healthy on screen.                           */

/* AL "Er" alternation state: while one probe of the AL pair is dead the       */
/* display cycles between the survivor's temperature and "Er", so the value    */
/* the relay actually heats by stays visible. The machine is driven by         */
/* Temp_ErPoll() on the 100 ms main-loop grid; phase lengths live in config.h. */
static uint16_t xdata er_alt_deadline;  /* tick10ms time of the next flip      */
static bit er_alt_run;                  /* 1 = the machine is cycling          */
static bit er_alt_lit;                  /* 1 = the current phase shows "Er"    */

/* 1 = the display should alternate temperature and an error code:
   - AL pair half-dead and no fresh WS owns the digits: the pair verdict is
     "Er" but exactly one probe keeps an accepted value;
   - E0 (system/unknown error) is active and the selected temperature is
     valid: E0 is not tied to a probe, so it blinks over the temperature. */
static uint8_t Temp_ErAltCondition(void) {
	const temp_sample_t xdata *in;
	const temp_sample_t xdata *ext;
	const temp_sample_t xdata *sample;

	if(Link_WsFresh()) return 0;

	if(Fault_DisplayCodeFor((uint8_t)temp_src) == 0) {
		sample = Temp_DisplaySample();
		return (uint8_t)(sample && sample->accepted_valid);
	}

	if(temp_src != SENSOR_SRC_ALL) return 0;
	in  = Temp_Pipeline_Get(TEMP_INTERNAL);
	ext = Temp_Pipeline_Get(TEMP_EXTERNAL);
	/* Only an EXTERNAL probe failure is announced in AL: the display falls
	   back to the internal temperature and alternates it with the external
	   E-code. A dead INTERNAL probe is network-only (the external temperature
	   stays on screen, the relay runs on it). */
	return (uint8_t)(!ext->accepted_valid && in->accepted_valid);
}

void Temp_Draw(const temp_sample_t xdata *sample) {
	const temp_sample_t xdata *in;
	const temp_sample_t xdata *ext;
	uint8_t err_code;

	/* WS override: while the wireless sensor is fresh it drives the relay, so  */
	/* it owns the big digits too. Its value was validated on arrival and the   */
	/* wired probes are irrelevant here - no "Er" verdict. When it goes stale   */
	/* the very next redraw falls back to the configured probes below.          */
	if(Link_WsFresh()) {
		Led_Temp(Link_WsTemp());
		return;
	}

	err_code = Fault_DisplayCodeFor((uint8_t)temp_src);

	if(temp_src == SENSOR_SRC_ALL) {
		in  = Temp_Pipeline_Get(TEMP_INTERNAL);
		ext = Temp_Pipeline_Get(TEMP_EXTERNAL);

		if(in->accepted_valid && ext->accepted_valid) {
			if(err_code != 0xFF) {
				if(err_code == 0 && er_alt_run && !er_alt_lit) {
					Led_Temp(sample->accepted_c100);
				}
				else {
					Led_ErrorCode(err_code);
				}
			}
			else {
				Led_Temp(sample->accepted_c100);
			}
			return;
		}

		if(ext->accepted_valid && !in->accepted_valid) {
			/* Internal probe failed: AL keeps the external temperature on   */
			/* screen; the internal E-code is network-only.                   */
			Led_Temp(ext->accepted_c100);
			return;
		}

		if(in->accepted_valid || ext->accepted_valid) {
			/* External probe failed: alternate the internal temperature and  */
			/* the external E-code while the error machine runs.              */
			if(er_alt_run && !er_alt_lit) {
				Led_Temp(in->accepted_valid ? in->accepted_c100
				                            : ext->accepted_c100);
			}
			else if(err_code != 0xFF) {
				Led_ErrorCode(err_code);
			}
			else {
				Led_TempErr();
			}
			return;
		}

		/* Both probes failed: no valid temperature, only "Er" on screen.    */
		Led_TempErr();
		return;
	}

	if(err_code != 0xFF) {
		if(err_code == 0 && er_alt_run && !er_alt_lit && sample->accepted_valid) {
			Led_Temp(sample->accepted_c100);
		}
		else {
			Led_ErrorCode(err_code);
		}
	}
	else if(sample->accepted_valid) {
		Led_Temp(sample->accepted_c100);
	}
	else {
		Led_TempErr();
	}
}

/* 100 ms poll driving the temperature/error alternation (AL half-dead pair or
   E0). Redraws only at phase transitions through Temp_Draw(); the machine
   stops while power is OFF, a mode owns the screen, an adjust window
   (Set / %RH) is up, or a fresh WS owns the digits. */
void Temp_ErPoll(void) {
	uint16_t now;

	if(!power_on || !Screen_ModeFree() || adj_remain ||
	   !Temp_ErAltCondition()) {
		er_alt_run = 0;
		er_alt_lit = 0;
		return;
	}
	now = BTN_Tick10ms();
	if(!er_alt_run) {
		er_alt_run = 1;
		er_alt_lit = 0;
		er_alt_deadline = (uint16_t)(now + TEMP_ER_TEMP_10MS_TICKS);
		Temp_Draw(Temp_DisplaySample());   /* survivor value right away        */
		return;
	}
	if((int16_t)(now - er_alt_deadline) < 0) return;
	er_alt_lit = !er_alt_lit;
	er_alt_deadline = (uint16_t)(now + (er_alt_lit ? TEMP_ER_WARN_10MS_TICKS
	                                            : TEMP_ER_TEMP_10MS_TICKS));
	Temp_Draw(Temp_DisplaySample());
}

/*******************************************************************************/
void Clock_Draw(void) {
	XDATA_TMP(uint8_t, h);
	XDATA_TMP(uint8_t, m);

	RTC_ReadTime(&h, &m, 0, 0);
	Led_Clock(h, m, clock_colon);
	Led_Flush();      /* push now: a caller must never rely on the next 1 s tick */
}

/*********************************************************************************/
/* Re-apply the green ON/OFF leaf to the current day/night level while OFF.      */
/* Called once per second from the standby branch so the leaf follows 06:00 and  */
/* 22:00 (and a host time sync) without waiting for a power toggle.              */
/* The standby padlock is driven by the SAME helper: it lives on the display     */
/* brightness, so a day/night or remote brightness change must move it together  */
/* with the leaf. The register write is idempotent; the helper only runs in      */
/* steady standby, where nothing else owns the display.                          */
void Standby_BaseApply(void) {
	/* the padlock rides on the display brightness: re-assert it every second so  */
	/* it tracks the day/night level (it was last written by Thermo_Apply).      */
	Led_SetBrightness(settings.keypadLockout ? Bright_LevelBase() : LED_BRIGHTNESS_OFF);
}

/* OFF feedback for the lock (chord or remote 0x17): blink the padlock on the   */
/* dark display so the toggle is visible. One phase = 300 ms, 10 phases = 5     */
/* full blinks (~3 s) at full brightness. Phases only own the blink itself:     */
/* afterwards a locked unit keeps the padlock glowing at the settings day/night */
/* level next to the padlock.                                                   */
#define LOCK_FLASH_PHASES  10
#define LOCK_FLASH_TICKS   30
#define LOCK_FLASH_LEVEL   LED_BRIGHTNESS_MAX
uint8_t xdata lock_flash;                 /* phases left to run                         */
uint8_t xdata trace_tail;                 /* LOCK_TRACE_EN: seconds of probe life after */
                                          /* the blink, so the post-blink state is      */
                                          /* visible (all probes test lock_flash||this) */
static uint16_t xdata lock_flash_tick;    /* tick10ms of the last flip                  */
static uint8_t xdata lock_flash_lit;      /* padlock currently lit                      */

/* Padlock symbol follows settings.keypadLockout; one definition for the three  */
/* places that draw it (power-on screen, lock change, 1 s redraw).              */
void Lock_Sym(void) {
	Led_SymSet(SYM_LOCK, settings.keypadLockout != 0);
}

/* ON: a keypad-lock change (local chord or remote 0x17) gets a fresh-ON        */
/* brightness spike and redraws the padlock at once, so the change is visible   */
/* on an otherwise dimmed display. One definition for both entry points.        */
void Lock_On_Screen(void) {
	Bright_MaxHold();
	Lock_Sym();
	Led_Flush();
}

/* Thermostat state: power always startup OFF.                                  */
bit power_on;    /* 0 = off (standby), 1 = on (display)                  */
bit ext_ok;      /* 1 = external probe present (SYM_HEATER)              */

/* "NET" (SYM_NET) blink: started by a >=2 s DOWN hold while OFF. Blinks OVER  */
/* whatever is on screen (dark in OFF, the normal UI in ON) for 1.5 min, then  */
/* stops by itself. A future ZT3L network event may stop it earlier / turn the */
/* symbol on steady (see Net_Flash_Stop()/Net_Set_Steady() below).             */
/* (NET_HOLD_10MS_TICKS stays in main.c: it arms down_arm in the key drain.)   */
#define NET_FLASH_SECS   90                  /* 1.5 minutes                              */
#define NET_FLASH_TICKS  30                  /* 300 ms per half-phase                    */
uint8_t xdata net_flash;                     /* 1 = blink active                         */
static uint8_t xdata net_flash_lit;          /* current phase (1 = lit)                  */
uint8_t xdata net_flash_secs;                /* seconds left before auto-stop            */
static uint16_t xdata net_flash_tick;        /* tick10ms of the last flip                */
static uint8_t xdata net_steady;             /* 1 = NET on steady (future ZT3L)          */
uint8_t xdata net_hold;                      /* OFF: seconds left of the steady          */
                                             /* NET display before the standby fade      */
/*****************************************************************************************/
/* Apply the whole LED state for power_on/off.                                  */
/* The manual/schedule icon pair: ONE mapping for the remote window, the        */
/* local MENU click and this repaint (was three hand-written copies that had    */
/* already drifted in formatting). progMode is a bitmap - hand/clock follow the */
/* schedule BIT, the economy symbol follows ZCL bit 2 (Eco).                    */
void ProgMode_Icons(uint8_t pm) {
	Led_SymSet(SYM_HAND,  (pm & PROG_MODE_SCHEDULE) ? 0 : 1);
	Led_SymSet(SYM_CLOCK, (pm & PROG_MODE_SCHEDULE) ? 1 : 0);
	Led_SymSet(SYM_ECO,   (pm & PROG_MODE_ECO)      ? 1 : 0);
}

/* 1 when NO settings mode owns the screen. Single source for the six           */
/* clk/sched/menu triple compares that used to live in main.c (two spellings).  */
uint8_t Screen_ModeFree(void) {
	return (uint8_t)(clk_set == CLK_SET_OFF && sched_set == SCHED_SET_OFF &&
	                 menu_set == MENU_OFF);
}

/* WS indicator on SYM_HEATER:
   - 3 s blink when a fresh WS report first arrives;
   - then steady, with a short 100 ms off pulse every 5 s;
   - when WS is stale, the icon returns to the wired external probe state. */
#define WS_CONNECT_10MS       300
#define WS_BLINK_HALF_10MS     25
#define WS_PULSE_PERIOD_10MS  500
#define WS_PULSE_OFF_10MS     100

static uint16_t xdata ws_conn_tick;
static uint16_t xdata ws_blink_tick;
static uint16_t xdata ws_period_tick;
static uint8_t  xdata ws_state;
static uint8_t  xdata ws_off_ticks;
static bit ws_seen;
static bit ws_icon_on;

void Ws_Icon_Tick(void) {
	uint16_t now;
	uint8_t fresh;

	if(!power_on || !Screen_ModeFree()) {
		ws_icon_on = 0;                 /* mode/off cleared the buffer: re-light on return */
		return;
	}
	now = BTN_Tick10ms();
	fresh = Link_WsFresh();

	if(!fresh) {
		if(ws_seen || (uint8_t)ws_icon_on != ext_ok) {
			ws_seen = 0;
			ws_state = 0;
			ws_off_ticks = 0;
			ws_icon_on = ext_ok;
			Led_SymSet(SYM_HEATER, ext_ok);
			Led_Flush();
		}
		return;
	}

	if(!ws_seen) {
		ws_seen = 1;
		ws_state = 1;
		ws_conn_tick = now;
		ws_blink_tick = now;
		ws_icon_on = 1;
		Led_SymSet(SYM_HEATER, 1);
		Led_Flush();
		return;
	}

	if(ws_state == 1) {
		if((uint16_t)(now - ws_conn_tick) >= WS_CONNECT_10MS) {
			ws_state = 2;
			ws_period_tick = now;
			ws_off_ticks = 0;
			if(!ws_icon_on) {
				ws_icon_on = 1;
				Led_SymSet(SYM_HEATER, 1);
				Led_Flush();
			}
			return;
		}
		if((uint16_t)(now - ws_blink_tick) >= WS_BLINK_HALF_10MS) {
			ws_blink_tick = now;
			ws_icon_on = !ws_icon_on;
			Led_SymSet(SYM_HEATER, ws_icon_on);
			Led_Flush();
		}
		return;
	}

	if(ws_off_ticks) {
		if(--ws_off_ticks == 0) {
			ws_period_tick = now;
			ws_icon_on = 1;
			Led_SymSet(SYM_HEATER, 1);
			Led_Flush();
		}
		return;
	}

	if((uint16_t)(now - ws_period_tick) >= WS_PULSE_PERIOD_10MS) {
		ws_off_ticks = WS_PULSE_OFF_10MS;
		ws_icon_on = 0;
		Led_SymSet(SYM_HEATER, 0);
		Led_Flush();
	}
	else if(!ws_icon_on) {
		ws_icon_on = 1;
		Led_SymSet(SYM_HEATER, 1);
		Led_Flush();
	}
}

/* Light the schedule-slot house of the transition that owns "now" under the   */
/* weekly-curve rule: today's last passed transition, else - before the day's  */
/* first transition or on an empty day - the last transition of the most       */
/* recent programmed day (up to 7 days back; see Sched_FindActive). The index  */
/* comes from Sched_ActiveSetpoint's resolve of the shared resolver (it        */
/* refreshes sched_slot_now as a side effect and shares the greatest-minute    */
/* criterion with CONTROL, so the lit house is always the slot feeding the     */
/* relay - a self-contained second copy of the walk cost +346 B clang / +310 B */
/* SDCC and overflowed the SDCC ROM). Encoding: 1-based, 0 = none, which       */
/* survives xdata_clear(). All six houses are off in MANUAL mode and when the  */
/* whole week is empty. The schedule editor draws its own blinking slot.       */
void Sched_SlotApply(void) {
	uint8_t i;

	for(i = 0; i < LNK_SCHED_N; i++) {
		Led_SymSet((uint8_t)(SYM_SP1 + i), 0);
	}
	if(!(settings.progMode & PROG_MODE_SCHEDULE)) {
		return;
	}
	(void)Sched_ActiveSetpoint();      /* refresh the cached index for this paint */
	if(sched_slot_now) {
		Led_SymSet((uint8_t)(SYM_SP1 + sched_slot_now - 1), 1);
	}
}

void Thermo_Apply(void) {
	const temp_sample_t xdata *sample = Temp_DisplaySample();

	bright_level = 0xFF;   /* power change: force a full brightness re-apply */

	if(power_on) {
		/* power-on: max brightness for the hold window (decays to the day/night level) */
		bright_hold = BRIGHTNESS_HOLD_SECS;
		Bright_Apply(LED_BRIGHTNESS_MAX);
		Led_SymSet(SYM_HOME, 1);
		Led_SymSet(SYM_HEATER, ext_ok);
		Led_SymSet(SYM_HEAT, 0);         /* updated by the 1 s control tick */
		Lock_Sym();
		Led_SymSet(SYM_NET,  net_steady);  /* steady when joined the network */
		ProgMode_Icons(settings.progMode);
		Sched_SlotApply();               /* current schedule-slot house (PROG)    */
		Led_Weekday(RTC_GetWeek());      /* light the current day-of-week         */
		clock_colon = 1;                 /* ':' starts lit                        */
		Clock_Draw();
		Temp_Draw(sample);
	}
	else {
		/* standby: display off, only the green ON/OFF leaf glows at the         */
		/* settings day/night level - plus the padlock while the keypad is       */
		/* locked, at that same level (after the 3 s hold it keeps glowing       */
		/* together with the leaf; level 0 = everything dark, as before).        */
		Relay_Set(0);                    /* heating off whenever the unit is off */
		/* Direct LEDs are outside the matrix buffer: clear the weekday row and
		   the flame now, otherwise they survive the OFF repaint. */
		Led_Weekday(0);
		Led_SymSet(SYM_HEAT, 0);
		Led_SymInit();
		Lock_Sym();
		Led_SetBrightness(settings.keypadLockout ?
		                  Bright_LevelBase() : LED_BRIGHTNESS_OFF);
		Led_Flush();
	}
}
/******************************************************************************/
/* OFF provisioning screen: display ON at max, clean shadow buffer (only NET   */
/* toggles over it). Used by the NET blink and restored whenever it is          */
/* interrupted.                                                                */
/* Screen ownership while OFF: the padlock blink (Lock_Flash) > NET blink >   */
/* max-brightness phase > steady standby. Every entry point below therefore    */
/* asks whether lock_flash is running and leaves the screen alone if it is;    */
/* only Lock_Flash() itself paints, and its completion restores the screen.   */
void NetBlink_Screen(void) {
	standby_hold = 0;
	Led_SymInit();
	Led_SetBrightness(LED_BRIGHTNESS_MAX);
	Led_Flush();
}

void Lock_Flash_Start(void) {
	lock_flash = LOCK_FLASH_PHASES;
	lock_flash_tick = BTN_Tick10ms();
	lock_flash_lit = 0;                 /* the first Lock_Flash() flips to ON */
	DEBUG(LOCK_TRACE_EN, { debug_putc('!'); });

	/* Own the screen right now and show a DARK panel: the buffer holds no      */
	/* symbols (Led_SymInit clears + flushes), and the display keeps RUNNING at  */
	/* LOCK_FLASH_LEVEL - from here on only the SYM_LOCK bit is toggled,        */
	/* exactly like Net_Flash() does for SYM_NET (no clock stop, no BLNK). Cut  */
	/* the other OFF phases too (same treatment NetBlink_Screen gives            */
	/* standby_hold): neither the max-brightness phase nor the NET steady hold  */
	/* may re-light the display behind or after the blink.                      */
	standby_hold = 0;
	net_hold = 0;
	Led_SymInit();
	Led_SetBrightness(LOCK_FLASH_LEVEL);
}

/* one 10 ms-driven phase of the padlock blink; call from the main loop while */
/* the thermostat is OFF. Aborts if the unit is turned back on mid-flash.     */
void Lock_Flash(void) {
	uint16_t e;

	if(!lock_flash) return;
	if(power_on) {
		lock_flash = 0;                 /* power changed mid-flash: stop it */
		return;
	}
	e = (uint16_t)(BTN_Tick10ms() - lock_flash_tick);
	if(e < LOCK_FLASH_TICKS) return;
	lock_flash_tick = BTN_Tick10ms();
	/* toggle ONLY the symbol bit - the panel, the clock and the brightness     */
	/* stay untouched for the whole blink (Net_Flash style). The buffer holds   */
	/* nothing else, so the "off" phase is an empty (visually dark) panel.      */
	lock_flash_lit = !lock_flash_lit;
	Led_SymSet(SYM_LOCK, lock_flash_lit);
	DEBUG(LOCK_TRACE_EN, { debug_putc(lock_flash_lit ? (uint8_t)'+' : (uint8_t)'-'); });
	Led_Flush();
	if(--lock_flash == 0) {
		DEBUG(LOCK_TRACE_EN, { debug_putc('#'); trace_tail = 2; });
		/* hand the screen back in priority order: a provisioning blink that was
		   interrupted, the NET steady hold, then the standby phases.            */
		if(net_flash)     NetBlink_Screen();
		else if(standby_hold) Standby_BrightApply();
		else if(net_hold) {   /* a NET steady hold was interrupted: restore it */
			NetBlink_Screen();
			Led_SymSet(SYM_NET, 1);
			Led_Flush();
		}
		else              Thermo_Apply();
	}
}
/********************************************************************************/
/* NET (SYM_NET) blink: >=2 s DOWN hold while OFF arms a 1.5 min blink.         */
void Net_Flash_Start(void) {
	net_flash = 1;
	net_flash_lit = 0;
	net_flash_secs = NET_FLASH_SECS;
	net_flash_tick = BTN_Tick10ms();
	if(power_on) {
		/* hold full brightness for the whole blink */
		Bright_MaxHold();
	}
	else if(!lock_flash) {
		/* OFF: end the touch-key stay phase; while the blink runs the 5 touch */
		/* display stays at FULL brightness                                   */
		/* (only NET toggles), so every flash is identical. A running padlock  */
		/* blink keeps the screen: it restores the NET screen when it ends.    */
		NetBlink_Screen();
	}
}

/* stop the blink now (auto after 1.5 min, or a future ZT3L network event)      */
void Net_Flash_Stop(void) {
	net_flash = 0;
	net_flash_lit = 0;
	Led_SymSet(SYM_NET, net_steady);     /* keep it lit if steady mode is on    */
	if(lock_flash) return;               /* the padlock blink owns the screen;  */
	                                     /* Lock_Flash() restores it at the end */
	if(power_on) {
		Led_Flush();
	}
	else {
		Led_SetBrightness(LED_BRIGHTNESS_OFF);   /* dark screen; standby follows */
		Led_Flush();
	}
}

/* future ZT3L hook: device joined the Zigbee network -> NET on steady forever  */
static void Net_Set_Steady(uint8_t on) {
	net_steady = on;
	if(on) {
		if(net_flash && !power_on && !lock_flash) {
			/* provisioning (OFF DOWN hold) finished: keep the display and the   */
			/* display lit with NET steady for the same time as a normal OFF     */
			/* key-stay phase (STANDBY_BRIGHT_SECS), then the 1 s standby tick     */
			/* fades to the green-leaf-only state (like OFF).                    */
			net_hold  = STANDBY_BRIGHT_SECS;
			standby_hold  = 0;
			/* the same screen a padlock blink restores when it ends here */
			NetBlink_Screen();
			Led_SymSet(SYM_NET, 1);
			Led_Flush();
		}
		net_flash = 0;
		net_flash_lit = 0;
	}
	Led_SymSet(SYM_NET, on);
	if(power_on) Led_Flush();
}

/* Follow the link net status: when the peer reports CONNECTED the NET symbol   */
/* stops blinking and stays lit (shown while ON); otherwise it is cleared.      */
void Net_StatusApply(void) {
	uint8_t conn = (Link_NetStatus() == LNK_NET_CONNECTED) ? 1 : 0;

	if(conn == net_steady) return;
	Net_Set_Steady(conn);
}

/* one 10 ms-driven half-phase of the NET blink; call from the main loop.       */
/* Always drawn OVER the current screen: in OFF it lights the symbol alone on   */
/* the dark display (like the padlock), in ON it toggles it on the live UI.     */
void Net_Flash(void) {
#if !FACTORY_RESET_TEST_EN
	if(net_steady) return;             /* steady NET owns the symbol */
#endif
	if(net_flash) {
		/* auto-stop after NET_FLASH_SECS, counted on the real 10 ms tick */
		if((uint16_t)(BTN_Tick10ms() - net_flash_tick) >= NET_FLASH_TICKS) {
			net_flash_tick = BTN_Tick10ms();
			net_flash_lit = !net_flash_lit;
			/* overlay on the live UI while ON, symbol alone on the dark OFF      */
			/* display; a running padlock blink keeps the screen to itself.       */
			if(power_on || !lock_flash) {
				Led_SymSet(SYM_NET, net_flash_lit);
				Led_Flush();
			}
		}
	}
}
