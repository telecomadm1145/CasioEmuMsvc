/* Emulated machine host I/O wrappers */

#include "eps6800.h"
#include "machine_internal.h"
#include "machine_io.h"

enum machine_cpu_mode machine_state_cpu_mode(const struct machine_state *state) {
	if (!state)
		return MACHINE_CPU_MODE_SLEEP;

	switch (state->cpu.mode) {
	case CPU_MODE_SLOW:
		return MACHINE_CPU_MODE_SLOW;
	case CPU_MODE_FAST:
		return MACHINE_CPU_MODE_FAST;
	case CPU_MODE_IDLE:
		return MACHINE_CPU_MODE_IDLE;
	case CPU_MODE_SLEEP:
	default:
		return MACHINE_CPU_MODE_SLEEP;
	}
}

uint8_t machine_state_interrupt_pending(const struct machine_state *state) {
	return state ? state->cpu.int_pending : 0;
}

size_t machine_state_lcd_copy_display(
	const struct machine_state *state,
	uint8_t *data,
	size_t size,
	struct machine_lcd_control *control
) {
	size_t i;
	size_t copy_size;

	if (!state || !data) {
		return 0;
	}

	copy_size = (size < eps_lcd_raw_size(state->mmio.variant)) ? size : eps_lcd_raw_size(state->mmio.variant);
	for (i = 0; i < copy_size; i++) {
		data[i] = lcd_ram_read_byte_state(&state->lcd, (uint16_t)i);
	}
	if (control) {
		control->lcdarh = eps_variant_is_6009(state->mmio.variant) ? 0 : state->lcd.reg[eps_reg_lcdarh(state->mmio.variant)];
		control->lcdcon = state->lcd.reg[eps_reg_lcdcon(state->mmio.variant)];
	}
	return copy_size;
}

size_t machine_state_lcd_copy_framebuffer(const struct machine_state *state, uint8_t *data, size_t size) {
	return machine_state_lcd_copy_display(state, data, size, NULL);
}

uint8_t machine_state_lcd_read_memory(const struct machine_state *state, size_t address) {
	if (!state || address >= eps_lcd_raw_size(state->mmio.variant))
		return 0xff;
	return state->lcd.fb[address];
}

bool machine_state_lcd_write_memory(struct machine_state *state, size_t address, uint8_t value) {
	if (!state || address >= eps_lcd_raw_size(state->mmio.variant))
		return false;
	state->lcd.fb[address] = value;
	return true;
}

void machine_state_keydown(struct machine_state *state, uint8_t key) {
	if (!state) {
		return;
	}

	kbd_keydown_state(&state->kbd, key);
}

void machine_state_restore_keydown(struct machine_state *state, uint8_t key) {
	if (!state) {
		return;
	}

	kbd_restore_keydown_state(&state->kbd, key);
}

void machine_state_keyup(struct machine_state *state, uint8_t key) {
	if (!state) {
		return;
	}

	kbd_keyup_state(&state->kbd, key);
}

void machine_state_ondown(struct machine_state *state) {
	if (!state) {
		return;
	}

	kbd_ondown_state(&state->kbd);
}

void machine_state_onup(struct machine_state *state) {
	if (!state) {
		return;
	}

	kbd_onup_state(&state->kbd);
}

void machine_state_set_portb_input(struct machine_state *state, uint8_t mask, uint8_t value) {
	if (state) {
		kbd_set_portb_input_state(&state->kbd, mask, value);
	}
}

void machine_state_set_portc_input(struct machine_state *state, uint8_t mask, uint8_t value) {
	if (state) {
		kbd_set_portc_input_state(&state->kbd, mask, value);
	}
}
