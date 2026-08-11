/* Emulated machine ROM loading boundary. */
#ifndef FX_EMU_CORE_MACHINE_ROM_H
#define FX_EMU_CORE_MACHINE_ROM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct machine_state;

bool machine_state_load_rom_image(struct machine_state *state, const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* FX_EMU_CORE_MACHINE_ROM_H */


