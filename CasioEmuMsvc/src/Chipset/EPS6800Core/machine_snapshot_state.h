/* Emulated machine save-state capture/restore boundary. */
#ifndef FX_EMU_CORE_MACHINE_SNAPSHOT_STATE_H
#define FX_EMU_CORE_MACHINE_SNAPSHOT_STATE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct machine_snapshot;
struct machine_state;

struct machine_snapshot *machine_state_save_snapshot(const struct machine_state *state, size_t *size);
void machine_state_load_snapshot(struct machine_state *state, const struct machine_snapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* FX_EMU_CORE_MACHINE_SNAPSHOT_STATE_H */


