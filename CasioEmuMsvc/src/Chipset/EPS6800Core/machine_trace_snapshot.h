/* Emulated machine trace snapshot boundary. */
#ifndef FX_EMU_CORE_MACHINE_TRACE_SNAPSHOT_H
#define FX_EMU_CORE_MACHINE_TRACE_SNAPSHOT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct machine_state;
struct machine_trace_snapshot;

struct machine_trace_snapshot *machine_trace_snapshot_create(void);
void machine_trace_snapshot_save(const struct machine_state *state, struct machine_trace_snapshot *snapshot);
bool machine_trace_snapshot_equal(
	const struct machine_trace_snapshot *a,
	const struct machine_trace_snapshot *b
);
void machine_trace_snapshot_free(struct machine_trace_snapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* FX_EMU_CORE_MACHINE_TRACE_SNAPSHOT_H */


