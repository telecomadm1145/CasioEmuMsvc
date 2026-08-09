/* ePS6800 Keypad Emulation */

#include "cpu_internal.h"
#include "eps6800.h"
#include "kbd_internal.h"
#include "mmio_internal.h"

#include <string.h>

void kbd_connect_bus_state(struct kbd_state *state, struct cpu_state *cpu, struct mmio_state *mmio) {
	state->cpu = cpu;
	state->mmio = mmio;
}

enum {
	KEY_PRESS_DELAY_CYCLES = 1000,
	KEY_RELEASE_HOLD_CYCLES = 1000,
	KEY_SCAN_BUSY_ADDR = KBD_REG_COUNT,
	KEY_SCAN_BUSY_MASK = 0x10,
	KBD_MATRIX_IDLE = 0xff,
	KBD_ON_COLUMN_MASK = 0x80,
	KBD_INVALID_READ_VALUE = 0xff,
	KBD_DCR_RESET = 0xff
};

static uint8_t kbd_bus_read_internal(struct kbd_state *state, uint8_t addr) {
	return mmio_read_byte_internal_state(state->mmio, addr);
}

static void kbd_bus_write_internal(struct kbd_state *state, uint8_t addr, uint8_t byte) {
	mmio_write_byte_internal_state(state->mmio, addr, byte);
}

static bool kbd_cpu_is_sleep_repeating(const struct kbd_state *state) {
	return cpu_is_sleep_repeating_state(state->cpu);
}

static void kbd_cpu_interrupt(struct kbd_state *state, uint8_t int_level) {
	cpu_interrupt_state(state->cpu, int_level);
}

static void kbd_cpu_wake(struct kbd_state *state, uint8_t source) {
	cpu_wake_state(state->cpu, source);
}

static uint8_t kbd_scan_row_mask(uint8_t row) {
	return (uint8_t)(1 << row);
}

/*
 * The keyboard is an undioded PA/PB switch matrix.  A closed switch joins one
 * PA line to one PB line, so three corners of a rectangle can pull the fourth
 * corner low while firmware is scanning.  Propagate the low level through the
 * bipartite graph instead of merely ANDing the selected PB rows; this also
 * preserves simultaneous keys in the same row/column.
 *
 * DCR bit 0 means output.  Inputs and high outputs do not seed a low level;
 * low outputs do.  In an output conflict the low level wins, matching the
 * conservative electrical behaviour expected by the calculator firmware.
 */
static uint8_t kbd_get_matrix(const struct kbd_state *state) {
	uint8_t low_pa = (uint8_t)~(state->reg[REG_DCRA] | state->porta_latch);
	uint8_t low_pb = (uint8_t)~(state->reg[REG_DCRB] | state->portb_latch);
	bool changed;

	do {
		int pb;
		uint8_t next_low_pa = low_pa;
		uint8_t next_low_pb = low_pb;

		for (pb = 0; pb < KBD_ROW_COUNT; ++pb) {
			const uint8_t closed_pa = (uint8_t)~state->matrix[pb];
			if (low_pb & kbd_scan_row_mask((uint8_t)pb)) {
				next_low_pa |= closed_pa;
			}
			if (closed_pa & low_pa) {
				next_low_pb |= kbd_scan_row_mask((uint8_t)pb);
			}
		}

		changed = next_low_pa != low_pa || next_low_pb != low_pb;
		low_pa = next_low_pa;
		low_pb = next_low_pb;
	} while (changed);

	if (state->on_pressed) {
		low_pa |= KBD_ON_COLUMN_MASK;
	}
	return (uint8_t)~low_pa;
}

static uint8_t kbd_get_pa(const struct kbd_state *state) {
	uint8_t input = kbd_get_matrix(state);
	return (input & state->reg[REG_DCRA]) | (state->porta_latch & ~state->reg[REG_DCRA]);
}

static void kbd_update_porta(struct kbd_state *state) {
	uint8_t pa = kbd_get_pa(state);

	state->reg[REG_PORTA] = pa;
	kbd_bus_write_internal(state, REG_PORTA, pa);
}

static uint8_t kbd_active_column_mask(const struct kbd_state *state) {
	int row;
	uint8_t mask = 0;
	for (row = 0; row < KBD_ROW_COUNT; row++) {
		mask |= (uint8_t)~state->matrix[row];
	}
	if (state->on_pressed) {
		mask |= KBD_ON_COLUMN_MASK;
	}
	return mask;
}

static void kbd_trigger_press(struct kbd_state *state, uint8_t mask) {
	uint8_t interrupt_mask = mask & state->reg[REG_PAINTEN];
	uint8_t wake_mask = mask & state->reg[REG_PAWAKE];

	if (interrupt_mask && (kbd_bus_read_internal(state, REG_CPUCON) & BIT_GLINT)) {
		state->reg[REG_PAINTSTA] |= interrupt_mask;
		kbd_bus_write_internal(state, REG_PAINTSTA, state->reg[REG_PAINTSTA]);
		kbd_cpu_interrupt(state, INT_LEVEL1_PAINT);
	}
	else if (wake_mask) {
		kbd_cpu_wake(state, WAKE_PAINT);
	}
}

static void kbd_queue_press(struct kbd_state *state, uint8_t mask) {
	state->pending_press_mask |= mask;
}

static bool kbd_key_valid(uint8_t key) {
	return key < KBD_KEY_COUNT;
}

static uint8_t kbd_key_row(uint8_t key) {
	return key / KBD_COL_COUNT;
}

static uint8_t kbd_key_col(uint8_t key) {
	return key % KBD_COL_COUNT;
}

static uint8_t kbd_key_mask(uint8_t key) {
	return (uint8_t)(1 << kbd_key_col(key));
}

static bool kbd_countdown_elapsed(uint16_t *counter, uint32_t cycles) {
	if (cycles >= *counter) {
		*counter = 0;
		return true;
	}

	*counter = (uint16_t)(*counter - cycles);
	return false;
}

static bool kbd_scan_busy(struct kbd_state *state) {
	return (kbd_bus_read_internal(state, KEY_SCAN_BUSY_ADDR) & KEY_SCAN_BUSY_MASK) != 0;
}

static void kbd_process_pending_press(struct kbd_state *state) {
	uint8_t active_mask = kbd_active_column_mask(state);
	uint8_t ready_mask;

	state->pending_press_mask &= active_mask;
	ready_mask = state->pending_press_mask & (state->reg[REG_PAINTEN] | state->reg[REG_PAWAKE]);
	if (!ready_mask || !kbd_cpu_is_sleep_repeating(state)) {
		return;
	}

	state->pending_press_mask &= ~ready_mask;
	kbd_trigger_press(state, ready_mask);
}

static void kbd_schedule_release_hold(struct kbd_state *state, uint8_t key) {
	state->key_pending_up[key] = false;
	state->key_release_cycles[key] = KEY_RELEASE_HOLD_CYCLES;
}

uint8_t kbd_read_byte_state(struct kbd_state *state, uint8_t addr) {
	uint8_t byte;
	if (addr < KBD_REG_COUNT) {
		switch (addr) {
		case REG_PORTA:
			kbd_update_porta(state);
			byte = state->reg[REG_PORTA];
			break;
		case REG_PORTB:
			byte = state->portb_latch;
			break;
		case REG_PORTC:
			byte = state->portc_latch;
			break;
		default:
			byte = state->reg[addr];
		}
	}
	else {
		byte = KBD_INVALID_READ_VALUE;
		mmio_bad_read_byte_state(state->mmio, addr);
	}
	return byte;
}

void kbd_write_byte_state(struct kbd_state *state, uint8_t addr, uint8_t byte) {
	if (addr < KBD_REG_COUNT) {
		state->reg[addr] = byte;
		switch (addr) {
		case REG_PORTA:
			state->porta_latch = byte;
			break;
		case REG_PORTB:
			state->portb_latch = byte;
			break;
		case REG_PORTC:
			state->portc_latch = byte;
			break;
		case REG_DCRA:
		case REG_DCRB:
		case REG_PAINTEN:
		case REG_PAWAKE:
			kbd_process_pending_press(state);
			break;
		case REG_PAINTSTA:
			kbd_bus_write_internal(state, REG_PAINTSTA, state->reg[REG_PAINTSTA]);
			break;
		}
	}
	else {
		mmio_bad_write_byte_state(state->mmio, addr);
	}
}

void kbd_keydown_state(struct kbd_state *state, uint8_t key) {
	if (!kbd_key_valid(key)) {
		return;
	}

	state->key_release_cycles[key] = 0;
	state->key_pending_down[key] = true;
	state->key_pending_up[key] = false;
	state->key_press_cycles[key] = KEY_PRESS_DELAY_CYCLES;
}

static void kbd_activate_key(struct kbd_state *state, uint8_t key) {
	if (!kbd_key_valid(key)) {
		return;
	}

	uint8_t row = kbd_key_row(key);
	uint8_t mask = kbd_key_mask(key);
	if (state->matrix[row] & mask) {
		state->matrix[row] &= ~mask;
		kbd_queue_press(state, mask);
	}
}

static void kbd_release_key(struct kbd_state *state, uint8_t key) {
	if (!kbd_key_valid(key)) {
		return;
	}

	uint8_t row = kbd_key_row(key);
	state->matrix[row] |= kbd_key_mask(key);
}

void kbd_keyup_state(struct kbd_state *state, uint8_t key) {
	if (!kbd_key_valid(key)) {
		return;
	}

	uint8_t row = kbd_key_row(key);
	uint8_t mask = kbd_key_mask(key);
	if (state->key_pending_down[key]) {
		state->key_pending_up[key] = true;
		return;
	}
	if (!(state->matrix[row] & mask)) {
		kbd_schedule_release_hold(state, key);
	}
}

static void kbd_process_pending_on_press(struct kbd_state *state, uint32_t cycles) {
	if (state->on_pending_down) {
		if (kbd_countdown_elapsed(&state->on_press_cycles, cycles)) {
			state->on_pressed = true;
			kbd_cpu_wake(state, WAKE_ON);
			state->on_pending_down = false;
		}
	}
}

static void kbd_process_pending_on_release(struct kbd_state *state) {
	if (state->on_pending_up) {
		state->on_pressed = false;
		state->on_pending_up = false;
	}
}

static void kbd_process_pending_key_press(
	struct kbd_state *state,
	uint8_t key,
	uint32_t cycles,
	bool scan_busy
) {
	if (!state->key_pending_down[key]) {
		return;
	}

	if (cycles < state->key_press_cycles[key]) {
		kbd_countdown_elapsed(&state->key_press_cycles[key], cycles);
		return;
	}

	/* Avoid changing the matrix while firmware is actively sampling it. */
	if (scan_busy) {
		return;
	}

	state->key_pending_down[key] = false;
	state->key_press_cycles[key] = 0;
	kbd_activate_key(state, key);
	if (state->key_pending_up[key]) {
		kbd_schedule_release_hold(state, key);
	}
}

static void kbd_process_pending_key_presses(struct kbd_state *state, uint32_t cycles, bool scan_busy) {
	int key;

	for (key = 0; key < KBD_KEY_COUNT; key++) {
		kbd_process_pending_key_press(state, (uint8_t)key, cycles, scan_busy);
	}
}

static void kbd_process_pending_key_release(struct kbd_state *state, uint8_t key, uint32_t cycles) {
	if (!state->key_release_cycles[key]) {
		return;
	}

	if (kbd_countdown_elapsed(&state->key_release_cycles[key], cycles)) {
		kbd_release_key(state, key);
	}
}

static void kbd_process_pending_key_releases(struct kbd_state *state, uint32_t cycles) {
	int key;

	for (key = 0; key < KBD_KEY_COUNT; key++) {
		kbd_process_pending_key_release(state, (uint8_t)key, cycles);
	}
}

void kbd_tick_state(struct kbd_state *state, uint32_t cycles) {
	bool scan_busy = kbd_scan_busy(state);

	kbd_process_pending_on_press(state, cycles);
	kbd_process_pending_on_release(state);
	kbd_process_pending_key_presses(state, cycles, scan_busy);
	kbd_process_pending_press(state);
	kbd_process_pending_key_releases(state, cycles);
	kbd_process_pending_press(state);
}

void kbd_ondown_state(struct kbd_state *state) {
	if (!state->on_pressed) {
		state->on_pending_down = true;
	}
	state->on_pending_up = false;
	state->on_press_cycles = KEY_PRESS_DELAY_CYCLES;
}

void kbd_onup_state(struct kbd_state *state) {
	if (state->on_pending_down) {
		state->on_pending_up = true;
	}
	else {
		state->on_pressed = false;
	}
}

static void kbd_clear_storage(struct kbd_state *state) {
	memset(state->reg, 0, sizeof(state->reg));
	memset(state->matrix, KBD_MATRIX_IDLE, sizeof(state->matrix));
	memset(state->key_pending_down, 0, sizeof(state->key_pending_down));
	memset(state->key_pending_up, 0, sizeof(state->key_pending_up));
	memset(state->key_press_cycles, 0, sizeof(state->key_press_cycles));
	memset(state->key_release_cycles, 0, sizeof(state->key_release_cycles));
}

static void kbd_apply_reset_defaults(struct kbd_state *state) {
	state->pending_press_mask = 0;
	state->reg[REG_DCRA] = KBD_DCR_RESET;
	state->reg[REG_DCRB] = KBD_DCR_RESET;
	state->reg[REG_DCRC] = KBD_DCR_RESET;
	state->porta_latch = 0;
	state->portb_latch = 0;
	state->portc_latch = 0;
	state->on_pressed = false;
	state->on_pending_down = false;
	state->on_pending_up = false;
	state->on_press_cycles = 0;
}

void kbd_reset_state(struct kbd_state *state) {
	kbd_clear_storage(state);
	kbd_apply_reset_defaults(state);
}
