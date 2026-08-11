/* Internal ROM state and API */
#ifndef FX_EMU_CORE_ROM_INTERNAL_H
#define FX_EMU_CORE_ROM_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
	ROM_MAX_WORDS = 96 * 1024,
	ROM_MAX_BYTES = ROM_MAX_WORDS * 2
};

struct rom_state {
	uint8_t data[ROM_MAX_BYTES];
};

void rom_init(struct rom_state *state);
uint16_t rom_read_word(const struct rom_state *state, uint32_t addr);
bool rom_write_word(struct rom_state *state, uint32_t addr, uint16_t word);
bool rom_load_image(struct rom_state *state, const uint8_t *data, size_t size);

#endif /* FX_EMU_CORE_ROM_INTERNAL_H */

