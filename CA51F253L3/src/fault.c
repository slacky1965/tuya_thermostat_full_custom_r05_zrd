#include "include/config.h"
#include "include/stdint.h"
#include "include/fault.h"
#include "include_common/thermostat.h"

static uint16_t xdata fault_mask;
static bit fault_changed;
static uint8_t xdata fault_report_needed;

static void fault_set_bit(uint16_t mask_bit, uint8_t on) {
	uint16_t next = fault_mask;

	if(on) {
		next |= mask_bit;
	}
	else {
		next &= (uint16_t)~mask_bit;
	}
	if(next != fault_mask) {
		fault_mask = next;
		fault_changed = 1;
	}
}

void Fault_Init(void) {
	fault_mask = 0;
	fault_changed = 0;
	fault_report_needed = 1;
}

void Fault_UpdateSensor(uint8_t sensor, temp_status_t st, int16_t c100) {
	uint16_t clear;
	uint16_t e_timeout;
	uint16_t e_short;
	uint16_t e_open;
	uint16_t hi;
	uint16_t lo;

	if(sensor == TEMP_INTERNAL) {
		clear = FAULT_E1 | FAULT_E2 | FAULT_E5 | FAULT_IN_HI | FAULT_IN_LO;
		e_timeout = FAULT_E5;
		e_short = FAULT_E2;
		e_open = FAULT_E1;
		hi = FAULT_IN_HI;
		lo = FAULT_IN_LO;
	}
	else {
		clear = FAULT_E3 | FAULT_E4 | FAULT_E6 | FAULT_EXT_HI | FAULT_EXT_LO;
		e_timeout = FAULT_E6;
		e_short = FAULT_E4;
		e_open = FAULT_E3;
		hi = FAULT_EXT_HI;
		lo = FAULT_EXT_LO;
	}

	fault_set_bit(clear, 0);

	switch(st) {
	case TEMP_ST_TIMEOUT:
		fault_set_bit(e_timeout, 1);
		break;
	case TEMP_ST_SHORT:
		fault_set_bit(e_short, 1);
		break;
	case TEMP_ST_OPEN:
		fault_set_bit(e_open, 1);
		break;
	case TEMP_ST_OK:
	default:
		if(c100 == TEMP_ERR) {
			/* Invalid read reported as OK: never treat the sentinel as a
			   temperature (0x7FFF would otherwise look like an overheat). */
			fault_set_bit(e_timeout, 1);
			break;
		}
		if(c100 > ABS_MAX_HEATSETPOINT_LIMIT) {
			fault_set_bit(hi, 1);
		}
		else if(c100 < ABS_MIN_HEATSETPOINT_LIMIT) {
			fault_set_bit(lo, 1);
		}
		break;
	}
}


uint16_t Fault_Mask(void) {
	return fault_mask;
}

uint8_t Fault_Changed(void) {
	return (uint8_t)fault_changed;
}

void Fault_ClearChanged(void) {
	fault_changed = 0;
}

uint8_t Fault_ReportNeeded(void) {
	return fault_report_needed;
}

void Fault_ReportDone(void) {
	fault_report_needed = 0;
}

void Fault_RequestReport(void) {
	fault_report_needed = 1;
}

static uint16_t fault_source_bits(uint8_t temp_src) {
	if(temp_src == SENSOR_SRC_IN) {
		return FAULT_E1 | FAULT_E2 | FAULT_E5 | FAULT_IN_HI | FAULT_IN_LO;
	}
	if(temp_src == SENSOR_SRC_OUT) {
		return FAULT_E3 | FAULT_E4 | FAULT_E6 | FAULT_EXT_HI | FAULT_EXT_LO;
	}
	return 0xFFFF;
}

uint8_t Fault_DisplayCodeFor(uint8_t temp_src) {
	uint16_t m = fault_mask & (fault_source_bits(temp_src) | FAULT_E7 | FAULT_E0);

	if(m & FAULT_E1) return 1;
	if(m & FAULT_E2) return 2;
	if(m & FAULT_E3) return 3;
	if(m & FAULT_E4) return 4;
	if(m & FAULT_E5) return 5;
	if(m & FAULT_E6) return 6;
	if(m & FAULT_E0) return 0;
	return 0xFF;
}

uint8_t Fault_DisplayCode(void) {
	return Fault_DisplayCodeFor(SENSOR_SRC_ALL);
}

uint8_t Fault_DisplayRangeFor(uint8_t temp_src) {
	uint16_t m = fault_mask & fault_source_bits(temp_src);

	if(m & (FAULT_IN_HI | FAULT_EXT_HI)) {
		return FAULT_RANGE_UP;
	}
	if(m & (FAULT_IN_LO | FAULT_EXT_LO)) {
		return FAULT_RANGE_LO;
	}
	return FAULT_RANGE_NONE;
}

uint8_t Fault_DisplayRange(void) {
	return Fault_DisplayRangeFor(SENSOR_SRC_ALL);
}
