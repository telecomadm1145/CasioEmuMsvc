/* Emulated machine ROM loading support */

#include "machine_internal.h"
#include "machine_rom.h"

bool machine_state_load_rom_image(struct machine_state *state, const uint8_t *data, size_t size) {
	if (!state) {
		return false;
	}

	return rom_load_image(&state->rom, data, size);
}


