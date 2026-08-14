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

void machine_state_reset(struct machine_state *state) {
	if (!state) {
		return;
	}

	mmio_reset_state(&state->mmio);
	cpu_reset_state(&state->cpu);
	lcd_reset_state(&state->lcd);
	timer_reset_state(&state->timer);
	kbd_reset_state(&state->kbd);

	/* Reference ice.dll CIce::Reset (EPS6800): CPUCON and PAWAKE initialise
	 * from ROM word 12 bit 9 (model configuration), ORed with 0x10. POSTID
	 * reset 0xF0 must be written through the LCD peripheral, which owns the
	 * register (the flat mmio mirror alone is shadowed on reads). */
	{
		uint16_t config_word = rom_read_word(&state->rom, 12);
		uint8_t reset_value = 0x10u | (uint8_t)((config_word >> 9) & 1u);
		state->mmio.regs[REG_CPUCON] = reset_value;
		state->kbd.reg[REG_PAWAKE] = reset_value;
		lcd_write_byte_state(&state->lcd, REG_POSTID,
			BIT_FSR0ID | BIT_FSR1ID | BIT_LCDID | BIT_FSR2ID);
	}
}


