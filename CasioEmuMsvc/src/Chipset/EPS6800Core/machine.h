/* Emulated machine boundary */
#ifndef FX_EMU_CORE_MACHINE_H
#define FX_EMU_CORE_MACHINE_H

#include "eps6800.h"

#ifdef __cplusplus
extern "C" {
#endif

struct machine_state;

/* Lifecycle */
struct machine_state *machine_state_create(void);
void machine_state_destroy(struct machine_state *state);
void machine_state_set_variant(struct machine_state *state, enum eps_variant variant);
void machine_state_reset(struct machine_state *state);

#ifdef __cplusplus
}
#endif

#endif /* FX_EMU_CORE_MACHINE_H */


