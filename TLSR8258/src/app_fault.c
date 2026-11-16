#include "app_main.h"
#include "app_fault.h"
#include "zcl_include.h"

u16 zcl_attr_alarmCount = 0;

extern zcl_alarmTable_t g_zcl_alarmTab[];
extern void zcl_alarm_alarmTabClear(void);
extern void zcl_alarm_clearAlarmEntry(zcl_alarmTable_t *pAlarmTab);

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

#define ALARM_RETRY_15S_US  (15 * 1000 * 1000)
#define ALARM_RETRY_60S_US  (60 * 1000 * 1000)

#define ALARM_SNAP_IDLE     0
#define ALARM_SNAP_RETRY15  1
#define ALARM_SNAP_PERIODIC 2
#define ALARM_SNAP_FINAL    3

static u16 app_fault_mask;
static u8 alarm_snapshot_state;
static u8 alarm_sync_seen;
static u32 alarm_snapshot_ref;
static u32 alarm_snapshot_period_us;

static void alarm_dst(epInfo_t *dst) {
	TL_SETSTRUCTCONTENT(*dst, 0);
	dst->dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
	dst->dstAddr.shortAddr = 0x0000;
	dst->dstEp = 1;
	dst->profileId = 0x0104;
}

static zcl_alarmTable_t *alarm_find(u8 code, u16 cluster_id) {
	for(u8 i = 0; i < ZCL_ALARM_TABLE_NUM; i++) {
		zcl_alarmTable_t *e = &g_zcl_alarmTab[i];
		if(e->used && e->endpoint == APP_ENDPOINT1 &&
		   e->alarm.alarmCode == code && e->alarm.clusterId == cluster_id) {
			return e;
		}
	}
	return NULL;
}

static u16 alarm_count_active(void) {
	u16 n = 0;

	for(u8 i = 0; i < ZCL_ALARM_TABLE_NUM; i++) {
		if(g_zcl_alarmTab[i].used && g_zcl_alarmTab[i].endpoint == APP_ENDPOINT1) {
			n++;
		}
	}
	return n;
}

static void alarm_count_report(void) {
	epInfo_t dst;

	alarm_dst(&dst);
	zcl_sendReportCmd(APP_ENDPOINT1, &dst, TRUE, ZCL_FRAME_SERVER_CLIENT_DIR,
	                  ZCL_CLUSTER_GEN_ALARMS, ZCL_ATTRID_ALARM_COUNT,
	                  ZCL_DATA_TYPE_UINT16, (u8 *)&zcl_attr_alarmCount);
}

static u8 alarm_ensure(u8 code, u16 cluster_id) {
	epInfo_t dst;
	alarm_t alarm;

	if(alarm_find(code, cluster_id)) {
		return 0;
	}
	if(zcl_alarm_AddAlarmTab(APP_ENDPOINT1, code, cluster_id,
	                         (u32)clock_time()) != ZCL_STA_SUCCESS) {
		return 0;
	}

	alarm.alarmCode = code;
	alarm.clusterId = cluster_id;
	alarm_dst(&dst);
	if(zcl_alarm_alarmCmd(APP_ENDPOINT1, &dst, TRUE, &alarm) != ZCL_STA_SUCCESS) {
		zcl_alarmTable_t *e = alarm_find(code, cluster_id);
		if(e) {
			zcl_alarm_clearAlarmEntry(e);
		}
		return 0;
	}
	return 1;
}

static void alarm_clear_code(u8 code, u16 cluster_id) {
	zcl_alarmTable_t *e = alarm_find(code, cluster_id);

	if(e) {
		zcl_alarm_clearAlarmEntry(e);
	}
}

static void alarm_sync_code(u8 code, u16 cluster_id, u16 old_on, u16 new_on) {
	if(new_on && !old_on) {
		alarm_ensure(code, cluster_id);
	}
	else if(!new_on && old_on) {
		alarm_clear_code(code, cluster_id);
	}
}

static u8 alarm_ensure_mask(u16 mask) {
	u8 changed = 0;

	if(mask & FAULT_E0) changed |= alarm_ensure(0x00, 0x0000);
	if(mask & FAULT_E1) changed |= alarm_ensure(0x01, 0x0402);
	if(mask & FAULT_E2) changed |= alarm_ensure(0x02, 0x0402);
	if(mask & FAULT_E3) changed |= alarm_ensure(0x03, 0x0402);
	if(mask & FAULT_E4) changed |= alarm_ensure(0x04, 0x0402);
	if(mask & FAULT_E5) changed |= alarm_ensure(0x05, 0x0402);
	if(mask & FAULT_E6) changed |= alarm_ensure(0x06, 0x0402);
	if(mask & FAULT_IN_HI)  changed |= alarm_ensure(0x80, 0x0402);
	if(mask & FAULT_IN_LO)  changed |= alarm_ensure(0x81, 0x0402);
	if(mask & FAULT_EXT_HI) changed |= alarm_ensure(0x82, 0x0402);
	if(mask & FAULT_EXT_LO) changed |= alarm_ensure(0x83, 0x0402);
	return changed;
}

void App_Fault_Init(void) {
	app_fault_mask = 0;
	zcl_attr_alarmCount = 0;
	alarm_snapshot_state = ALARM_SNAP_IDLE;
	alarm_sync_seen = 0;
	alarm_snapshot_ref = 0;
	alarm_snapshot_period_us = 0;
	zcl_alarm_alarmTabClear();
}

static void alarm_announce_active(void) {
	epInfo_t dst;
	alarm_t alarm;

	alarm_dst(&dst);
	for(u8 i = 0; i < ZCL_ALARM_TABLE_NUM; i++) {
		zcl_alarmTable_t *e = &g_zcl_alarmTab[i];
		if(e->used && e->endpoint == APP_ENDPOINT1) {
			alarm.alarmCode = e->alarm.alarmCode;
			alarm.clusterId = e->alarm.clusterId;
			zcl_alarm_alarmCmd(APP_ENDPOINT1, &dst, TRUE, &alarm);
		}
	}
}

static void alarm_snapshot_send(void) {
	alarm_announce_active();
	zcl_attr_alarmCount = alarm_count_active();
	alarm_count_report();
}

void App_Link_FaultSet(uint16_t mask) {
	u16 old = app_fault_mask;
	u8 first = alarm_sync_seen ? 0 : 1;

	if(mask == old && !first) {
		return;
	}
	alarm_sync_seen = 1;

	if(mask != old) {
		alarm_sync_code(0x00, 0x0000, (u16)(old & FAULT_E0), (u16)(mask & FAULT_E0));
		alarm_sync_code(0x01, 0x0402, (u16)(old & FAULT_E1), (u16)(mask & FAULT_E1));
		alarm_sync_code(0x02, 0x0402, (u16)(old & FAULT_E2), (u16)(mask & FAULT_E2));
		alarm_sync_code(0x03, 0x0402, (u16)(old & FAULT_E3), (u16)(mask & FAULT_E3));
		alarm_sync_code(0x04, 0x0402, (u16)(old & FAULT_E4), (u16)(mask & FAULT_E4));
		alarm_sync_code(0x05, 0x0402, (u16)(old & FAULT_E5), (u16)(mask & FAULT_E5));
		alarm_sync_code(0x06, 0x0402, (u16)(old & FAULT_E6), (u16)(mask & FAULT_E6));
		alarm_sync_code(0x80, 0x0402,
		                (u16)(old & FAULT_IN_HI),
		                (u16)(mask & FAULT_IN_HI));
		alarm_sync_code(0x81, 0x0402,
		                (u16)(old & FAULT_IN_LO),
		                (u16)(mask & FAULT_IN_LO));
		alarm_sync_code(0x82, 0x0402,
		                (u16)(old & FAULT_EXT_HI),
		                (u16)(mask & FAULT_EXT_HI));
		alarm_sync_code(0x83, 0x0402,
		                (u16)(old & FAULT_EXT_LO),
		                (u16)(mask & FAULT_EXT_LO));
		app_fault_mask = (u16)mask;
	}

	alarm_snapshot_send();
	alarm_snapshot_ref = (u32)clock_time();
	alarm_snapshot_period_us = ALARM_RETRY_15S_US;
	alarm_snapshot_state = ALARM_SNAP_RETRY15;
}

void App_Fault_Tick(void) {
	u8 changed = alarm_ensure_mask(app_fault_mask);
	u32 now = (u32)clock_time();

	if(changed) {
		alarm_snapshot_send();
		alarm_snapshot_ref = now;
		alarm_snapshot_period_us = ALARM_RETRY_15S_US;
		alarm_snapshot_state = ALARM_SNAP_RETRY15;
		return;
	}

	if(alarm_snapshot_state == ALARM_SNAP_IDLE) {
		return;
	}
	if(!clock_time_exceed(alarm_snapshot_ref, alarm_snapshot_period_us)) {
		return;
	}

	alarm_snapshot_send();
	now = (u32)clock_time();

	if(alarm_snapshot_state == ALARM_SNAP_RETRY15) {
		alarm_snapshot_ref = now;
		alarm_snapshot_period_us = ALARM_RETRY_60S_US;
		alarm_snapshot_state = zcl_attr_alarmCount ? ALARM_SNAP_PERIODIC : ALARM_SNAP_FINAL;
	}
	else if(alarm_snapshot_state == ALARM_SNAP_PERIODIC) {
		if(zcl_attr_alarmCount) {
			alarm_snapshot_ref = now;
			alarm_snapshot_period_us = ALARM_RETRY_60S_US;
		}
		else {
			alarm_snapshot_state = ALARM_SNAP_IDLE;
		}
	}
	else {
		alarm_snapshot_state = ALARM_SNAP_IDLE;
	}
}
