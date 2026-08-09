/* Emulated machine diagnostics support */

#include "machine_diag.h"
#include "machine_internal.h"

void machine_state_diag_set_writer(struct machine_state *state, machine_diag_write_fn write_fn, void *user) {
	if (!state) {
		return;
	}

	cpu_set_diag_writer_state(&state->cpu, write_fn, user);
}


