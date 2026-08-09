/* ePS6800 Mask ROM */
#include "rom_internal.h"

#include <string.h>

enum {
	ROM_BYTES_PER_WORD = 2,
	ROM_HIGH_BYTE_OFFSET = 0,
	ROM_LOW_BYTE_OFFSET = 1
};

static size_t rom_word_offset(uint32_t addr) {
	return (size_t)addr * ROM_BYTES_PER_WORD;
}

static uint16_t rom_pack_word(uint8_t high, uint8_t low) {
	return (uint16_t)((high << 8) | low);
}

static void rom_clear(struct rom_state *state) {
	memset(state->data, 0, sizeof(state->data));
}

static bool rom_image_fits_packed_storage(const struct rom_state *state, size_t size) {
	return size > 0 && size % ROM_BYTES_PER_WORD == 0 && size <= sizeof(state->data);
}

void rom_init(struct rom_state *state) {
	if (!state) {
		return;
	}

	rom_clear(state);
}

uint16_t rom_read_word(const struct rom_state *state, uint32_t addr) {
	size_t offset;

	if (!state) {
		return 0;
	}

	if (addr < ROM_MAX_WORDS) {
		offset = rom_word_offset(addr);
		return rom_pack_word(state->data[offset + ROM_HIGH_BYTE_OFFSET], state->data[offset + ROM_LOW_BYTE_OFFSET]);
	}

	return 0;
}

bool rom_write_word(struct rom_state *state, uint32_t addr, uint16_t word) {
	size_t offset;

	if (!state || addr >= ROM_MAX_WORDS) {
		return false;
	}

	offset = rom_word_offset(addr);
	state->data[offset + ROM_HIGH_BYTE_OFFSET] = (uint8_t)(word >> 8);
	state->data[offset + ROM_LOW_BYTE_OFFSET] = (uint8_t)word;
	return true;
}

bool rom_load_image(struct rom_state *state, const uint8_t *data, size_t size) {
	if (!state || !data) {
		return false;
	}

	if (!rom_image_fits_packed_storage(state, size)) {
		return false;
	}

	rom_clear(state);
	memcpy(state->data, data, size);
	return true;
}
