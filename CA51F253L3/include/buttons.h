#ifndef _BUTTONS_H_
#define _BUTTONS_H_
#include "include/stdint.h"
/***********************************************************************************/
/* 4 GPIO keys of the r05 front panel (SC05B parallel outputs).                     */
/* Pin map: see config.h section 4 - single source of truth.                        */
typedef enum
{
	BTN_MENU  = 0,   /* P3.6, menu / square icon     */
	BTN_POWER = 1,   /* P3.7, leaf "on/off" icon     */
	BTN_UP    = 2,   /* P3.3, up arrow               */
	BTN_DOWN  = 3    /* P3.2, down arrow             */
} btn_id_t;

/***********************************************************************************/
/* Press/release events produced by BTN_Scan(), delivered via BTN_GetEvent().      */
/* One gesture yields at most one "terminal" event, so the app uses it directly:   */
/*   - short press (press + quick release)          -> BTN_EVT_CLICK               */
/*   - hold longer than the hold time               -> BTN_EVT_HOLD   (once)       */
/*   - while still holding, every repeat time       -> BTN_EVT_HOLD_REPEAT         */
/*   - release AFTER a HOLD has fired               -> BTN_EVT_RELEASE             */
/* A PRESS edge is not queued on purpose: apps that need it can poll BTN_IsDown(). */
typedef enum
{
	BTN_EVT_NONE        = 0x00,  /* nothing pending                           */
	BTN_EVT_CLICK       = 0x01,  /* short press completed (released quickly)  */
	BTN_EVT_HOLD        = 0x02,  /* long press reached (key still held)       */
	BTN_EVT_HOLD_REPEAT = 0x03,  /* auto-repeat while holding                 */
	BTN_EVT_RELEASE     = 0x04,  /* release after a long hold                 */
	BTN_EVT_CHORD       = 0x05,  /* UP+DOWN held together (keypad lock)       */
	BTN_EVT_CHORD2      = 0x06   /* POWER+MENU held together (Eco toggle)     */
} btn_evt_t;

#define BTN_COUNT   4

/* Two independent two-button chords, either press order:                        */
/*   chord 1: BTN_UP + BTN_DOWN      -> keypad lock  (BTN_EVT_CHORD,  id 5)      */
/*   chord 2: BTN_POWER + BTN_MENU   -> Eco toggle    (BTN_EVT_CHORD2, id 6)     */
/* The POWER+MENU pair is the Eco toggle on r05. While both keys of a chord are   */
/* down they emit NO individual events; once both have stayed down for the pair's */
/* 10MS_TICKS (total, from the FIRST press) a single event is queued with the      */
/* pair's pseudo id. Driver-level gestures: the state machine runs per pair; the   */
/* app decides what each event means (see main.c).                                */
/* Staggered presses: a lone chord key does not start its individual HOLD          */
/* until BTN_CHORD_GRACE_10MS_TICKS has passed (partner window), so pressing       */
/* e.g. UP 100 ms before DOWN never leaks a lone "up" HOLD/REPEAT.                */
#define BTN_CHORD   5      /* pseudo key id used in chord events               */
#define BTN_CHORD_A  BTN_UP
#define BTN_CHORD_B  BTN_DOWN
#define BTN_CHORD_10MS_TICKS   200       /* 2.0 s with both held -> BTN_EVT_CHORD            */
#define BTN_CHORD2  6                    /* pseudo key id used in the second chord events    */
#define BTN_CHORD2_A  BTN_POWER
#define BTN_CHORD2_B  BTN_MENU
#define BTN_CHORD2_10MS_TICKS  200       /* 2.0 s -> BTN_EVT_CHORD2                */
#define BTN_CHORD_GRACE_10MS_TICKS  25   /* 250 ms partner window                  */

/* timing in real 10 ms ticks, advanced from the Timer0 10 ms ISR in main.c.  */
/* The GPIO scan runs on the same 10 ms grid; HOLD/REPEAT use real time.      */
#define BTN_HOLD_10MS_TICKS     50    /* 50 x 10 ms = 0.5 s until HOLD fires  */
#define BTN_REPEAT_10MS_TICKS    6    /* 6 x 10 ms = 60 ms between REPEAT     */

extern uint16_t BTN_Tick10ms(void);  /* current tick10ms, wraps 0..65535, from main.c */
/*****************************************************************************/
void BTN_Init(void);    /* configure the four GPIO key lines, clear state              */
void BTN_Scan(void);    /* one scan + debounce + event generation; call periodically   */

uint8_t  BTN_IsDown(btn_id_t b);            /* debounced press state of one key                         */
uint8_t  BTN_AnyDown(void);                 /* 1 if any key is held                                     */
uint8_t  Btn_ActionAllowed(btn_id_t key);   /* keypad lock (settings.keypadLockout) allows key?         */
uint8_t  BTN_GetEvent(btn_id_t xdata *b);   /* next queued event (btn_evt_t, 0 = none), *b = key        */
int16_t  BTN_Delta(btn_id_t b);             /* bench diagnostic: always 0 on GPIO keys                  */
uint16_t BTN_Raw(btn_id_t b);               /* last debounced pressed state (0/1)                       */
uint16_t BTN_Baseline(btn_id_t b);          /* bench diagnostic: always 0 on GPIO keys                  */
uint16_t BTN_Ref(void);                     /* bench diagnostic: always 0 on GPIO keys                  */

#endif
