/* Emulated machine debugger support */

#include "eps6800.h"
#include "machine_debug.h"
#include "machine_internal.h"

#include <string.h>

enum {
	MACHINE_DEBUG_PC_MID_SHIFT = 8,
	MACHINE_DEBUG_PC_HIGH_SHIFT = 16,
	MACHINE_DEBUG_POSTID_DISABLED = 0x00
};

static uint32_t machine_debug_read_pc(struct machine_state *state) {
	return ((uint32_t)mmio_read_byte_internal_state(&state->mmio, REG_PCH) << MACHINE_DEBUG_PC_HIGH_SHIFT) |
		((uint32_t)mmio_read_byte_internal_state(&state->mmio, REG_PCM) << MACHINE_DEBUG_PC_MID_SHIFT) |
		(uint32_t)mmio_read_byte_internal_state(&state->mmio, REG_PCL);
}

static uint32_t machine_debug_fetch_instruction_word(const struct machine_state *state, uint32_t pc) {
	return ((uint32_t)rom_read_word(&state->rom, pc) << MACHINE_DEBUG_PC_HIGH_SHIFT) |
		rom_read_word(&state->rom, pc + 1);
}

void machine_state_debug_get_state(
	struct machine_state *machine_state,
	struct machine_debug_state *debug_state
) {
	if (!machine_state || !debug_state) {
		return;
	}

	debug_state->pc = machine_debug_read_pc(machine_state);
	debug_state->acc = mmio_read_byte_internal_state(&machine_state->mmio, REG_ACC);
	debug_state->status = mmio_read_byte_internal_state(&machine_state->mmio, REG_STATUS);
}

uint32_t machine_state_debug_fetch_instruction(const struct machine_state *state, uint32_t pc) {
	if (!state) {
		return 0;
	}

	return machine_debug_fetch_instruction_word(state, pc);
}

void machine_state_debug_get_register_overview(
	struct machine_state *state,
	struct machine_debug_register_overview *overview
) {
	size_t i;
	uint8_t postid;

	if (!overview) {
		return;
	}
	memset(overview, 0, sizeof(*overview));
	if (!state) {
		return;
	}

	/* Reading INDF and LCDDATA can have side effects through POSTID. */
	postid = mmio_read_byte_state(&state->mmio, REG_POSTID);
	mmio_write_byte_state(&state->mmio, REG_POSTID, MACHINE_DEBUG_POSTID_DISABLED);
	for (i = 0; i < MACHINE_DEBUG_LOW_REGISTER_COUNT; i++) {
		overview->low_regs[i] = mmio_read_byte_state(&state->mmio, (uint8_t)i);
	}
	overview->cpucon = mmio_read_byte_state(&state->mmio, REG_CPUCON);
	mmio_write_byte_state(&state->mmio, REG_POSTID, postid);
}

uint8_t machine_state_debug_read_byte(struct machine_state *state, uint8_t addr) {
	if (!state) {
		return 0;
	}

	return mmio_read_byte_state(&state->mmio, addr);
}

void machine_state_debug_write_byte(struct machine_state *state, uint8_t addr, uint8_t byte) {
	if (!state) {
		return;
	}

	mmio_write_byte_state(&state->mmio, addr, byte);
}

void machine_state_debug_step(struct machine_state *state) {
	if (!state) {
		return;
	}

	cpu_loop_state(&state->cpu, 1);
}


