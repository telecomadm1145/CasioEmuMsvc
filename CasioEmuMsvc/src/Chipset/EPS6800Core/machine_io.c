/* Emulated machine host I/O wrappers */

#include "eps6800.h"
#include "machine_internal.h"
#include "machine_io.h"

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

	copy_size = (size < MACHINE_LCD_FRAMEBUFFER_SIZE) ? size : MACHINE_LCD_FRAMEBUFFER_SIZE;
	for (i = 0; i < copy_size; i++) {
		data[i] = lcd_ram_read_byte_state(&state->lcd, (uint16_t)i);
	}
	if (control) {
		control->lcdarh = state->lcd.reg[REG_LCDARH];
		control->lcdcon = state->lcd.reg[REG_LCDCON];
	}
	return copy_size;
}

size_t machine_state_lcd_copy_framebuffer(const struct machine_state *state, uint8_t *data, size_t size) {
	return machine_state_lcd_copy_display(state, data, size, NULL);
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

void machine_state_set_portc_input(struct machine_state *state, uint8_t mask, uint8_t value) {
	if (state) {
		kbd_set_portc_input_state(&state->kbd, mask, value);
	}
}
