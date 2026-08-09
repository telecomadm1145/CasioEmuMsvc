/* ePS6800 Timer */

/* Event count mode not emulated. */

#include "cpu_internal.h"
#include "eps6800.h"
#include "mmio_internal.h"
#include "timer_internal.h"

#include <string.h>

enum {
	TIMER_PRESCALER_SELECT_MASK = 0x03,
	TIMER_BYTE_MASK = 0xff,
	TIMER_HIGH_BYTE_SHIFT = 8,
	TIMER_INVALID_READ_VALUE = 0xff,
	TIMER_OSC_EXTERNAL_PRESCALER = 2,
	TIMER_OSC_INTERNAL_PRESCALER = 30
};
static const int32_t TIMER0_PRESCALERS[] = { 1, 4, 16, 64 };
static const int32_t TIMER1_PRESCALERS[] = { 4, 16, 64, 256 };
static const int32_t TIMER2_PRESCALERS[] = { 1, 2, 4, 8 };

void timer_connect_bus_state(struct timer_state *state, struct cpu_state *cpu, struct mmio_state *mmio) {
	state->cpu = cpu;
	state->mmio = mmio;
}

static void timer_bus_write_internal(struct timer_state *state, uint8_t addr, uint8_t byte) {
	mmio_write_byte_internal_state(state->mmio, addr, byte);
}

static void timer_cpu_interrupt(struct timer_state *state, uint8_t int_level) {
	cpu_interrupt_state(state->cpu, int_level);
}

static void timer_signal_interrupt(struct timer_state *state, uint8_t flag, uint8_t control, uint8_t enable_bit) {
	state->reg[REG_INTSTA] |= flag;
	timer_bus_write_internal(state, REG_INTSTA, state->reg[REG_INTSTA]);
	if (control & enable_bit) {
		timer_cpu_interrupt(state, INT_LEVEL4_TIMINT);
	}
}

static uint8_t timer_counter_high_byte(int32_t counter) {
	return (uint8_t)((counter >> TIMER_HIGH_BYTE_SHIFT) & TIMER_BYTE_MASK);
}

static uint8_t timer_counter_low_byte(int32_t counter) {
	return (uint8_t)(counter & TIMER_BYTE_MASK);
}

static uint16_t timer_counter_word(uint8_t high, uint8_t low) {
	return (uint16_t)(((uint16_t)high << TIMER_HIGH_BYTE_SHIFT) | low);
}

static int32_t timer_selected_prescaler(const int32_t *prescalers, uint8_t control) {
	return prescalers[control & TIMER_PRESCALER_SELECT_MASK];
}

static bool timer_enabled(uint8_t control, uint8_t enable_bit) {
	return (control & enable_bit) != 0;
}

static bool timer_tick_counter(
	int32_t *prescaler_counter,
	int32_t prescaler_reload,
	int32_t *timer_counter,
	int32_t timer_reload,
	uint32_t cycles
) {
	int32_t rem;
	int32_t dec;

	if (*prescaler_counter > cycles) {
		*prescaler_counter -= cycles;
		return false;
	}

	rem = (int32_t)cycles - *prescaler_counter;
	dec = 1;
	if (rem >= prescaler_reload) {
		dec += rem / prescaler_reload;
		rem = rem % prescaler_reload;
	}
	*prescaler_counter = prescaler_reload - rem;
	if (*timer_counter > dec) {
		*timer_counter -= dec;
		return false;
	}

	rem = dec - timer_reload;
	if (rem >= timer_reload) {
		rem = timer_reload; /* Emulation resolution is not enough. */
	}
	*timer_counter = timer_reload - rem;
	return true;
}

static void timer_recalc_0(struct timer_state *state) {
	int32_t osc_psc = (state->reg[REG_TR0CON] & BIT_T0CS) ?
		TIMER_OSC_EXTERNAL_PRESCALER : TIMER_OSC_INTERNAL_PRESCALER;
	int32_t tmr_psc = timer_selected_prescaler(TIMER0_PRESCALERS, state->reg[REG_TR0CON]);
	state->t0prl = osc_psc * tmr_psc;
	state->t0crl = timer_counter_word(state->reg[REG_TRL0H], state->reg[REG_TRL0L]);
	state->t0psc = state->t0prl;
	state->t0cnt = state->t0crl;
}

static void timer_recalc_1(struct timer_state *state) {
	state->t1prl = timer_selected_prescaler(TIMER1_PRESCALERS, state->reg[REG_TR1CON]);
	state->t1crl = state->reg[REG_TRL1];
	state->t1psc = state->t1prl;
	state->t1cnt = state->t1crl;
}

static void timer_recalc_2(struct timer_state *state) {
	int32_t osc_psc = (state->reg[REG_TR2WCON] & BIT_T2CS) ?
		TIMER_OSC_EXTERNAL_PRESCALER : TIMER_OSC_INTERNAL_PRESCALER;
	int32_t tmr_psc = timer_selected_prescaler(TIMER2_PRESCALERS, state->reg[REG_TR2WCON]);
	state->t2prl = osc_psc * tmr_psc;
	state->t2crl = state->reg[REG_TRL2];
	state->t2psc = state->t2prl;
	state->t2cnt = state->t2crl;
}

uint8_t timer_read_byte_state(struct timer_state *state, uint8_t addr) {
	uint8_t byte;
	if (addr < TIMER_REG_COUNT) {
		switch (addr) {
		case REG_T0CH:
			byte = timer_counter_high_byte(state->t0cnt);
			break;
		case REG_T0CL:
			byte = timer_counter_low_byte(state->t0cnt);
			break;
		default:
			byte = state->reg[addr];
		}
	}
	else {
		byte = TIMER_INVALID_READ_VALUE;
		mmio_bad_read_byte_state(state->mmio, addr);
	}
	return byte;
}

void timer_write_byte_state(struct timer_state *state, uint8_t addr, uint8_t byte) {
	if (addr < TIMER_REG_COUNT) {
		state->reg[addr] = byte;
		switch (addr) {
		case REG_TRL0H:
		case REG_TRL0L:
		case REG_TR0CON:
			timer_recalc_0(state);
			break;
		case REG_TRL1:
		case REG_TR1CON:
			timer_recalc_1(state);
			break;
		case REG_TRL2:
		case REG_TR2WCON:
			timer_recalc_2(state);
			break;
		}
	}
	else {
		mmio_bad_write_byte_state(state->mmio, addr);
	}
}

void timer_reset_state(struct timer_state *state) {
	memset(state->reg, 0, sizeof(state->reg));
}

void timer_tick_state(struct timer_state *state, uint32_t cycles) {
	if (timer_enabled(state->reg[REG_TR0CON], BIT_T0EN)) {
		if (timer_tick_counter(&state->t0psc, state->t0prl, &state->t0cnt, state->t0crl, cycles)) {
			timer_signal_interrupt(state, BIT_TMR0I, state->reg[REG_TR0CON], BIT_TMR0IE);
		}
	}

	if (timer_enabled(state->reg[REG_TR1CON], BIT_T1EN)) {
		if (timer_tick_counter(&state->t1psc, state->t1prl, &state->t1cnt, state->t1crl, cycles)) {
			timer_signal_interrupt(state, BIT_TMR1I, state->reg[REG_TR1CON], BIT_TMR1IE);
		}
	}

	if (timer_enabled(state->reg[REG_TR2WCON], BIT_T2EN)) {
		if (timer_tick_counter(&state->t2psc, state->t2prl, &state->t2cnt, state->t2crl, cycles)) {
			timer_signal_interrupt(state, BIT_TMR2I, state->reg[REG_TR2WCON], BIT_TMR2IE);
		}
	}
}


