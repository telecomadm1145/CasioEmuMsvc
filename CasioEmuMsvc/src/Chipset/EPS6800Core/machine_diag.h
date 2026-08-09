/* Emulated machine diagnostics boundary. */
#ifndef FX_EMU_CORE_MACHINE_DIAG_H
#define FX_EMU_CORE_MACHINE_DIAG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*machine_diag_write_fn)(const char *data, size_t size, void *user);

struct machine_state;

void machine_state_diag_set_writer(struct machine_state *state, machine_diag_write_fn write_fn, void *user);

#ifdef __cplusplus
}
#endif

#endif /* FX_EMU_CORE_MACHINE_DIAG_H */


