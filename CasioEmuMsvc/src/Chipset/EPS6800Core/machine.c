/* Emulated machine boundary */

#include "eps6800.h"
#include "machine.h"
#include "machine_internal.h"

#include <stdlib.h>

static void machine_state_init(struct machine_state *state);

struct machine_state *machine_state_create(void) {
	struct machine_state *state = (struct machine_state *)calloc(1, sizeof(*state));
	if (!state) {
		return NULL;
	}

	machine_state_init(state);
	return state;
}

void machine_state_destroy(struct machine_state *state) {
	if (!state) {
		return;
	}

	free(state);
}

static void machine_state_init(struct machine_state *state) {
	rom_init(&state->rom);
	mmio_init_state(&state->mmio);
	machine_state_bind_modules(state);
}

void machine_state_set_variant(struct machine_state *state, enum eps_variant variant) {
	if (!state) {
		return;
	}

	state->mmio.variant = variant;
}

void machine_state_reset(struct machine_state *state) {
	if (!state) {
		return;
	}

	mmio_reset_state(&state->mmio);
	cpu_reset_state(&state->cpu);
	lcd_reset_state(&state->lcd);
	timer_reset_state(&state->timer);
	kbd_reset_state(&state->kbd);

	/* Reference ice.dll CIce::Reset: variant-specific SFRs must be written
	 * through their owning peripherals so the flat debugger mirror and device
	 * state stay synchronized. */
	{
		uint16_t config_word = rom_read_word(&state->rom, 12);
		if (eps_variant_is_6009(state->mmio.variant)) {
			uint8_t cpucon = (uint8_t)((config_word >> 1) & 1u);
			mmio_write_byte_internal_state(&state->mmio, eps_reg_cpucon(state->mmio.variant), cpucon);
			lcd_write_byte_state(&state->lcd, eps_reg_postid(state->mmio.variant),
				BIT_FSR0ID | BIT_FSR1ID | BIT_LCDID);
			kbd_write_byte_state(&state->kbd, eps_reg_pacon(state->mmio.variant), 0x0eu);
			kbd_write_byte_state(&state->kbd, eps_reg_pbcon(state->mmio.variant), 0x00u);
			kbd_write_byte_state(&state->kbd, eps_reg_dcrb(state->mmio.variant), 0x03u);
			kbd_write_byte_state(&state->kbd, eps_reg_porta(state->mmio.variant), 0xffu);
			kbd_write_byte_state(&state->kbd, eps_reg_portb(state->mmio.variant), 0xffu);
		}
		else if (eps_variant_is_9500(state->mmio.variant)) {
			/* Official EL-W531TL CIce::Reset core-mode-4 branch
			 * (sub_422F80).  sub_425C20 only performs the preceding low-level
			 * SFR clear; these are the final values visible to firmware. */
			const uint8_t cpucon = 0x10u | (uint8_t)((config_word >> 9) & 1u);
			mmio_write_byte_internal_state(&state->mmio,
				eps_reg_cpucon(state->mmio.variant), cpucon);
			kbd_write_byte_state(&state->kbd, REG_STBCON, 0x20u);
			kbd_write_byte_state(&state->kbd, REG_PORTA, 0xffu);
			kbd_write_byte_state(&state->kbd, REG_PACON, 0x00u);
			kbd_write_byte_state(&state->kbd, REG_DCRA, 0xffu);
			kbd_write_byte_state(&state->kbd, REG_PORTB, 0xffu);
			kbd_write_byte_state(&state->kbd, REG_PBCON, 0x00u);
			kbd_write_byte_state(&state->kbd, REG_DCRB, 0xffu);
			kbd_write_byte_state(&state->kbd, REG_PORTC, 0xffu);
			kbd_write_byte_state(&state->kbd, REG_PCCON, 0x00u);
			kbd_write_byte_state(&state->kbd, REG_DCRC, 0xffu);
			kbd_write_byte_state(&state->kbd,
				eps_reg_pawake(state->mmio.variant), 0x00u);
			lcd_write_byte_state(&state->lcd, eps_reg_postid(state->mmio.variant),
				BIT_FSR0ID | BIT_FSR1ID | BIT_LCDID | BIT_FSR2ID);
		}
		else {
			uint8_t reset_value = 0x10u | (uint8_t)((config_word >> 9) & 1u);
			mmio_write_byte_internal_state(&state->mmio, eps_reg_cpucon(state->mmio.variant), reset_value);
			kbd_write_byte_state(&state->kbd, eps_reg_pawake(state->mmio.variant), reset_value);
			lcd_write_byte_state(&state->lcd, eps_reg_postid(state->mmio.variant),
				BIT_FSR0ID | BIT_FSR1ID | BIT_LCDID | BIT_FSR2ID);
		}
	}
}


