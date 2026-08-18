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

static void timer_cpu_wake(struct timer_state *state, uint8_t wake_source) {
	cpu_wake_state(state->cpu, wake_source);
}

static void timer_signal_interrupt(struct timer_state *state, uint8_t flag, uint8_t control, uint8_t enable_bit) {
	const uint8_t reg_status = eps_reg_trintsta(state->mmio->variant);
	state->reg[reg_status] |= flag;
	timer_bus_write_internal(state, reg_status, state->reg[reg_status]);
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

static uint8_t timer_t1_prescaler_control(const struct timer_state *state) {
	const uint8_t control = state->reg[eps_reg_tr1con(state->mmio->variant)];
	return eps_variant_is_6009(state->mmio->variant) ? (uint8_t)(control >> 4) : control;
}

static uint8_t timer_bit_t1en(const struct timer_state *state) {
	return eps_variant_is_6009(state->mmio->variant) ? 0x40u : BIT_T1EN;
}

static uint8_t timer_bit_t1ie(const struct timer_state *state) {
	return eps_variant_is_6009(state->mmio->variant) ? BIT_TMR1I : BIT_TMR1IE;
}

static uint8_t timer_bit_t2ie(const struct timer_state *state) {
	return eps_variant_is_6009(state->mmio->variant) ? BIT_TMR2I : BIT_TMR2IE;
}

static int32_t timer_eps6009_t1_reload(const struct timer_state *state) {
	const uint8_t reload = state->reg[eps_reg_trl1(state->mmio->variant)];
	if (reload != 0) {
		return 16 * (int32_t)reload;
	}
	return 16 * (1 << (2 * (timer_t1_prescaler_control(state) & TIMER_PRESCALER_SELECT_MASK)));
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

	/* The counter is already at or below dec here; compute the residual from
	 * the current counter value, not from the reload, so the post-overflow
	 * counter reflects the remaining ticks. */
	rem = dec - *timer_counter;
	if (rem >= timer_reload) {
		rem = timer_reload; /* Emulation resolution is not enough. */
	}
	*timer_counter = timer_reload - rem;
	return true;
}

static bool timer_tick_eps6009_t1_counter(struct timer_state *state, uint32_t cycles) {
	bool overflow = false;
	uint32_t ticks;

	if (cycles == 0) {
		return false;
	}
	if (state->t1psc < state->t1prl) {
		const uint32_t needed = (uint32_t)(state->t1prl - state->t1psc);
		if (cycles < needed) {
			state->t1psc += (int32_t)cycles;
			return false;
		}
		state->t1psc = state->t1prl;
		ticks = 1;
		cycles -= needed;
	}
	else {
		ticks = 0;
	}
	ticks += cycles;

	while (ticks != 0) {
		if (state->t1cnt != 0) {
			const uint32_t decrement = ((uint32_t)state->t1cnt < ticks) ? (uint32_t)state->t1cnt : ticks;
			state->t1cnt -= (int32_t)decrement;
			ticks -= decrement;
		}
		else {
			state->t1cnt = state->t1crl;
			overflow = true;
			--ticks;
		}
	}
	return overflow;
}

static void timer_recalc_0(struct timer_state *state) {
	const uint8_t reg_tr0con = eps_reg_tr0con(state->mmio->variant);
	int32_t osc_psc = (state->reg[reg_tr0con] & BIT_T0CS) ?
		TIMER_OSC_EXTERNAL_PRESCALER : TIMER_OSC_INTERNAL_PRESCALER;
	int32_t tmr_psc = timer_selected_prescaler(TIMER0_PRESCALERS, state->reg[reg_tr0con]);
	state->t0prl = osc_psc * tmr_psc;
	state->t0crl = timer_counter_word(state->reg[eps_reg_trl0h(state->mmio->variant)],
		state->reg[eps_reg_trl0l(state->mmio->variant)]);
	state->t0psc = state->t0prl;
	state->t0cnt = state->t0crl;
}

static void timer_recalc_1(struct timer_state *state) {
	if (eps_variant_is_6009(state->mmio->variant)) {
		state->t1prl = timer_selected_prescaler(TIMER1_PRESCALERS, timer_t1_prescaler_control(state));
		state->t1crl = timer_eps6009_t1_reload(state);
		state->t1psc = 0;
		state->t1cnt = state->t1crl;
		return;
	}
	state->t1prl = timer_selected_prescaler(TIMER1_PRESCALERS, timer_t1_prescaler_control(state));
	state->t1crl = state->reg[eps_reg_trl1(state->mmio->variant)];
	state->t1psc = state->t1prl;
	state->t1cnt = state->t1crl;
}

static void timer_update_1_reload(struct timer_state *state) {
	if (eps_variant_is_6009(state->mmio->variant)) {
		state->t1prl = timer_selected_prescaler(TIMER1_PRESCALERS, timer_t1_prescaler_control(state));
		state->t1crl = timer_eps6009_t1_reload(state);
		return;
	}
	timer_recalc_1(state);
}

static void timer_recalc_2(struct timer_state *state) {
	const uint8_t reg_tr2wcon = eps_reg_tr2wcon(state->mmio->variant);
	int32_t osc_psc = (state->reg[reg_tr2wcon] & BIT_T2CS) ?
		TIMER_OSC_EXTERNAL_PRESCALER : TIMER_OSC_INTERNAL_PRESCALER;
	int32_t tmr_psc = timer_selected_prescaler(TIMER2_PRESCALERS, state->reg[reg_tr2wcon]);
	state->t2prl = osc_psc * tmr_psc;
	state->t2crl = state->reg[eps_reg_trl2(state->mmio->variant)];
	state->t2psc = state->t2prl;
	state->t2cnt = state->t2crl;
}

uint8_t timer_read_byte_state(struct timer_state *state, uint8_t addr) {
	uint8_t byte;
	if (addr < TIMER_REG_COUNT) {
		switch (addr) {
		case REG_T0CH:
			byte = eps_variant_is_6009(state->mmio->variant) ? state->reg[addr] : timer_counter_high_byte(state->t0cnt);
			break;
		case REG_T0CL:
			byte = eps_variant_is_6009(state->mmio->variant) ? state->reg[addr] : timer_counter_low_byte(state->t0cnt);
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
		if (eps_variant_is_6009(state->mmio->variant)) {
			const uint8_t old_byte = state->reg[addr];
			state->reg[addr] = byte;
			if (addr == eps_reg_tr0con(state->mmio->variant)) {
				if (!timer_enabled(old_byte, BIT_T0EN) && timer_enabled(byte, BIT_T0EN)) {
					timer_recalc_0(state);
				}
				if (!timer_enabled(old_byte, timer_bit_t1en(state)) && timer_enabled(byte, timer_bit_t1en(state))) {
					timer_recalc_1(state);
				}
				else {
					timer_update_1_reload(state);
				}
			}
			else if (addr == eps_reg_trl0h(state->mmio->variant) ||
				addr == eps_reg_trl0l(state->mmio->variant)) {
				timer_recalc_0(state);
			}
			else if (addr == eps_reg_trl1(state->mmio->variant)) {
				timer_update_1_reload(state);
			}
			else if (addr == eps_reg_tr2wcon(state->mmio->variant) ||
				addr == eps_reg_trl2(state->mmio->variant)) {
				timer_recalc_2(state);
			}
			return;
		}
		state->reg[addr] = byte;
		switch (addr) {
		case REG_TRL0H:
		case REG_TRL0L:
		case REG_TR0CON:
			timer_recalc_0(state);
			if (eps_reg_tr1con(state->mmio->variant) == addr)
				timer_recalc_1(state);
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

static void timer_tick_0_state(struct timer_state *state, uint32_t cycles) {
	const uint8_t reg_tr0con = eps_reg_tr0con(state->mmio->variant);
	const uint8_t control = state->reg[reg_tr0con];
	const uint8_t interrupt_control = eps_variant_is_6009(state->mmio->variant) ? state->reg[0x21] : control;
	if (timer_enabled(control, BIT_T0EN)) {
		if (timer_tick_counter(&state->t0psc, state->t0prl, &state->t0cnt, state->t0crl, cycles)) {
			timer_signal_interrupt(state, BIT_TMR0I, interrupt_control, eps_variant_is_6009(state->mmio->variant) ? BIT_TMR0I : BIT_TMR0IE);
		}
	}
}

static void timer_tick_1_state(struct timer_state *state, uint32_t cycles) {
	const uint8_t reg_tr1con = eps_reg_tr1con(state->mmio->variant);
	const uint8_t control = state->reg[reg_tr1con];
	const uint8_t interrupt_control = eps_variant_is_6009(state->mmio->variant) ? state->reg[0x21] : control;
	if (timer_enabled(control, timer_bit_t1en(state))) {
		const bool overflow = eps_variant_is_6009(state->mmio->variant) ?
			timer_tick_eps6009_t1_counter(state, cycles) :
			timer_tick_counter(&state->t1psc, state->t1prl, &state->t1cnt, state->t1crl, cycles);
		if (overflow) {
			timer_signal_interrupt(state, BIT_TMR1I, interrupt_control, timer_bit_t1ie(state));
			if (control & BIT_T1WKEN) {
				timer_cpu_wake(state, WAKE_TIMER);
			}
		}
	}
}

static void timer_tick_2_state(struct timer_state *state, uint32_t cycles) {
	const uint8_t reg_tr2wcon = eps_reg_tr2wcon(state->mmio->variant);
	const uint8_t control = state->reg[reg_tr2wcon];
	const uint8_t interrupt_control = eps_variant_is_6009(state->mmio->variant) ? state->reg[0x21] : control;
	if (timer_enabled(control, BIT_T2EN)) {
		if (timer_tick_counter(&state->t2psc, state->t2prl, &state->t2cnt, state->t2crl, cycles)) {
			timer_signal_interrupt(state, BIT_TMR2I, interrupt_control, timer_bit_t2ie(state));
		}
	}
}

void timer_tick_state(struct timer_state *state, uint32_t cycles) {
	timer_tick_0_state(state, cycles);
	timer_tick_1_state(state, cycles);
	timer_tick_2_state(state, cycles);
}

void timer_tick_fast_state(struct timer_state *state, uint32_t cycles) {
	timer_tick_0_state(state, cycles);
	timer_tick_2_state(state, cycles);
}

void timer_tick_idle_state(struct timer_state *state, uint32_t cycles) {
	/* The low-speed Timer1 oscillator remains active in Idle.  Timer0 and
	 * Timer2 are driven only while the CPU oscillator is running. */
	timer_tick_1_state(state, cycles);
}


