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

uint32_t machine_state_debug_linear_memory_size(const struct machine_state *state) {
	if (!state)
		return 0;
	return MACHINE_DEBUG_REGISTER_COUNT + (uint32_t)eps_bank_ram_size(state->mmio.variant);
}

uint8_t machine_state_debug_stack_depth(const struct machine_state *state) {
	if (!state)
		return 0;
	return eps_stack_depth_from_raw(state->mmio.variant, state->mmio.regs[REG_STKPTR]);
}

uint32_t machine_state_debug_program_counter(const struct machine_state *state) {
	return state ? state->cpu.pc : 0;
}

void machine_state_debug_set_program_counter(struct machine_state *state, uint32_t word_address) {
	if (!state)
		return;
	state->cpu.pc = word_address & 0x00ffffffu;
	state->mmio.regs[REG_PCL] = (uint8_t)state->cpu.pc;
	state->mmio.regs[REG_PCM] = (uint8_t)(state->cpu.pc >> 8);
	state->mmio.regs[REG_PCH] = (uint8_t)(state->cpu.pc >> 16);
}

uint8_t machine_state_debug_accumulator(const struct machine_state *state) {
	return state ? state->mmio.regs[REG_ACC] : 0;
}

uint8_t machine_state_debug_status(const struct machine_state *state) {
	return state ? state->cpu.status : 0;
}

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
	mmio_suppress_debug_access_state(&state->mmio, true);
	postid = mmio_read_byte_state(&state->mmio, REG_POSTID);
	mmio_write_byte_state(&state->mmio, REG_POSTID, MACHINE_DEBUG_POSTID_DISABLED);
	for (i = 0; i < MACHINE_DEBUG_LOW_REGISTER_COUNT; i++) {
		overview->low_regs[i] = mmio_read_byte_state(&state->mmio, (uint8_t)i);
	}
	overview->cpucon = mmio_read_byte_state(&state->mmio, REG_CPUCON);
	mmio_write_byte_state(&state->mmio, REG_POSTID, postid);
	mmio_suppress_debug_access_state(&state->mmio, false);
}

void machine_state_debug_get_snapshot(
	struct machine_state *state,
	struct machine_debug_snapshot *snapshot
) {
	size_t i;
	uint8_t postid;

	if (!snapshot) {
		return;
	}
	memset(snapshot, 0, sizeof(*snapshot));
	if (!state) {
		return;
	}

	snapshot->pc = state->cpu.pc;
	mmio_suppress_debug_access_state(&state->mmio, true);
	postid = mmio_read_byte_state(&state->mmio, REG_POSTID);
	mmio_write_byte_state(&state->mmio, REG_POSTID, MACHINE_DEBUG_POSTID_DISABLED);
	for (i = 0; i < MACHINE_DEBUG_REGISTER_COUNT; i++) {
		snapshot->registers[i] = mmio_read_byte_state(&state->mmio, (uint8_t)i);
	}
	mmio_write_byte_state(&state->mmio, REG_POSTID, postid);
	mmio_suppress_debug_access_state(&state->mmio, false);
	memcpy(snapshot->wbk_registers, state->mmio.ram_wbk, sizeof(snapshot->wbk_registers));
	if (eps_stack_is_descending_even(state->mmio.variant)) {
		const uint8_t depth = machine_state_debug_stack_depth(state);
		const uint8_t copied = depth < MACHINE_DEBUG_STACK_DEPTH
			? depth : MACHINE_DEBUG_STACK_DEPTH;
		uint8_t stack_index;
		memset(snapshot->stack, 0, sizeof(snapshot->stack));
		/* Present the stack in the adapter's conventional oldest-to-newest
		 * order even when the silicon stores the newest return at raw STKPTR. */
		for (stack_index = 0; stack_index < copied; ++stack_index) {
			const uint8_t raw_index = (uint8_t)(0xfeu - 2u * stack_index);
			snapshot->stack[stack_index] = state->cpu.stack[raw_index];
		}
		snapshot->stack_pointer = copied;
	}
	else {
		memcpy(snapshot->stack, state->cpu.stack, sizeof(snapshot->stack));
		snapshot->stack_pointer = machine_state_debug_stack_depth(state);
	}
}

uint8_t machine_state_debug_read_byte(struct machine_state *state, uint8_t addr) {
	uint8_t byte;
	if (!state) {
		return 0;
	}

	mmio_suppress_debug_access_state(&state->mmio, true);
	byte = mmio_read_byte_state(&state->mmio, addr);
	mmio_suppress_debug_access_state(&state->mmio, false);
	return byte;
}

void machine_state_debug_write_byte(struct machine_state *state, uint8_t addr, uint8_t byte) {
	if (!state) {
		return;
	}
	switch (addr) {
	case REG_BSR:
	case REG_BSR1:
	case REG_BSR2:
		byte &= 0x3f;
		break;
	case REG_STKPTR:
		if (!eps_stack_is_descending_even(state->mmio.variant))
			byte &= MACHINE_DEBUG_STACK_DEPTH - 1;
		break;
	case REG_CPUCON:
		byte &= BIT_WBK | BIT_GLINT | BIT_MS1 | BIT_MS0;
		break;
	case REG_LCDARH:
		byte &= MASK_LCD_CONTRAST | MASK_LCD_ADDRESS_HIGH;
		break;
	default:
		break;
	}

	mmio_suppress_debug_access_state(&state->mmio, true);
	mmio_write_byte_state(&state->mmio, addr, byte);
	mmio_suppress_debug_access_state(&state->mmio, false);
	if ((addr == REG_PCL) || (addr == REG_PCM) || (addr == REG_PCH)) {
		state->cpu.pc = machine_debug_read_pc(state);
	}
}

uint8_t machine_state_debug_peek_memory(struct machine_state *state, uint32_t linear_addr) {
	uint8_t postid;
	uint8_t byte;

	if (!state || linear_addr >= machine_state_debug_linear_memory_size(state)) {
		return 0xff;
	}
	if (linear_addr >= 0x80) {
		return state->mmio.ram[linear_addr - 0x80];
	}

	mmio_suppress_debug_access_state(&state->mmio, true);
	postid = mmio_read_byte_state(&state->mmio, eps_reg_postid(state->mmio.variant));
	mmio_write_byte_state(&state->mmio, eps_reg_postid(state->mmio.variant), MACHINE_DEBUG_POSTID_DISABLED);
	byte = mmio_read_byte_state(&state->mmio, (uint8_t)linear_addr);
	mmio_write_byte_state(&state->mmio, eps_reg_postid(state->mmio.variant), postid);
	mmio_suppress_debug_access_state(&state->mmio, false);
	return byte;
}

bool machine_state_debug_write_memory(struct machine_state *state, uint32_t linear_addr, uint8_t byte) {
	if (!state || linear_addr >= machine_state_debug_linear_memory_size(state)) {
		return false;
	}
	if (linear_addr >= 0x80) {
		state->mmio.ram[linear_addr - 0x80] = byte;
		return true;
	}
	machine_state_debug_write_byte(state, (uint8_t)linear_addr, byte);
	return true;
}

uint16_t machine_state_debug_read_rom_word(const struct machine_state *state, uint32_t word_addr) {
	if (!state) {
		return 0xffff;
	}
	return rom_read_word(&state->rom, word_addr);
}

bool machine_state_debug_write_rom_word(struct machine_state *state, uint32_t word_addr, uint16_t word) {
	if (!state) {
		return false;
	}
	return rom_write_word(&state->rom, word_addr, word);
}

void machine_state_debug_set_memory_access_callback(
	struct machine_state *state,
	machine_debug_memory_access_callback callback,
	void *user
) {
	if (!state)
		return;
	mmio_set_debug_access_callback_state(&state->mmio, callback, user);
}

void machine_state_debug_step(struct machine_state *state) {
	if (!state) {
		return;
	}

	cpu_loop_state(&state->cpu, 1);
}
