/* Emulated machine boundary */
#ifndef FX_EMU_CORE_MACHINE_H
#define FX_EMU_CORE_MACHINE_H

#include "eps6800.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct machine_state;

/* Lifecycle */
struct machine_state *machine_state_create(void);
struct machine_state *machine_state_create_variant(enum eps_variant variant);
void machine_state_destroy(struct machine_state *state);
enum eps_variant machine_state_variant(const struct machine_state *state);
void machine_state_reset(struct machine_state *state);
void machine_state_clear_ram_and_reset(struct machine_state *state);

/* Host-persisted RAM image.  Import requires exactly the size returned by
 * machine_state_ram_image_size(state). */
size_t machine_state_ram_image_size(const struct machine_state *state);
bool machine_state_export_ram(
	const struct machine_state *state,
	uint8_t *data,
	size_t size
);
bool machine_state_import_ram(
	struct machine_state *state,
	const uint8_t *data,
	size_t size
);

#ifdef __cplusplus
}
#endif

#endif /* FX_EMU_CORE_MACHINE_H */


