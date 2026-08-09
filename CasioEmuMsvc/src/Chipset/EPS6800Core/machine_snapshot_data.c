/* Emulated machine save-state data support */

#include "machine_snapshot_data.h"
#include "machine_snapshot_internal.h"

#include <stdlib.h>
#include <string.h>

bool machine_snapshot_valid_internal(const struct machine_snapshot *snapshot) {
	return snapshot &&
		(snapshot->magic == MACHINE_SNAPSHOT_MAGIC) &&
		(snapshot->version == MACHINE_SNAPSHOT_VERSION);
}

size_t machine_snapshot_payload_size_internal(void) {
	return sizeof(struct machine_snapshot);
}

struct machine_snapshot *machine_snapshot_alloc_internal(void) {
	return (struct machine_snapshot *)calloc(1, machine_snapshot_payload_size_internal());
}

void machine_snapshot_set_size_internal(size_t *size, size_t value) {
	if (size) {
		*size = value;
	}
}

const void *machine_snapshot_data(const struct machine_snapshot *snapshot, size_t *size) {
	machine_snapshot_set_size_internal(size, 0);
	if (!machine_snapshot_valid_internal(snapshot)) {
		return NULL;
	}

	machine_snapshot_set_size_internal(size, machine_snapshot_payload_size_internal());
	return snapshot;
}

struct machine_snapshot *machine_snapshot_from_data(const void *data, size_t size) {
	struct machine_snapshot *snapshot;

	if (!data || (size != machine_snapshot_payload_size_internal())) {
		return NULL;
	}

	snapshot = machine_snapshot_alloc_internal();
	if (!snapshot) {
		return NULL;
	}
	memcpy(snapshot, data, machine_snapshot_payload_size_internal());
	if (!machine_snapshot_valid_internal(snapshot)) {
		free(snapshot);
		return NULL;
	}

	return snapshot;
}

void machine_snapshot_free(struct machine_snapshot *snapshot) {
	if (!snapshot) {
		return;
	}

	free(snapshot);
}


