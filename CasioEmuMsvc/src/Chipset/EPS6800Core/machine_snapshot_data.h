/* Emulated machine save-state data boundary. */
#ifndef FX_EMU_CORE_MACHINE_SNAPSHOT_DATA_H
#define FX_EMU_CORE_MACHINE_SNAPSHOT_DATA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct machine_snapshot;

const void *machine_snapshot_data(const struct machine_snapshot *snapshot, size_t *size);
struct machine_snapshot *machine_snapshot_from_data(const void *data, size_t size);
void machine_snapshot_free(struct machine_snapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* FX_EMU_CORE_MACHINE_SNAPSHOT_DATA_H */


