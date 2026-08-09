/* Emulated machine boundary */

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
}


