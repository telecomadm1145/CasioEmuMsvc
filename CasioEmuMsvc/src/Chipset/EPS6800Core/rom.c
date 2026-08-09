/* ePS6800 Mask ROM */
#include "rom_internal.h"

#include <string.h>

enum {
	ROM_BYTES_PER_WORD = 2,
	ROM_HIGH_BYTE_OFFSET = 0,
	ROM_LOW_BYTE_OFFSET = 1,
	ROM_NIBBLE_SHIFT = 4,
	ROM_NIBBLE_UNPACKED_BYTES = 256 * 1024
};

static size_t rom_word_offset(uint32_t addr) {
	return (size_t)addr * ROM_BYTES_PER_WORD;
}

static uint16_t rom_pack_word(uint8_t high, uint8_t low) {
	return (uint16_t)((high << 8) | low);
}

static uint8_t rom_pack_nibbles(uint8_t high, uint8_t low) {
	return (uint8_t)((high << ROM_NIBBLE_SHIFT) | low);
}

static void rom_clear(struct rom_state *state) {
	memset(state->data, 0, sizeof(state->data));
}

static void rom_load_unpacked_nibbles(struct rom_state *state, const uint8_t *data, size_t size) {
	size_t i;

	rom_clear(state);
	for (i = 0; i < size / ROM_BYTES_PER_WORD; i++) {
		size_t offset = i * ROM_BYTES_PER_WORD;
		state->data[i] = rom_pack_nibbles(data[offset + ROM_HIGH_BYTE_OFFSET], data[offset + ROM_LOW_BYTE_OFFSET]);
	}
}

static bool rom_image_is_unpacked_nibbles(size_t size) {
	return size == ROM_NIBBLE_UNPACKED_BYTES;
}

static bool rom_image_fits_packed_storage(const struct rom_state *state, size_t size) {
	return size <= sizeof(state->data);
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

bool rom_load_image(struct rom_state *state, const uint8_t *data, size_t size) {
	if (!state || !data) {
		return false;
	}

	if (rom_image_is_unpacked_nibbles(size)) {
		rom_load_unpacked_nibbles(state, data, size);
		return true;
	}

	if (!rom_image_fits_packed_storage(state, size)) {
		return false;
	}

	rom_clear(state);
	memcpy(state->data, data, size);
	return true;
}


