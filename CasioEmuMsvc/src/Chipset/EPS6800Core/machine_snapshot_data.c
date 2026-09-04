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

static bool machine_snapshot_legacy_header_valid(uint32_t magic, uint32_t version) {
	return magic == MACHINE_SNAPSHOT_MAGIC && version == MACHINE_SNAPSHOT_LEGACY_VERSION;
}

static void machine_snapshot_copy_legacy_lcd(
	struct machine_lcd_snapshot *dst,
	const struct machine_lcd_snapshot_v4 *src
) {
	memcpy(dst->fb, src->fb, sizeof(src->fb));
	memcpy(dst->reg, src->reg, sizeof(src->reg));
}

static void machine_snapshot_copy_v5_lcd(
	struct machine_lcd_snapshot *dst,
	const struct machine_lcd_snapshot_v5 *src
) {
	memcpy(dst->fb, src->fb, sizeof(src->fb));
	memcpy(dst->reg, src->reg, sizeof(src->reg));
	memcpy(dst->w192_fb, src->w192_fb, sizeof(src->w192_fb));
	dst->w192_page = src->w192_page;
	dst->w192_column = src->w192_column;
	dst->w192_rmw_column = src->w192_rmw_column;
	dst->w192_portd = src->w192_portd;
	dst->w192_porte = src->w192_porte;
	dst->w192_portd_latch = src->w192_portd;
	dst->w192_porte_latch = src->w192_porte;
	dst->w192_dcrde = src->w192_dcrde;
	dst->w192_display_on = src->w192_display_on;
	dst->w192_all_pixels_on = src->w192_all_pixels_on;
	dst->w192_rmw_active = src->w192_rmw_active;
	dst->w192_segment_reverse = src->w192_segment_reverse;
	dst->w192_com_reverse = src->w192_com_reverse;
	dst->w192_contrast = src->w192_contrast;
	dst->w192_contrast_pending = src->w192_contrast_pending;
}

static void machine_snapshot_copy_legacy_mmio(
	struct machine_mmio_snapshot *dst,
	const struct machine_mmio_snapshot_v3_legacy *src
) {
	memcpy(dst->regs, src->regs, sizeof(src->regs));
	memcpy(dst->ram_wbk, src->ram_wbk, sizeof(src->ram_wbk));
	memcpy(dst->ram, src->ram, sizeof(src->ram));
}

static struct machine_snapshot *machine_snapshot_migrate_v3_legacy(
	const struct machine_snapshot_v3_legacy *legacy
) {
	struct machine_snapshot *snapshot = machine_snapshot_alloc_internal();
	if (!snapshot)
		return NULL;

	snapshot->magic = MACHINE_SNAPSHOT_MAGIC;
	snapshot->version = MACHINE_SNAPSHOT_VERSION;
	snapshot->cpu.pc = legacy->cpu.pc;
	memcpy(snapshot->cpu.stack, legacy->cpu.stack, sizeof(legacy->cpu.stack));
	snapshot->cpu.status = legacy->cpu.status;
	snapshot->cpu.rpt_counter = legacy->cpu.rpt_counter;
	snapshot->cpu.rpt_target_pc = legacy->cpu.rpt_target_pc;
	snapshot->cpu.mode = legacy->cpu.mode;
	snapshot->cpu.int_pending = legacy->cpu.int_pending;
	snapshot->cpu.sleep_repeat_pc = legacy->cpu.sleep_repeat_pc;
	machine_snapshot_copy_legacy_mmio(&snapshot->mmio, &legacy->mmio);
	machine_snapshot_copy_legacy_lcd(&snapshot->lcd, &legacy->lcd);
	snapshot->timer = legacy->timer;
	snapshot->kbd = legacy->kbd;
	return snapshot;
}

static struct machine_snapshot *machine_snapshot_migrate_v3_expanded_stack(
	const struct machine_snapshot_v3_expanded_stack *legacy
) {
	struct machine_snapshot *snapshot = machine_snapshot_alloc_internal();
	if (!snapshot)
		return NULL;

	snapshot->magic = MACHINE_SNAPSHOT_MAGIC;
	snapshot->version = MACHINE_SNAPSHOT_VERSION;
	snapshot->cpu = legacy->cpu;
	machine_snapshot_copy_legacy_mmio(&snapshot->mmio, &legacy->mmio);
	machine_snapshot_copy_legacy_lcd(&snapshot->lcd, &legacy->lcd);
	snapshot->timer = legacy->timer;
	snapshot->kbd = legacy->kbd;
	return snapshot;
}

struct machine_snapshot *machine_snapshot_from_data(const void *data, size_t size) {
	struct machine_snapshot *snapshot;

	if (!data)
		return NULL;

	if (size == machine_snapshot_payload_size_internal()) {
		snapshot = machine_snapshot_alloc_internal();
		if (!snapshot)
			return NULL;
		memcpy(snapshot, data, size);
		if (machine_snapshot_valid_internal(snapshot))
			return snapshot;
		/* Builds after the ePS9500 storage expansion still emitted version 3.
		 * Accept that transitional layout and normalize it to the current ABI. */
		if (machine_snapshot_legacy_header_valid(snapshot->magic, snapshot->version)) {
			snapshot->version = MACHINE_SNAPSHOT_VERSION;
			return snapshot;
		}
		free(snapshot);
		return NULL;
	}

	if (size == sizeof(struct machine_snapshot_v4)) {
		struct machine_snapshot_v4 legacy;
		memcpy(&legacy, data, sizeof(legacy));
		if (legacy.magic != MACHINE_SNAPSHOT_MAGIC ||
			(legacy.version != MACHINE_SNAPSHOT_V4_VERSION &&
			 legacy.version != MACHINE_SNAPSHOT_LEGACY_VERSION))
			return NULL;
		snapshot = machine_snapshot_alloc_internal();
		if (!snapshot)
			return NULL;
		snapshot->magic = MACHINE_SNAPSHOT_MAGIC;
		snapshot->version = MACHINE_SNAPSHOT_VERSION;
		snapshot->cpu = legacy.cpu;
		snapshot->mmio = legacy.mmio;
		machine_snapshot_copy_legacy_lcd(&snapshot->lcd, &legacy.lcd);
		snapshot->timer = legacy.timer;
		snapshot->kbd = legacy.kbd;
		return snapshot;
	}

	if (size == sizeof(struct machine_snapshot_v5)) {
		struct machine_snapshot_v5 legacy;
		memcpy(&legacy, data, sizeof(legacy));
		if (legacy.magic != MACHINE_SNAPSHOT_MAGIC || legacy.version != MACHINE_SNAPSHOT_V5_VERSION)
			return NULL;
		snapshot = machine_snapshot_alloc_internal();
		if (!snapshot)
			return NULL;
		snapshot->magic = MACHINE_SNAPSHOT_MAGIC;
		snapshot->version = MACHINE_SNAPSHOT_VERSION;
		snapshot->cpu = legacy.cpu;
		snapshot->mmio = legacy.mmio;
		machine_snapshot_copy_v5_lcd(&snapshot->lcd, &legacy.lcd);
		snapshot->timer = legacy.timer;
		snapshot->kbd = legacy.kbd;
		return snapshot;
	}

	if (size == sizeof(struct machine_snapshot_v3_expanded_stack)) {
		struct machine_snapshot_v3_expanded_stack legacy;
		memcpy(&legacy, data, sizeof(legacy));
		if (!machine_snapshot_legacy_header_valid(legacy.magic, legacy.version))
			return NULL;
		return machine_snapshot_migrate_v3_expanded_stack(&legacy);
	}

	if (size == sizeof(struct machine_snapshot_v3_legacy)) {
		struct machine_snapshot_v3_legacy legacy;
		memcpy(&legacy, data, sizeof(legacy));
		if (!machine_snapshot_legacy_header_valid(legacy.magic, legacy.version))
			return NULL;
		return machine_snapshot_migrate_v3_legacy(&legacy);
	}

	return NULL;
}

void machine_snapshot_free(struct machine_snapshot *snapshot) {
	if (!snapshot) {
		return;
	}

	free(snapshot);
}


