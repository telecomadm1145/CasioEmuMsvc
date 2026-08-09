/* Emulated machine trace snapshot support */

#include "machine_internal.h"
#include "machine_trace_snapshot.h"

#include <stdlib.h>
#include <string.h>

struct machine_trace_snapshot {
	uint8_t regs[MMIO_REG_COUNT];
	uint8_t ram_wbk[MMIO_WBK_COUNT];
	uint8_t ram[MMIO_RAM_COUNT];
};

struct machine_trace_snapshot *machine_trace_snapshot_create(void) {
	return (struct machine_trace_snapshot *)calloc(1, sizeof(struct machine_trace_snapshot));
}

void machine_trace_snapshot_save(
	const struct machine_state *state,
	struct machine_trace_snapshot *snapshot
) {
	if (!state || !snapshot) {
		return;
	}

	mmio_trace_snapshot_state(&state->mmio, snapshot->regs, snapshot->ram_wbk, snapshot->ram);
}

bool machine_trace_snapshot_equal(
	const struct machine_trace_snapshot *a,
	const struct machine_trace_snapshot *b
) {
	if (!a || !b) {
		return false;
	}

	return memcmp(a, b, sizeof(*a)) == 0;
}

void machine_trace_snapshot_free(struct machine_trace_snapshot *snapshot) {
	if (!snapshot) {
		return;
	}

	free(snapshot);
}


