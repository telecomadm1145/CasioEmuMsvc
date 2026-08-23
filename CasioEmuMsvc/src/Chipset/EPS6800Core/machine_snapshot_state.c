/* Emulated machine save-state capture/restore support */

#include "machine_snapshot_internal.h"
#include "machine_snapshot_state.h"

#include <string.h>

static uint8_t machine_snapshot_encode_bool(bool value) {
	return value ? 1u : 0u;
}

static bool machine_snapshot_decode_bool(uint8_t value) {
	return value != 0u;
}

static void machine_snapshot_encode_bool_array(uint8_t *snapshot, const bool *state, size_t count) {
	size_t i;

	for (i = 0; i < count; i++) {
		snapshot[i] = machine_snapshot_encode_bool(state[i]);
	}
}

static void machine_snapshot_decode_bool_array(bool *state, const uint8_t *snapshot, size_t count) {
	size_t i;

	for (i = 0; i < count; i++) {
		state[i] = machine_snapshot_decode_bool(snapshot[i]);
	}
}

static void machine_snapshot_save_cpu(struct machine_cpu_snapshot *snapshot, const struct cpu_state *state) {
	snapshot->pc = state->pc;
	memcpy(snapshot->stack, state->stack, sizeof(snapshot->stack));
	snapshot->status = state->status;
	snapshot->rpt_counter = state->rpt_counter;
	snapshot->rpt_target_pc = state->rpt_target_pc;
	snapshot->mode = (uint8_t)state->mode;
	snapshot->int_pending = state->int_pending;
	snapshot->sleep_repeat_pc = machine_snapshot_encode_bool(state->sleep_repeat_pc);
}

static void machine_snapshot_load_cpu(struct cpu_state *state, const struct machine_cpu_snapshot *snapshot) {
	state->pc = snapshot->pc;
	memcpy(state->stack, snapshot->stack, sizeof(state->stack));
	state->status = snapshot->status;
	state->rpt_counter = snapshot->rpt_counter;
	state->rpt_target_pc = snapshot->rpt_target_pc;
	state->mode = (enum cpu_mode)snapshot->mode;
	state->int_pending = snapshot->int_pending;
	state->sleep_repeat_pc = machine_snapshot_decode_bool(snapshot->sleep_repeat_pc);
	state->rom_read_fn = NULL;
	state->rom_read_user = NULL;
	state->mmio = NULL;
}

static void machine_snapshot_save_mmio(struct machine_mmio_snapshot *snapshot, const struct mmio_state *state) {
	memcpy(snapshot->regs, state->regs, sizeof(snapshot->regs));
	memcpy(snapshot->ram_wbk, state->ram_wbk, sizeof(snapshot->ram_wbk));
	memcpy(snapshot->ram, state->ram, sizeof(snapshot->ram));
}

static void machine_snapshot_load_mmio(struct mmio_state *state, const struct machine_mmio_snapshot *snapshot) {
	memcpy(state->regs, snapshot->regs, sizeof(state->regs));
	memcpy(state->ram_wbk, snapshot->ram_wbk, sizeof(state->ram_wbk));
	memcpy(state->ram, snapshot->ram, sizeof(state->ram));
	state->cpu = NULL;
	state->kbd = NULL;
	state->lcd = NULL;
	state->timer = NULL;
}

static void machine_snapshot_save_lcd(struct machine_lcd_snapshot *snapshot, const struct lcd_state *state) {
	memcpy(snapshot->fb, state->fb, sizeof(snapshot->fb));
	memcpy(snapshot->reg, state->reg, sizeof(snapshot->reg));
}

static void machine_snapshot_load_lcd(struct lcd_state *state, const struct machine_lcd_snapshot *snapshot) {
	memcpy(state->fb, snapshot->fb, sizeof(state->fb));
	memcpy(state->reg, snapshot->reg, sizeof(state->reg));
	state->mmio = NULL;
}

static void machine_snapshot_save_timer(struct machine_timer_snapshot *snapshot, const struct timer_state *state) {
	memcpy(snapshot->reg, state->reg, sizeof(snapshot->reg));
	snapshot->t0psc = state->t0psc;
	snapshot->t1psc = state->t1psc;
	snapshot->t2psc = state->t2psc;
	snapshot->t0prl = state->t0prl;
	snapshot->t1prl = state->t1prl;
	snapshot->t2prl = state->t2prl;
	snapshot->t0cnt = state->t0cnt;
	snapshot->t1cnt = state->t1cnt;
	snapshot->t2cnt = state->t2cnt;
	snapshot->t0crl = state->t0crl;
	snapshot->t1crl = state->t1crl;
	snapshot->t2crl = state->t2crl;
}

static void machine_snapshot_load_timer(struct timer_state *state, const struct machine_timer_snapshot *snapshot) {
	memcpy(state->reg, snapshot->reg, sizeof(state->reg));
	state->t0psc = snapshot->t0psc;
	state->t1psc = snapshot->t1psc;
	state->t2psc = snapshot->t2psc;
	state->t0prl = snapshot->t0prl;
	state->t1prl = snapshot->t1prl;
	state->t2prl = snapshot->t2prl;
	state->t0cnt = snapshot->t0cnt;
	state->t1cnt = snapshot->t1cnt;
	state->t2cnt = snapshot->t2cnt;
	state->t0crl = snapshot->t0crl;
	state->t1crl = snapshot->t1crl;
	state->t2crl = snapshot->t2crl;
	state->cpu = NULL;
	state->mmio = NULL;
}

static void machine_snapshot_save_kbd(struct machine_kbd_snapshot *snapshot, const struct kbd_state *state) {
	memcpy(snapshot->reg, state->reg, sizeof(snapshot->reg));
	memcpy(snapshot->matrix, state->matrix, sizeof(snapshot->matrix));
	machine_snapshot_encode_bool_array(snapshot->key_pending_down, state->key_pending_down, KBD_KEY_COUNT);
	machine_snapshot_encode_bool_array(snapshot->key_pending_up, state->key_pending_up, KBD_KEY_COUNT);
	memcpy(snapshot->key_press_cycles, state->key_press_cycles, sizeof(snapshot->key_press_cycles));
	memcpy(snapshot->key_release_cycles, state->key_release_cycles, sizeof(snapshot->key_release_cycles));
	snapshot->pending_press_mask = state->pending_press_mask;
	snapshot->porta_latch = state->porta_latch;
	snapshot->portb_latch = state->portb_latch;
	snapshot->portc_latch = state->portc_latch;
	snapshot->on_pressed = machine_snapshot_encode_bool(state->on_pressed);
	snapshot->on_pending_down = machine_snapshot_encode_bool(state->on_pending_down);
	snapshot->on_pending_up = machine_snapshot_encode_bool(state->on_pending_up);
	snapshot->on_press_cycles = state->on_press_cycles;
}

static void machine_snapshot_load_kbd(struct kbd_state *state, const struct machine_kbd_snapshot *snapshot) {
	memcpy(state->reg, snapshot->reg, sizeof(state->reg));
	memcpy(state->matrix, snapshot->matrix, sizeof(state->matrix));
	machine_snapshot_decode_bool_array(state->key_pending_down, snapshot->key_pending_down, KBD_KEY_COUNT);
	machine_snapshot_decode_bool_array(state->key_pending_up, snapshot->key_pending_up, KBD_KEY_COUNT);
	memcpy(state->key_press_cycles, snapshot->key_press_cycles, sizeof(state->key_press_cycles));
	memcpy(state->key_release_cycles, snapshot->key_release_cycles, sizeof(state->key_release_cycles));
	state->pending_press_mask = snapshot->pending_press_mask;
	state->porta_latch = snapshot->porta_latch;
	state->portb_latch = snapshot->portb_latch;
	state->portc_latch = snapshot->portc_latch;
	state->on_pressed = machine_snapshot_decode_bool(snapshot->on_pressed);
	state->on_pending_down = machine_snapshot_decode_bool(snapshot->on_pending_down);
	state->on_pending_up = machine_snapshot_decode_bool(snapshot->on_pending_up);
	state->on_press_cycles = snapshot->on_press_cycles;
	state->cpu = NULL;
	state->mmio = NULL;
}

struct machine_snapshot *machine_state_save_snapshot(const struct machine_state *state, size_t *size) {
	struct machine_snapshot *snapshot;

	machine_snapshot_set_size_internal(size, 0);
	if (!state) {
		return NULL;
	}

	snapshot = machine_snapshot_alloc_internal();
	if (!snapshot) {
		return NULL;
	}

	snapshot->magic = MACHINE_SNAPSHOT_MAGIC;
	snapshot->version = MACHINE_SNAPSHOT_VERSION;
	machine_snapshot_save_cpu(&snapshot->cpu, &state->cpu);
	machine_snapshot_save_mmio(&snapshot->mmio, &state->mmio);
	machine_snapshot_save_lcd(&snapshot->lcd, &state->lcd);
	machine_snapshot_save_timer(&snapshot->timer, &state->timer);
	machine_snapshot_save_kbd(&snapshot->kbd, &state->kbd);

	machine_snapshot_set_size_internal(size, machine_snapshot_payload_size_internal());
	return snapshot;
}

void machine_state_load_snapshot(
	struct machine_state *state,
	const struct machine_snapshot *snapshot
) {
	if (!state || !machine_snapshot_valid_internal(snapshot)) {
		return;
	}

	machine_snapshot_load_cpu(&state->cpu, &snapshot->cpu);
	machine_snapshot_load_mmio(&state->mmio, &snapshot->mmio);
	machine_snapshot_load_lcd(&state->lcd, &snapshot->lcd);
	machine_snapshot_load_timer(&state->timer, &snapshot->timer);
	machine_snapshot_load_kbd(&state->kbd, &snapshot->kbd);
	machine_state_bind_modules(state);
}


