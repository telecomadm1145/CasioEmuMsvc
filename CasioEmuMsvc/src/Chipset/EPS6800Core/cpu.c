/* ePS6800 Emulation Core */
#include "cpu_internal.h"
#include "eps6800.h"
#include "mmio_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum {
	CPU_BYTE_MASK = 0xFF,
	CPU_IMM8_MASK = 0x00FF,
	CPU_BIT_INDEX_MASK = 0x0700,
	CPU_MOVR_PAGE_MASK = 0x1F00,
	CPU_LOW_NIBBLE_MASK = 0x0F,
	CPU_HIGH_NIBBLE_MASK = 0xF0,
	CPU_NIBBLE_SHIFT = 4,
	CPU_ALU_CARRY_MASK = 0x0100,
	CPU_ALU_WIDE_HIGH_MASK = 0xFF00,
	CPU_BCD_DIGIT_MAX = 9,
	CPU_BCD_LOW_DIGIT_ADJUST = 0x06,
	CPU_BCD_HIGH_DIGIT_MAX = 0x90,
	CPU_BCD_HIGH_DIGIT_ADJUST = 0x60,
	CPU_BCD_TENS_COMPLEMENT_BASE = 0x9A,
	CPU_OPCODE_FULL_MASK = 0xFFFF,
	CPU_OPCODE_LONG_BRANCH_MASK = 0xFFFE,
	CPU_OPCODE_HIGH_BYTE_MASK = 0xFF00,
	CPU_OPCODE_TBRD_MASK = 0xFC00,
	CPU_OPCODE_BIT_OP_MASK = 0xF800,
	CPU_OPCODE_SCALL_MASK = 0xF000,
	CPU_OPCODE_TBRD_GENERAL = 0x2C00,
	CPU_OPCODE_S0CALL = 0x3000,
	CPU_OPCODE_SCALL = 0xE000,
	CPU_S0CALL_ADDRESS_MASK = 0x0FFF,
	CPU_OPCODE_SHORT_GROUP_MASK = 0xE000,
	CPU_OPCODE_NOP = 0x0000,
	CPU_OPCODE_UNIMPLEMENTED = 0x0001,
	CPU_OPCODE_RET = 0x2BFE,
	CPU_OPCODE_RETI = 0x2BFF,
	CPU_OPCODE_LJMP = 0x0020,
	CPU_OPCODE_LCALL = 0x0030,
	CPU_OPCODE_MOV_A_R = 0x2000,
	CPU_OPCODE_MOV_R_A = 0x2100,
	CPU_OPCODE_MOV_A_IMM = 0x4E00,
	CPU_OPCODE_TEST_R = 0x2500,
	CPU_OPCODE_RPT_R = 0x2700,
	CPU_OPCODE_BANK_IMM = 0x4300,
	CPU_OPCODE_CLR_R = 0x2400,
	CPU_OPCODE_TBPTL_IMM = 0x4000,
	CPU_OPCODE_TBPTM_IMM = 0x4100,
	CPU_OPCODE_TBPTH_IMM = 0x4200,
	CPU_OPCODE_TBRD_A_R = 0x2F00,
	CPU_OPCODE_OR_A_R = 0x0200,
	CPU_OPCODE_OR_R_A = 0x0300,
	CPU_OPCODE_OR_A_IMM = 0x4400,
	CPU_OPCODE_AND_A_R = 0x0400,
	CPU_OPCODE_AND_R_A = 0x0500,
	CPU_OPCODE_AND_A_IMM = 0x4500,
	CPU_OPCODE_XOR_A_R = 0x0600,
	CPU_OPCODE_XOR_R_A = 0x0700,
	CPU_OPCODE_XOR_A_IMM = 0x4600,
	CPU_OPCODE_COMA_R = 0x0800,
	CPU_OPCODE_COM_R = 0x0900,
	CPU_OPCODE_INCA_R = 0x1C00,
	CPU_OPCODE_INC_R = 0x1D00,
	CPU_OPCODE_ADD_A_R = 0x1000,
	CPU_OPCODE_ADD_R_A = 0x1100,
	CPU_OPCODE_ADD_A_IMM = 0x4A00,
	CPU_OPCODE_ADC_A_R = 0x1200,
	CPU_OPCODE_ADC_R_A = 0x1300,
	CPU_OPCODE_ADC_A_IMM = 0x4B00,
	CPU_OPCODE_DECA_R = 0x1E00,
	CPU_OPCODE_DEC_R = 0x1F00,
	CPU_OPCODE_SUB_A_R = 0x1600,
	CPU_OPCODE_SUB_R_A = 0x1700,
	CPU_OPCODE_SUB_A_IMM = 0x4C00,
	CPU_OPCODE_SUBB_A_R = 0x1800,
	CPU_OPCODE_SUBB_R_A = 0x1900,
	CPU_OPCODE_SUBB_A_IMM = 0x4D00,
	CPU_OPCODE_ADDDC_A_R = 0x1400,
	CPU_OPCODE_ADDDC_R_A = 0x1500,
	CPU_OPCODE_SUBDB_A_R = 0x1A00,
	CPU_OPCODE_SUBDB_R_A = 0x1B00,
	CPU_OPCODE_RRCA_R = 0x0A00,
	CPU_OPCODE_RRC_R = 0x0B00,
	CPU_OPCODE_RLCA_R = 0x0C00,
	CPU_OPCODE_RLC_R = 0x0D00,
	CPU_OPCODE_SHRA_R = 0x2200,
	CPU_OPCODE_SHLA_R = 0x2300,
	CPU_OPCODE_EX_R = 0x5400,
	CPU_OPCODE_EXL_R = 0x5200,
	CPU_OPCODE_EXH_R = 0x5300,
	CPU_OPCODE_MOVL_R_A = 0x2600,
	CPU_OPCODE_MOVH_R_A = 0x2800,
	CPU_OPCODE_MOVL_A_R = 0x2900,
	CPU_OPCODE_MOVH_A_R = 0x2A00,
	CPU_OPCODE_SFR4_R = 0x0100,
	CPU_OPCODE_SFL4_R = 0x4F00,
	CPU_OPCODE_SWAP_R = 0x0F00,
	CPU_OPCODE_SWAPA_R = 0x0E00,
	CPU_OPCODE_JDNZ_A_R = 0x5000,
	CPU_OPCODE_JDNZ_R = 0x5100,
	CPU_OPCODE_JGE_A_IMM = 0x4700,
	CPU_OPCODE_JLE_A_IMM = 0x4800,
	CPU_OPCODE_JE_A_IMM = 0x4900,
	CPU_OPCODE_JGE_A_R = 0x5500,
	CPU_OPCODE_JLE_A_R = 0x5600,
	CPU_OPCODE_JE_A_R = 0x5700,
	CPU_OPCODE_BC_R_BIT = 0x6800,
	CPU_OPCODE_BS_R_BIT = 0x7000,
	CPU_OPCODE_BTG_R_BIT = 0x7800,
	CPU_OPCODE_JBC_R_BIT = 0x5800,
	CPU_OPCODE_JBS_R_BIT = 0x6000,
	CPU_OPCODE_SJMP = 0xC000,
	CPU_OPCODE_MOVRP = 0x8000,
	CPU_OPCODE_MOVPR = 0xA000,
	CPU_INSTR_LOW16_MASK = 0xFFFF
};
enum {
	CPU_TBRD_OPTION_MASK = 0x0300,
	CPU_TBRD_OPTION_INCREMENT = 0x0100,
	CPU_TBRD_OPTION_DECREMENT = 0x0200,
	CPU_PC_MID_SHIFT = 8,
	CPU_PC_HIGH_SHIFT = 16
};
static const uint32_t CPU_PC_HIGH_BITS_MASK = 0xFFFF0000;
static const uint32_t CPU_PC_HIGH_13_BITS_MASK = 0xFFFFE000;
static const uint32_t CPU_TABPTR_MASK = 0x0003FFFF;
enum {
	CPU_PC_LOW_13_BITS_MASK = 0x1FFF,
	CPU_LONG_BRANCH_ADDRESS_MASK = 0x0001FFFF,
	CPU_RESET_STATUS = 0xC0,
	CPU_ROM_HIGH_BYTE_SELECT = 0x01,
	CPU_OPCODE_SLEEP = 0x0002,
	CPU_TRACE_FORMAT_BUFFER_SIZE = 256
};
enum {
	CPU_TRACE_SFR_MEMORY_THRESHOLD = 0x40,
	CPU_TRACE_SFR_MEMORY_OFFSET = 0x40,
	CPU_TRACE_WBK_MEMORY_BASE = 0x25,
	CPU_TRACE_RAM_MEMORY_BASE = 0xC0,
	CPU_TRACE_PC_MASK = 0x1FFFF,
	CPU_TRACE_HOST_ADDR_MASK = 0xFFFF,
	CPU_TRACE_ROM_NIBBLE_MASK = 0x0F,
	CPU_TRACE_ROM_NIBBLE0_SHIFT = 12,
	CPU_TRACE_ROM_NIBBLE1_SHIFT = 8,
	CPU_TRACE_ROM_NIBBLE2_SHIFT = 4,
	CPU_TRACE_DISASM_BUFFER_SIZE = 160
};

void cpu_connect_mmio_state(struct cpu_state *state, struct mmio_state *mmio) {
	state->mmio = mmio;
}

void cpu_set_diag_writer_state(struct cpu_state *state, core_diag_write_fn write_fn, void *user) {
	core_diag_set_writer_state(&state->diag, write_fn, user);
}

static uint16_t cpu_read_rom_word_state(const struct cpu_state *state, uint32_t addr) {
	if (!state->rom_read_fn) {
		return 0;
	}

	return state->rom_read_fn(addr, state->rom_read_user);
}

static uint8_t cpu_read_rom_byte_state(const struct cpu_state *state, uint32_t addr) {
	uint16_t word = cpu_read_rom_word_state(state, addr >> 1);
	if (addr & CPU_ROM_HIGH_BYTE_SELECT) {
		word >>= CPU_PC_MID_SHIFT;
	}
	return (uint8_t)(word & CPU_BYTE_MASK);
}

static bool trace_enabled(const struct cpu_trace_state *trace) {
	return trace->write_fn != NULL;
}

static void trace_write_bytes(struct cpu_trace_state *trace, const char *data, size_t size) {
	if (trace->write_fn && data && (size > 0)) {
		trace->write_fn(data, size, trace->write_user);
	}
}

static void trace_writef(struct cpu_trace_state *trace, const char *fmt, ...) {
	char buf[CPU_TRACE_FORMAT_BUFFER_SIZE];
	va_list args;
	int len;

	va_start(args, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if (len <= 0) {
		return;
	}
	if ((size_t)len >= sizeof(buf)) {
		len = (int)sizeof(buf) - 1;
	}
	trace_write_bytes(trace, buf, (size_t)len);
}

static uint8_t cpu_bus_read(struct cpu_state *state, uint8_t addr) {
	return mmio_read_byte_state(state->mmio, addr);
}

static void cpu_bus_write(struct cpu_state *state, uint8_t addr, uint8_t byte) {
	mmio_write_byte_state(state->mmio, addr, byte);
}

static uint8_t cpu_bus_read_internal(struct cpu_state *state, uint8_t addr) {
	return mmio_read_byte_internal_state(state->mmio, addr);
}

static void cpu_bus_write_internal(struct cpu_state *state, uint8_t addr, uint8_t byte) {
	mmio_write_byte_internal_state(state->mmio, addr, byte);
}

static void cpu_bus_post_id(struct cpu_state *state, uint8_t addr) {
	mmio_post_id_state(state->mmio, addr);
}

static void cpu_bus_carry_propagate(struct cpu_state *state, uint8_t addr) {
	mmio_carry_propagate_state(state->mmio, addr);
}

static void cpu_bus_borrow_propagate(struct cpu_state *state, uint8_t addr) {
	mmio_borrow_propagate_state(state->mmio, addr);
}

static void cpu_bus_reset(struct cpu_state *state) {
	mmio_reset_state(state->mmio);
}

static void cpu_write_pc_registers_state(struct cpu_state *state) {
	cpu_bus_write_internal(state, REG_PCL, state->pc & CPU_BYTE_MASK);
	cpu_bus_write_internal(state, REG_PCM, (state->pc >> CPU_PC_MID_SHIFT) & CPU_BYTE_MASK);
	cpu_bus_write_internal(state, REG_PCH, (state->pc >> CPU_PC_HIGH_SHIFT) & CPU_BYTE_MASK);
}

static uint32_t cpu_read_pc_registers_state(struct cpu_state *state) {
	uint32_t pc = (uint32_t)cpu_bus_read_internal(state, REG_PCH) << CPU_PC_HIGH_SHIFT;
	pc |= (uint32_t)cpu_bus_read_internal(state, REG_PCM) << CPU_PC_MID_SHIFT;
	pc |= (uint32_t)cpu_bus_read_internal(state, REG_PCL);
	return pc;
}

static uint32_t cpu_replace_pc_low16(uint32_t pc, uint16_t low16) {
	return (pc & CPU_PC_HIGH_BITS_MASK) | low16;
}

static uint32_t cpu_replace_pc_low13(uint32_t pc, uint16_t low13) {
	return (pc & CPU_PC_HIGH_13_BITS_MASK) | (low13 & CPU_PC_LOW_13_BITS_MASK);
}

static uint32_t cpu_read_tabptr_state(struct cpu_state *state) {
	uint32_t tabptr = (uint32_t)cpu_bus_read_internal(state, REG_TABPTRH) << CPU_PC_HIGH_SHIFT;
	tabptr |= (uint32_t)cpu_bus_read_internal(state, REG_TABPTRM) << CPU_PC_MID_SHIFT;
	tabptr |= (uint32_t)cpu_bus_read_internal(state, REG_TABPTRL);
	return tabptr & CPU_TABPTR_MASK;
}

static void cpu_write_tabptr_state(struct cpu_state *state, uint32_t tabptr) {
	tabptr &= CPU_TABPTR_MASK;
	cpu_bus_write_internal(state, REG_TABPTRH, (tabptr >> CPU_PC_HIGH_SHIFT) & MASK_TABPTRH);
	cpu_bus_write_internal(state, REG_TABPTRM, (tabptr >> CPU_PC_MID_SHIFT) & CPU_BYTE_MASK);
	cpu_bus_write_internal(state, REG_TABPTRL, tabptr & CPU_BYTE_MASK);
}

static void cpu_bus_trace_snapshot(
	struct cpu_state *state,
	uint8_t *regs_out,
	uint8_t *ram_wbk_out,
	uint8_t *ram_out
) {
	mmio_trace_snapshot_state(state->mmio, regs_out, ram_wbk_out, ram_out);
}

static uint8_t cpu_read_direct_state(struct cpu_state *state, uint8_t addr) {
	switch (addr) {
	case REG_PCL:
		return (state->pc + 1) & CPU_BYTE_MASK;
	case REG_PCM:
		return ((state->pc + 1) >> CPU_PC_MID_SHIFT) & CPU_BYTE_MASK;
	case REG_PCH:
		return ((state->pc + 1) >> CPU_PC_HIGH_SHIFT) & CPU_BYTE_MASK;
	default:
		return cpu_bus_read(state, addr);
	}
}

uint8_t cpu_get_status_state(struct cpu_state *state);
static void cpu_interpret_instruction_state(struct cpu_state *state, uint32_t instr);

static void trace_appendf(struct cpu_trace_state *trace, const char *fmt, ...) {
	va_list args;
	size_t remaining;
	int len;

	if (trace->verbose_buf_pos >= sizeof(trace->verbose_buf)) {
		return;
	}

	remaining = sizeof(trace->verbose_buf) - trace->verbose_buf_pos;
	va_start(args, fmt);
	len = vsnprintf(trace->verbose_buf + trace->verbose_buf_pos, remaining, fmt, args);
	va_end(args);

	if (len <= 0) {
		return;
	}
	if ((size_t)len >= remaining) {
		trace->verbose_buf_pos = sizeof(trace->verbose_buf) - 1;
		return;
	}
	trace->verbose_buf_pos += (size_t)len;
}

static void trace_emit_write_prefix(struct cpu_trace_state *trace) {
	trace_appendf(trace, "  ");
}

static void trace_append_state_diffs(struct cpu_trace_state *trace) {
	for (uint32_t i = 0; i < MMIO_REG_COUNT; i++) {
		/* Skip registers already shown in instruction line: PCL, PCM, PCH, STATUS, BSR, STKPTR */
		if ((i == REG_PCL) || (i == REG_PCM) || (i == REG_PCH) ||
			(i == REG_STATUS) || (i == REG_BSR) || (i == REG_STKPTR) ||
			(i == REG_ACC) || (i == REG_INDF0) || (i == REG_INDF1) ||
			(i == REG_INDF2)) {
			continue;
		}
		if (trace->regs_before[i] != trace->regs_after[i]) {
			trace_emit_write_prefix(trace);
			if (i >= CPU_TRACE_SFR_MEMORY_THRESHOLD) {
				trace_appendf(trace,
					"m%04x:%02x->%02x",
					(unsigned)(i + CPU_TRACE_SFR_MEMORY_OFFSET),
					(unsigned)trace->regs_before[i],
					(unsigned)trace->regs_after[i]);
			}
			else {
				trace_appendf(trace,
					"r%02x:%02x->%02x",
					(unsigned)i,
					(unsigned)trace->regs_before[i],
					(unsigned)trace->regs_after[i]);
			}
		}
	}

	for (uint32_t i = 0; i < MMIO_WBK_COUNT; i++) {
		if (trace->wbk_before[i] != trace->wbk_after[i]) {
			trace_emit_write_prefix(trace);
			trace_appendf(trace,
				"m%04x:%02x->%02x",
				(unsigned)(CPU_TRACE_WBK_MEMORY_BASE + i),
				(unsigned)trace->wbk_before[i],
				(unsigned)trace->wbk_after[i]);
		}
	}

	for (uint32_t i = 0; i < MMIO_RAM_COUNT; i++) {
		if (trace->ram_before[i] != trace->ram_after[i]) {
			trace_emit_write_prefix(trace);
			trace_appendf(trace,
				"m%04x:%02x->%02x",
				(unsigned)(CPU_TRACE_RAM_MEMORY_BASE + i),
				(unsigned)trace->ram_before[i],
				(unsigned)trace->ram_after[i]);
		}
	}
}

static void trace_flush_pending_net_diffs(struct cpu_trace_state *trace) {
	if (!trace_enabled(trace) || !trace->net_pending) {
		return;
	}

	trace->verbose_buf_pos = 0;
	trace_append_state_diffs(trace);
	if (trace->verbose_buf_pos > 0) {
		trace_write_bytes(trace, trace->verbose_buf, (size_t)trace->verbose_buf_pos);
		trace_write_bytes(trace, "\n", 1);
	}
	trace->net_pending = false;
}

static void cpu_trace_instruction_state(struct cpu_state *state, uint32_t cur_pc, uint32_t instr) {
	struct cpu_trace_state *trace = &state->trace;
	if (!trace_enabled(trace)) {
		return;
	}

	/* Collapse RPT repeats: when repeat is active and the instruction stream
	 * is re-executing the same PC/opcode, emit only the first line. */
	if (trace->have_last && (trace->last_pc == cur_pc) && (trace->last_instr == instr)) {
		if ((state->rpt_target_pc != 0) || trace->repeat_collapse_active) {
			if (!trace->verbose) {
				trace->repeat_collapse_active = true;
				return;
			}
		}
	}
	else {
		trace->repeat_collapse_active = false;
	}

	uint16_t w0 = cpu_read_rom_word_state(state, cur_pc);
	trace_writef(trace,
		"%05x %02x %02x %02x %02x  A=%02x ST=%02x BSR=%02x SP=%02x\n",
		(unsigned)(cur_pc & CPU_TRACE_PC_MASK),
		(unsigned)((w0 >> CPU_TRACE_ROM_NIBBLE0_SHIFT) & CPU_TRACE_ROM_NIBBLE_MASK),
		(unsigned)((w0 >> CPU_TRACE_ROM_NIBBLE1_SHIFT) & CPU_TRACE_ROM_NIBBLE_MASK),
		(unsigned)((w0 >> CPU_TRACE_ROM_NIBBLE2_SHIFT) & CPU_TRACE_ROM_NIBBLE_MASK),
		(unsigned)(w0 & CPU_TRACE_ROM_NIBBLE_MASK),
		(unsigned)cpu_bus_read_internal(state, REG_ACC),
		(unsigned)cpu_get_status_state(state),
		(unsigned)cpu_bus_read_internal(state, REG_BSR),
		(unsigned)cpu_bus_read_internal(state, REG_STKPTR));

	trace->lines++;
	trace->have_last = true;
	trace->last_pc = cur_pc;
	trace->last_instr = instr;
}

static void status_zero_state(struct cpu_state *state, uint8_t x) {
	if (x == 0) {
		state->status |= BIT_STATUS_Z;
	}
	else {
		state->status &= (uint8_t)(~BIT_STATUS_Z);
	}
}

static void status_carry_state(struct cpu_state *state, uint8_t x) {
	if (x) {
		state->status |= BIT_STATUS_C;
	}
	else {
		state->status &= (uint8_t)(~BIT_STATUS_C);
	}
}

static uint8_t cpu_bit_mask(uint8_t bit_index) {
	return (uint8_t)(1u << bit_index);
}

static void cpu_push_state(struct cpu_state *state, uint32_t dat) {
	uint8_t stkptr;
	/* STKPTR is a 5-bit circular stack pointer; normalize the register value
	 * before every array access so guest writes or a full stack can never
	 * index outside stack[CPU_STACK_DEPTH]. */
	stkptr = cpu_bus_read_internal(state, REG_STKPTR) & (CPU_STACK_DEPTH - 1);
	state->stack[stkptr] = dat;
	stkptr = (uint8_t)((stkptr + 1) & (CPU_STACK_DEPTH - 1));
	cpu_bus_write_internal(state, REG_STKPTR, stkptr);
}

static uint32_t cpu_pop_state(struct cpu_state *state) {
	uint8_t stkptr;
	stkptr = cpu_bus_read_internal(state, REG_STKPTR) & (CPU_STACK_DEPTH - 1);
	if (stkptr > 0) {
		stkptr--;
	}
	cpu_bus_write_internal(state, REG_STKPTR, stkptr);
	return state->stack[stkptr];
}

static void cpu_handle_interrupt_state(struct cpu_state *state, uint32_t addr) {
	uint8_t cpucon;
	cpucon = cpu_bus_read_internal(state, REG_CPUCON);
	if (cpucon & BIT_GLINT) {
		cpu_push_state(state, state->pc + (state->sleep_repeat_pc ? 1 : 0));
		state->sleep_repeat_pc = false;
		state->pc = addr;
		cpu_write_pc_registers_state(state);
	}
}

void cpu_loop_state(struct cpu_state *state, uint32_t count) {
	int32_t cnt;
	uint32_t instr;
	struct cpu_trace_state *trace = &state->trace;

	/* A request raised while firmware is inside an ISR must remain pending
	 * until RETI restores GLINT.  Clearing it before cpu_handle_interrupt_state
	 * can actually enter the vector loses Timer1 wakeups from Idle. */
	/* The hardware finishes an active RPT before accepting an interrupt. */
	if (state->int_pending && state->rpt_target_pc == 0 &&
		(cpu_bus_read_internal(state, REG_CPUCON) & BIT_GLINT)) {
		if (state->int_pending & INT_LEVEL4_TIMINT) {
			state->int_pending &= ~INT_LEVEL4_TIMINT;
			cpu_handle_interrupt_state(state, ADDR_TIMINT);
		}
		else if (state->int_pending & INT_LEVEL1_PAINT) {
			state->int_pending &= ~INT_LEVEL1_PAINT;
			cpu_handle_interrupt_state(state, ADDR_PAINT);
		}
	}

	cnt = count;
	while ((cnt != 0) && ((state->mode == CPU_MODE_SLOW) || (state->mode == CPU_MODE_FAST))) {
		instr = ((uint32_t)cpu_read_rom_word_state(state, state->pc) << 16) | cpu_read_rom_word_state(state, state->pc + 1);

		if (trace_enabled(trace) && !trace->verbose && trace->net_pending &&
			((trace->net_pc != state->pc) || (trace->net_instr != instr))) {
			trace_flush_pending_net_diffs(trace);
		}

		cpu_trace_instruction_state(state, state->pc, instr);

		if (trace_enabled(trace) && trace->verbose && !trace->repeat_collapse_active) {
			char disasm[CPU_TRACE_DISASM_BUFFER_SIZE];
			trace->verbose_buf_pos = 0;
			if (trace->disasm_fn &&
				trace->disasm_fn((uint16_t)(state->pc & CPU_TRACE_HOST_ADDR_MASK), instr, disasm, sizeof(disasm), trace->disasm_user)) {
				trace_emit_write_prefix(trace);
				trace_appendf(trace, "disasm: %s", disasm);
			}
			cpu_bus_trace_snapshot(state, trace->regs_before, trace->wbk_before, trace->ram_before);
		}
		else if (trace_enabled(trace) && !trace->verbose && !trace->repeat_collapse_active) {
			cpu_bus_trace_snapshot(state, trace->regs_before, trace->wbk_before, trace->ram_before);
			trace->net_pending = true;
			trace->net_pc = state->pc;
			trace->net_instr = instr;
		}

		trace->in_instruction = true;
		cpu_interpret_instruction_state(state, instr);
		trace->in_instruction = false;

		if (trace_enabled(trace) && trace->verbose && !trace->repeat_collapse_active) {
			cpu_bus_trace_snapshot(state, trace->regs_after, trace->wbk_after, trace->ram_after);
			trace_append_state_diffs(trace);
			if (trace->verbose_buf_pos > 0) {
				trace_write_bytes(trace, trace->verbose_buf, (size_t)trace->verbose_buf_pos);
				trace_write_bytes(trace, "\n\n", 2);
			}
		}
		else if (trace_enabled(trace) && !trace->verbose && trace->net_pending) {
			cpu_bus_trace_snapshot(state, trace->regs_after, trace->wbk_after, trace->ram_after);
		}

		cnt--;
		if (state->sleep_repeat_pc) {
			cnt = 0;
		}
	}
}

void cpu_set_status_state(struct cpu_state *state, uint8_t status) {
	state->status = status;
}

void cpu_reset_state(struct cpu_state *state) {
	state->pc = 0;
	state->status = 0;
	state->rpt_counter = 0;
	state->rpt_target_pc = 0;
	state->mode = CPU_MODE_FAST;
	state->int_pending = 0;
	state->sleep_repeat_pc = false;
	cpu_bus_reset(state);
	cpu_set_status_state(state, CPU_RESET_STATUS);
}

void cpu_interrupt_state(struct cpu_state *state, uint8_t int_level) {
	state->int_pending |= int_level;
}

void cpu_wake_state(struct cpu_state *state, uint8_t source) {
	bool timer_idle_wake = (source == WAKE_TIMER) && (state->mode == CPU_MODE_IDLE);
	if (state->sleep_repeat_pc &&
		((source == WAKE_PAINT) || (source == WAKE_ON) || timer_idle_wake)) {
		state->sleep_repeat_pc = false;
		state->pc += 1;
		cpu_write_pc_registers_state(state);
	}
	if ((source == WAKE_PAINT) || (source == WAKE_ON) || timer_idle_wake) {
		state->mode = (cpu_bus_read_internal(state, REG_CPUCON) & BIT_MS0)
			? CPU_MODE_FAST
			: CPU_MODE_SLOW;
	}
}

static void status_flag_state(struct cpu_state *state, uint8_t mask, bool set) {
	if (set) {
		state->status |= mask;
	}
	else {
		state->status &= (uint8_t)(~mask);
	}
}

static void status_arithmetic_state(
	struct cpu_state *state,
	uint16_t unsigned_result,
	int16_t signed_result,
	uint16_t low_digit_result
) {
	status_carry_state(state, unsigned_result > CPU_BYTE_MASK);
	status_flag_state(state, BIT_STATUS_DC, low_digit_result > CPU_LOW_NIBBLE_MASK);
	status_zero_state(state, (uint8_t)unsigned_result);
	status_flag_state(state, BIT_STATUS_OV, signed_result < -128 || signed_result > 127);
	status_flag_state(state, BIT_STATUS_SLE, signed_result <= 0);
	status_flag_state(state, BIT_STATUS_SGE, signed_result >= 0);
}

uint8_t cpu_get_status_state(struct cpu_state *state) {
	return state->status;
}

bool cpu_is_sleep_repeating_state(const struct cpu_state *state) {
	return state->sleep_repeat_pc && (cpu_read_rom_word_state(state, state->pc) == CPU_OPCODE_SLEEP);
}

void cpu_trace_set_verbose_state(struct cpu_state *state, bool v) {
	state->trace.verbose = v;
}

void cpu_trace_set_disassembler_state(struct cpu_state *state, cpu_trace_disasm_fn disasm_fn, void *user) {
	state->trace.disasm_fn = disasm_fn;
	state->trace.disasm_user = user;
}

void cpu_set_rom_reader_state(struct cpu_state *state, cpu_rom_read_fn read_fn, void *user) {
	state->rom_read_fn = read_fn;
	state->rom_read_user = user;
}

void cpu_verbose_log_read_state(struct cpu_state *state, uint8_t addr, uint8_t val) {
	struct cpu_trace_state *trace = &state->trace;
	if (!trace_enabled(trace) || !trace->verbose) {
		return;
	}
	trace_emit_write_prefix(trace);
	trace_appendf(trace, "r%04x = %02x", (unsigned)addr, (unsigned)val);
}

bool cpu_trace_enable_state(struct cpu_state *state, cpu_trace_write_fn write_fn, void *user) {
	struct cpu_trace_state *trace = &state->trace;
	trace->write_fn = NULL;
	trace->write_user = NULL;
	trace->lines = 0;
	trace->have_last = false;
	trace->repeat_collapse_active = false;
	trace->verbose_buf_pos = 0;
	trace->in_instruction = false;
	trace->net_pending = false;

	if (!write_fn) {
		return false;
	}
	trace->write_fn = write_fn;
	trace->write_user = user;
	return true;
}

void cpu_trace_disable_state(struct cpu_state *state) {
	struct cpu_trace_state *trace = &state->trace;
	if (trace_enabled(trace) && !trace->verbose && trace->net_pending) {
		trace_flush_pending_net_diffs(trace);
	}

	trace->write_fn = NULL;
	trace->write_user = NULL;
	trace->have_last = false;
	trace->repeat_collapse_active = false;
	trace->net_pending = false;
}

uint64_t cpu_trace_count_state(const struct cpu_state *state) {
	return state->trace.lines;
}

static uint8_t alu_add_state(struct cpu_state *state, uint8_t a, uint8_t b) {
	const uint16_t result = (uint16_t)a + (uint16_t)b;
	const int16_t signed_result = (int16_t)(int8_t)a + (int16_t)(int8_t)b;
	status_arithmetic_state(state, result, signed_result,
		(uint16_t)(a & CPU_LOW_NIBBLE_MASK) + (uint16_t)(b & CPU_LOW_NIBBLE_MASK));
	return (uint8_t)result;
}

static uint8_t alu_adc_state(struct cpu_state *state, uint8_t a, uint8_t b, uint8_t c) {
	const uint16_t result = (uint16_t)a + (uint16_t)b + (uint16_t)c;
	const int16_t signed_result = (int16_t)(int8_t)a + (int16_t)(int8_t)b + (int16_t)c;
	status_arithmetic_state(state, result, signed_result,
		(uint16_t)(a & CPU_LOW_NIBBLE_MASK) + (uint16_t)(b & CPU_LOW_NIBBLE_MASK) + (uint16_t)c);
	return (uint8_t)result;
}

static uint8_t alu_sub_state(struct cpu_state *state, uint8_t a, uint8_t b) {
	const uint16_t result = (uint16_t)((uint16_t)a - (uint16_t)b);
	const int16_t signed_result = (int16_t)(int8_t)a - (int16_t)(int8_t)b;
	status_flag_state(state, BIT_STATUS_C, a >= b);
	status_flag_state(state, BIT_STATUS_DC,
		(a & CPU_LOW_NIBBLE_MASK) >= (b & CPU_LOW_NIBBLE_MASK));
	status_zero_state(state, (uint8_t)result);
	status_flag_state(state, BIT_STATUS_OV, signed_result < -128 || signed_result > 127);
	status_flag_state(state, BIT_STATUS_SLE, signed_result <= 0);
	status_flag_state(state, BIT_STATUS_SGE, signed_result >= 0);
	return (uint8_t)result;
}

static uint8_t alu_subb_state(struct cpu_state *state, uint8_t a, uint8_t b, uint8_t c) {
	const uint16_t subtrahend = (uint16_t)b + (uint16_t)c;
	const uint16_t result = (uint16_t)((uint16_t)a - subtrahend);
	const int16_t signed_result = (int16_t)(int8_t)a - (int16_t)(int8_t)b - (int16_t)c;
	status_flag_state(state, BIT_STATUS_C, (uint16_t)a >= subtrahend);
	status_flag_state(state, BIT_STATUS_DC,
		(uint16_t)(a & CPU_LOW_NIBBLE_MASK) >=
		(uint16_t)(b & CPU_LOW_NIBBLE_MASK) + (uint16_t)c);
	status_zero_state(state, (uint8_t)result);
	status_flag_state(state, BIT_STATUS_OV, signed_result < -128 || signed_result > 127);
	status_flag_state(state, BIT_STATUS_SLE, signed_result <= 0);
	status_flag_state(state, BIT_STATUS_SGE, signed_result >= 0);
	return (uint8_t)result;
}

/* BCD addition. */
static uint8_t alu_adddc_state(struct cpu_state *state, uint8_t a, uint8_t b, uint8_t c) {
	uint16_t low_digit = (uint16_t)(a & CPU_LOW_NIBBLE_MASK) +
		(uint16_t)(b & CPU_LOW_NIBBLE_MASK) + (uint16_t)c;
	uint16_t high_digit = (uint16_t)(a >> CPU_NIBBLE_SHIFT) +
		(uint16_t)(b >> CPU_NIBBLE_SHIFT);
	bool digit_carry = low_digit >= 10;
	if (digit_carry)
		low_digit = (low_digit + CPU_BCD_LOW_DIGIT_ADJUST) & CPU_LOW_NIBBLE_MASK;
	high_digit += digit_carry ? 1u : 0u;
	const bool carry = high_digit >= 10;
	if (carry)
		high_digit = (high_digit + CPU_BCD_LOW_DIGIT_ADJUST) & CPU_LOW_NIBBLE_MASK;
	const uint8_t result = (uint8_t)(low_digit | (high_digit << CPU_NIBBLE_SHIFT));
	status_flag_state(state, BIT_STATUS_C, carry);
	status_flag_state(state, BIT_STATUS_DC, digit_carry);
	status_zero_state(state, result);
	return result;
}

static uint8_t alu_subdb_state(struct cpu_state *state, uint8_t a, uint8_t b, uint8_t c) {
	const uint8_t complement = c ? (uint8_t)~b : (uint8_t)-b;
	uint16_t low_digit = (uint16_t)(complement & CPU_LOW_NIBBLE_MASK) +
		(uint16_t)(a & CPU_LOW_NIBBLE_MASK);
	uint16_t high_digit = (uint16_t)(complement >> CPU_NIBBLE_SHIFT) +
		(uint16_t)(a >> CPU_NIBBLE_SHIFT);
	bool digit_carry = low_digit >= 16;
	if (digit_carry)
		low_digit &= CPU_LOW_NIBBLE_MASK;
	high_digit += digit_carry ? 1u : 0u;
	bool carry = high_digit >= 16;
	if (carry)
		high_digit &= CPU_LOW_NIBBLE_MASK;
	if ((uint8_t)(b + c) == 0)
		carry = true;
	if (((b + c) & CPU_LOW_NIBBLE_MASK) == 0)
		digit_carry = true;
	if (!digit_carry)
		low_digit = (low_digit - CPU_BCD_LOW_DIGIT_ADJUST) & CPU_LOW_NIBBLE_MASK;
	if (!carry)
		high_digit = (high_digit - CPU_BCD_LOW_DIGIT_ADJUST) & CPU_LOW_NIBBLE_MASK;
	const uint8_t result = (uint8_t)(low_digit | (high_digit << CPU_NIBBLE_SHIFT));
	status_flag_state(state, BIT_STATUS_C, carry);
	status_flag_state(state, BIT_STATUS_DC, digit_carry);
	status_zero_state(state, result);
	return result;
}

static void cpu_interpret_instruction_state(struct cpu_state *state, uint32_t instr) {
	uint16_t instr1;
	uint16_t imm16_1;
	uint8_t imm8_1, imm8_2;
	uint32_t imm32_1;
	uint8_t imm5; /* only used in movrp, movpr */
	uint32_t newpc, mempc;
	uint8_t temp8_1, temp8_2, temp8_3; /* instruction related temp */
	uint32_t temp32; /* instruction related temp */

	instr1 = instr >> 16;
	newpc = state->pc + 1;
	if (state->rpt_target_pc != 0) {
		if (state->rpt_counter != 0) {
			newpc = state->rpt_target_pc;
			state->rpt_counter--;
		}
		else {
			state->rpt_target_pc = 0;
		}
	}

	switch (instr1 & CPU_OPCODE_FULL_MASK) {
		case CPU_OPCODE_NOP:
			break;
		case CPU_OPCODE_UNIMPLEMENTED:
			break;
		case CPU_OPCODE_SLEEP:
			temp8_1 = cpu_bus_read_internal(state, REG_CPUCON);
			state->mode = (temp8_1 & BIT_MS1) ? CPU_MODE_IDLE : CPU_MODE_SLEEP;
			state->sleep_repeat_pc = true;
			newpc = state->pc;
			break;
		case CPU_OPCODE_RETI:
			temp8_1 = cpu_bus_read_internal(state, REG_CPUCON);
			temp8_1 |= BIT_GLINT;
			cpu_bus_write_internal(state, REG_CPUCON, temp8_1);
			newpc = cpu_pop_state(state);
			break;
		case CPU_OPCODE_RET:
			newpc = cpu_pop_state(state);
			break;
		default:
		switch (instr1 & CPU_OPCODE_LONG_BRANCH_MASK) {
			case CPU_OPCODE_LCALL:
				cpu_push_state(state, newpc + 1); /* state->pc+2, lcall 2bytes */
				/* fall through */
			case CPU_OPCODE_LJMP:
				imm32_1 = instr & CPU_LONG_BRANCH_ADDRESS_MASK;
				newpc = imm32_1;
				break;
			default:
			imm8_1 = instr1 & CPU_IMM8_MASK;
			imm16_1 = instr & CPU_INSTR_LOW16_MASK; /* only change low 16 bits of PC */
			imm8_2 = (instr1 & CPU_BIT_INDEX_MASK) >> CPU_PC_MID_SHIFT; /* used in JBC/JBS/BC/BS/BTG */
			switch (instr1 & CPU_OPCODE_HIGH_BYTE_MASK) {
                case CPU_OPCODE_MOV_A_R: cpu_bus_write_internal(state, REG_ACC, cpu_bus_read(state, imm8_1));
							 status_zero_state(state, cpu_bus_read_internal(state, REG_ACC));
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_MOV_R_A: cpu_bus_write(state, imm8_1, cpu_bus_read_internal(state, REG_ACC));
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_MOV_A_IMM: cpu_bus_write_internal(state, REG_ACC, imm8_1);
                             break;
                case CPU_OPCODE_TEST_R: status_zero_state(state, cpu_bus_read(state, imm8_1));
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_RPT_R: state->rpt_counter = cpu_bus_read(state, imm8_1) - 1;
                             state->rpt_target_pc = newpc;
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_BANK_IMM: cpu_bus_write_internal(state, REG_BSR, imm8_1);
                             break;
                case CPU_OPCODE_CLR_R: cpu_bus_write(state, imm8_1, 0);
					         status_zero_state(state, 0);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_TBPTL_IMM: cpu_bus_write_internal(state, REG_TABPTRL, imm8_1);
                             break;
                case CPU_OPCODE_TBPTM_IMM: cpu_bus_write_internal(state, REG_TABPTRM, imm8_1);
                             break;
				case CPU_OPCODE_TBPTH_IMM: cpu_bus_write_internal(state, REG_TABPTRH, imm8_1 & MASK_TABPTRH);
                             break;
				case CPU_OPCODE_TBRD_A_R: temp32 = cpu_read_tabptr_state(state);
							 temp32 = (temp32 + cpu_bus_read_internal(state, REG_ACC)) & CPU_TABPTR_MASK;
                             cpu_bus_write(state, imm8_1, cpu_read_rom_byte_state(state, temp32));
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_OR_A_R: temp8_1 = cpu_bus_read_internal(state, REG_ACC);
                             temp8_1 |= cpu_bus_read(state, imm8_1);
                             status_zero_state(state, temp8_1);
                             cpu_bus_write_internal(state, REG_ACC, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_OR_R_A: temp8_1 = cpu_bus_read_internal(state, REG_ACC);
                             temp8_1 |= cpu_bus_read(state, imm8_1);
                             status_zero_state(state, temp8_1);
                             cpu_bus_write(state, imm8_1, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_OR_A_IMM: temp8_1 = cpu_bus_read_internal(state, REG_ACC);
                             temp8_1 |= imm8_1;
                             status_zero_state(state, temp8_1);
                             cpu_bus_write_internal(state, REG_ACC, temp8_1);
                             break;
                case CPU_OPCODE_AND_A_R: temp8_1 = cpu_bus_read_internal(state, REG_ACC);
                             temp8_1 &= cpu_bus_read(state, imm8_1);
                             status_zero_state(state, temp8_1);
                             cpu_bus_write_internal(state, REG_ACC, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_AND_R_A: temp8_1 = cpu_bus_read_internal(state, REG_ACC);
                             temp8_1 &= cpu_bus_read(state, imm8_1);
                             status_zero_state(state, temp8_1);
                             cpu_bus_write(state, imm8_1, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_AND_A_IMM: temp8_1 = cpu_bus_read_internal(state, REG_ACC);
                             temp8_1 &= imm8_1;
                             status_zero_state(state, temp8_1);
                             cpu_bus_write_internal(state, REG_ACC, temp8_1);
                             break;
                case CPU_OPCODE_XOR_A_R: temp8_1 = cpu_bus_read_internal(state, REG_ACC);
                             temp8_1 ^= cpu_bus_read(state, imm8_1);
                             status_zero_state(state, temp8_1);
                             cpu_bus_write_internal(state, REG_ACC, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_XOR_R_A: temp8_1 = cpu_bus_read_internal(state, REG_ACC);
                             temp8_1 ^= cpu_bus_read(state, imm8_1);
                             status_zero_state(state, temp8_1);
                             cpu_bus_write(state, imm8_1, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_XOR_A_IMM: temp8_1 = cpu_bus_read_internal(state, REG_ACC);
                             temp8_1 ^= imm8_1;
                             status_zero_state(state, temp8_1);
                             cpu_bus_write_internal(state, REG_ACC, temp8_1);
                             break;
                case CPU_OPCODE_COMA_R: temp8_1 = cpu_bus_read(state, imm8_1);
                             temp8_1 = ~temp8_1; 
                             status_zero_state(state, temp8_1);
                             cpu_bus_write_internal(state, REG_ACC, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_COM_R: temp8_1 = cpu_bus_read(state, imm8_1);
                             temp8_1 = ~temp8_1; 
                             status_zero_state(state, temp8_1);
                             cpu_bus_write(state, imm8_1, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_INCA_R: temp8_1 = alu_add_state(state, cpu_read_direct_state(state, imm8_1), 1);
                             cpu_bus_write_internal(state, REG_ACC, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_INC_R: temp8_1 = alu_add_state(state, cpu_read_direct_state(state, imm8_1), 1);
                             cpu_bus_write(state, imm8_1, temp8_1);
                             if (state->status & BIT_STATUS_C) cpu_bus_carry_propagate(state, imm8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_ADD_A_R: cpu_bus_write_internal(state, REG_ACC,
                                alu_add_state(state, cpu_bus_read(state, imm8_1), cpu_bus_read_internal(state, REG_ACC)));
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_ADD_R_A: cpu_bus_write(state, imm8_1,
                                alu_add_state(state, cpu_read_direct_state(state, imm8_1), cpu_bus_read_internal(state, REG_ACC)));
                             if (state->status & BIT_STATUS_C) cpu_bus_carry_propagate(state, imm8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_ADD_A_IMM: cpu_bus_write_internal(state, REG_ACC,
                                alu_add_state(state, imm8_1, cpu_bus_read_internal(state, REG_ACC)));
                             break;
                case CPU_OPCODE_ADC_A_R: cpu_bus_write_internal(state, REG_ACC,
                                alu_adc_state(state, cpu_bus_read(state, imm8_1), cpu_bus_read_internal(state, REG_ACC),
                                state->status & BIT_STATUS_C));
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_ADC_R_A: cpu_bus_write(state, imm8_1,
                                alu_adc_state(state, cpu_bus_read(state, imm8_1), cpu_bus_read_internal(state, REG_ACC),
                                state->status & BIT_STATUS_C));
                             if (state->status & BIT_STATUS_C) cpu_bus_carry_propagate(state, imm8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_ADC_A_IMM: cpu_bus_write_internal(state, REG_ACC,
                                alu_adc_state(state, imm8_1, cpu_bus_read_internal(state, REG_ACC),
                                state->status & BIT_STATUS_C));
                             break;
                case CPU_OPCODE_DECA_R: temp8_1 = alu_sub_state(state, cpu_bus_read(state, imm8_1), 1);
                             cpu_bus_write_internal(state, REG_ACC, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_DEC_R: temp8_1 = alu_sub_state(state, cpu_bus_read(state, imm8_1), 1);
                             cpu_bus_write(state, imm8_1, temp8_1);
                             if (!(state->status & BIT_STATUS_C)) cpu_bus_borrow_propagate(state, imm8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_SUB_A_R: cpu_bus_write_internal(state, REG_ACC,
                                alu_sub_state(state, cpu_bus_read(state, imm8_1), cpu_bus_read_internal(state, REG_ACC)));
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_SUB_R_A: cpu_bus_write(state, imm8_1,
                                alu_sub_state(state, cpu_bus_read(state, imm8_1), cpu_bus_read_internal(state, REG_ACC)));
                             if (!(state->status & BIT_STATUS_C)) cpu_bus_borrow_propagate(state, imm8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_SUB_A_IMM: cpu_bus_write_internal(state, REG_ACC,
                                alu_sub_state(state, imm8_1, cpu_bus_read_internal(state, REG_ACC)));
                             break;
                case CPU_OPCODE_SUBB_A_R: cpu_bus_write_internal(state, REG_ACC,
                                alu_subb_state(state, cpu_bus_read(state, imm8_1), cpu_bus_read_internal(state, REG_ACC),
                                ((state->status & BIT_STATUS_C)?0:1)));
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_SUBB_R_A: cpu_bus_write(state, imm8_1,
                                alu_subb_state(state, cpu_bus_read(state, imm8_1), cpu_bus_read_internal(state, REG_ACC),
                                ((state->status & BIT_STATUS_C)?0:1)));
                             if (!(state->status & BIT_STATUS_C)) cpu_bus_borrow_propagate(state, imm8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_SUBB_A_IMM: cpu_bus_write_internal(state, REG_ACC,
                                alu_subb_state(state, imm8_1, cpu_bus_read_internal(state, REG_ACC),
                                ((state->status & BIT_STATUS_C)?0:1)));
                             break;
                case CPU_OPCODE_ADDDC_A_R: cpu_bus_write_internal(state, REG_ACC,
                                alu_adddc_state(state, cpu_bus_read(state, imm8_1), cpu_bus_read_internal(state, REG_ACC),
                                state->status & BIT_STATUS_C));
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_ADDDC_R_A: cpu_bus_write(state, imm8_1,
                                alu_adddc_state(state, cpu_bus_read(state, imm8_1), cpu_bus_read_internal(state, REG_ACC),
                                state->status & BIT_STATUS_C));
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_SUBDB_A_R: cpu_bus_write_internal(state, REG_ACC,
                                alu_subdb_state(state, cpu_bus_read(state, imm8_1), cpu_bus_read_internal(state, REG_ACC),
                                ((state->status & BIT_STATUS_C)?0:1)));
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_SUBDB_R_A: cpu_bus_write(state, imm8_1,
                                alu_subdb_state(state, cpu_bus_read(state, imm8_1), cpu_bus_read_internal(state, REG_ACC),
                                ((state->status & BIT_STATUS_C)?0:1)));
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_RRCA_R: temp8_1 = cpu_bus_read(state, imm8_1);
                             temp8_2 = temp8_1 & 0x01;
                             temp8_1 >>= 1;
                             temp8_1 |= (state->status & BIT_STATUS_C) << 7;
                             status_carry_state(state, temp8_2);
                             cpu_bus_write_internal(state, REG_ACC, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_RRC_R: temp8_1 = cpu_bus_read(state, imm8_1);
                             temp8_2 = temp8_1 & 0x01;
                             temp8_1 >>= 1;
                             temp8_1 |= (state->status & BIT_STATUS_C) << 7;
                             status_carry_state(state, temp8_2);
                             cpu_bus_write(state, imm8_1, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_RLCA_R: temp8_1 = cpu_bus_read(state, imm8_1);
                             temp8_2 = temp8_1 & 0x80;
                             temp8_1 <<= 1;
                             temp8_1 |= state->status & BIT_STATUS_C;
                             status_carry_state(state, temp8_2);
                             cpu_bus_write_internal(state, REG_ACC, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_RLC_R: temp8_1 = cpu_bus_read(state, imm8_1);
                             temp8_2 = temp8_1 & 0x80;
                             temp8_1 <<= 1;
                             temp8_1 |= state->status & BIT_STATUS_C;
                             status_carry_state(state, temp8_2);
                             cpu_bus_write(state, imm8_1, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_SHRA_R: temp8_1 = cpu_bus_read(state, imm8_1);
                             temp8_1 >>= 1;
                             temp8_1 |= (state->status & BIT_STATUS_C) << 7;
                             cpu_bus_write_internal(state, REG_ACC, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_SHLA_R: temp8_1 = cpu_bus_read(state, imm8_1);
                             temp8_1 <<= 1;
                             temp8_1 |= state->status & BIT_STATUS_C;
                             cpu_bus_write_internal(state, REG_ACC, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_EX_R: temp8_1 = cpu_bus_read_internal(state, REG_ACC);
                             cpu_bus_write_internal(state, REG_ACC, cpu_bus_read(state, imm8_1));
                             cpu_bus_write(state, imm8_1, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_EXL_R: temp8_1 = cpu_bus_read_internal(state, REG_ACC);
                             temp8_2 = cpu_bus_read(state, imm8_1);
                             temp8_3 = temp8_1;
                             temp8_1 &= CPU_HIGH_NIBBLE_MASK;
                             temp8_1 |= temp8_2 & CPU_LOW_NIBBLE_MASK;
                             temp8_2 &= CPU_HIGH_NIBBLE_MASK;
                             temp8_2 |= temp8_3 & CPU_LOW_NIBBLE_MASK;
                             cpu_bus_write(state, imm8_1, temp8_2);
                             cpu_bus_write_internal(state, REG_ACC, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_EXH_R: temp8_1 = cpu_bus_read_internal(state, REG_ACC);
                             temp8_2 = cpu_bus_read(state, imm8_1);
                             cpu_bus_write(state, imm8_1, (temp8_1 << CPU_NIBBLE_SHIFT) | (temp8_2 & CPU_LOW_NIBBLE_MASK));
                             cpu_bus_write_internal(state, REG_ACC, (temp8_1 & CPU_HIGH_NIBBLE_MASK) | (temp8_2 >> CPU_NIBBLE_SHIFT));
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_MOVL_R_A: temp8_1 = cpu_bus_read(state, imm8_1);
                             temp8_1 &= CPU_HIGH_NIBBLE_MASK;
                             temp8_1 |= cpu_bus_read_internal(state, REG_ACC) & CPU_LOW_NIBBLE_MASK;
                             cpu_bus_write(state, imm8_1, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_MOVH_R_A: temp8_1 = cpu_bus_read(state, imm8_1);
                             temp8_1 &= CPU_LOW_NIBBLE_MASK;
                             temp8_1 |= cpu_bus_read_internal(state, REG_ACC) << CPU_NIBBLE_SHIFT;
                             cpu_bus_write(state, imm8_1, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_MOVL_A_R: cpu_bus_write_internal(state, REG_ACC, cpu_bus_read(state, imm8_1) & CPU_LOW_NIBBLE_MASK);
                             cpu_bus_post_id(state, imm8_1);
                             break;
                case CPU_OPCODE_MOVH_A_R: cpu_bus_write_internal(state, REG_ACC, cpu_bus_read(state, imm8_1) >> CPU_NIBBLE_SHIFT);
                             cpu_bus_post_id(state, imm8_1);
                             break;
				case CPU_OPCODE_SFR4_R: temp8_1 = cpu_bus_read_internal(state, REG_ACC);
							 temp8_2 = cpu_bus_read(state, imm8_1);
							 cpu_bus_write_internal(state, REG_ACC, (temp8_1 & CPU_HIGH_NIBBLE_MASK) | (temp8_2 & CPU_LOW_NIBBLE_MASK));
							 temp8_2 >>= CPU_NIBBLE_SHIFT;
							 temp8_2 |= temp8_1 << CPU_NIBBLE_SHIFT;
							 cpu_bus_write(state, imm8_1, temp8_2);
                             cpu_bus_post_id(state, imm8_1);
							 break;
				case CPU_OPCODE_SFL4_R: temp8_1 = cpu_bus_read_internal(state, REG_ACC);
							 temp8_2 = cpu_bus_read(state, imm8_1);
							 cpu_bus_write_internal(state, REG_ACC, (temp8_1 & CPU_HIGH_NIBBLE_MASK) | (temp8_2 >> CPU_NIBBLE_SHIFT));
							 temp8_2 <<= CPU_NIBBLE_SHIFT;
							 temp8_2 |= temp8_1 & CPU_LOW_NIBBLE_MASK;
							 cpu_bus_write(state, imm8_1, temp8_2);
                             cpu_bus_post_id(state, imm8_1);
							 break;
				case CPU_OPCODE_SWAP_R: temp8_1 = cpu_bus_read(state, imm8_1);
							 cpu_bus_write(state, imm8_1, ((temp8_1 >> CPU_NIBBLE_SHIFT) | (temp8_1 << CPU_NIBBLE_SHIFT)));
                             cpu_bus_post_id(state, imm8_1);
							 break;
				case CPU_OPCODE_SWAPA_R: temp8_1 = cpu_bus_read(state, imm8_1);
							 cpu_bus_write_internal(state, REG_ACC, ((temp8_1 >> CPU_NIBBLE_SHIFT) | (temp8_1 << CPU_NIBBLE_SHIFT)));
                             cpu_bus_post_id(state, imm8_1);
							 break;
				case CPU_OPCODE_JDNZ_A_R: temp8_1 = cpu_bus_read(state, imm8_1) - 1;
							 if (temp8_1 != 0) { newpc = cpu_replace_pc_low16(newpc, imm16_1); }
							 else { newpc += 1; }
							 cpu_bus_write_internal(state, REG_ACC, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
					         break;
				case CPU_OPCODE_JDNZ_R: temp8_1 = cpu_bus_read(state, imm8_1) - 1;
							 if (temp8_1 != 0) { newpc = cpu_replace_pc_low16(newpc, imm16_1); }
							 else { newpc += 1; }
							 cpu_bus_write(state, imm8_1, temp8_1);
                             cpu_bus_post_id(state, imm8_1);
							 break;
				case CPU_OPCODE_JGE_A_IMM: if (cpu_bus_read_internal(state, REG_ACC) >= imm8_1) { newpc = cpu_replace_pc_low16(newpc, imm16_1); }
							 else { newpc += 1; }
							 break;
				case CPU_OPCODE_JLE_A_IMM: if (cpu_bus_read_internal(state, REG_ACC) <= imm8_1) { newpc = cpu_replace_pc_low16(newpc, imm16_1); }
							 else { newpc += 1; }
							 break;
				case CPU_OPCODE_JE_A_IMM: if (cpu_bus_read_internal(state, REG_ACC) == imm8_1) { newpc = cpu_replace_pc_low16(newpc, imm16_1); }
							 else { newpc += 1; }
							 break;
				case CPU_OPCODE_JGE_A_R: if (cpu_bus_read_internal(state, REG_ACC) >= cpu_bus_read(state, imm8_1)) { newpc = cpu_replace_pc_low16(newpc, imm16_1); }
							 else { newpc += 1; }
                             cpu_bus_post_id(state, imm8_1);
							 break;
				case CPU_OPCODE_JLE_A_R: if (cpu_bus_read_internal(state, REG_ACC) <= cpu_bus_read(state, imm8_1)) { newpc = cpu_replace_pc_low16(newpc, imm16_1); }
							 else { newpc += 1; }
                             cpu_bus_post_id(state, imm8_1);
							 break;
				case CPU_OPCODE_JE_A_R: if (cpu_bus_read_internal(state, REG_ACC) == cpu_bus_read(state, imm8_1)) { newpc = cpu_replace_pc_low16(newpc, imm16_1); }
							 else { newpc += 1; }
                             cpu_bus_post_id(state, imm8_1);
							 break;
				default:
					if ((instr1 & CPU_OPCODE_TBRD_MASK) == CPU_OPCODE_TBRD_GENERAL) { /* TBRD opt, r */
						temp32 = cpu_read_tabptr_state(state);
						cpu_bus_write(state, imm8_1, cpu_read_rom_byte_state(state, temp32));
						if ((instr1 & CPU_TBRD_OPTION_MASK) == CPU_TBRD_OPTION_INCREMENT)
							temp32++;
						else if ((instr1 & CPU_TBRD_OPTION_MASK) == CPU_TBRD_OPTION_DECREMENT)
							temp32--;
						cpu_write_tabptr_state(state, temp32);
                        cpu_bus_post_id(state, imm8_1);
					}
					else {
						temp8_3 = cpu_bit_mask(imm8_2);
						switch (instr1 & CPU_OPCODE_BIT_OP_MASK) {
							case CPU_OPCODE_BC_R_BIT: cpu_bus_write(state, imm8_1, (uint8_t)(cpu_bus_read(state, imm8_1) & ~temp8_3));
                                         cpu_bus_post_id(state, imm8_1);
										 break;
							case CPU_OPCODE_BS_R_BIT: cpu_bus_write(state, imm8_1, cpu_bus_read(state, imm8_1) | temp8_3);
                                         cpu_bus_post_id(state, imm8_1);
										 break;
							case CPU_OPCODE_BTG_R_BIT: temp8_1 = cpu_bus_read(state, imm8_1);
										 if (temp8_1 & temp8_3)
											temp8_1 &= (uint8_t)(~temp8_3);
										 else
											temp8_1 |= temp8_3;
										 cpu_bus_write(state, imm8_1, temp8_1);
                                         cpu_bus_post_id(state, imm8_1);
										 break;
							case CPU_OPCODE_JBC_R_BIT: if (!(cpu_bus_read(state, imm8_1) & temp8_3)) { newpc = cpu_replace_pc_low16(newpc, imm16_1); }
										 else { newpc += 1; }
                                         cpu_bus_post_id(state, imm8_1);
										 break;
							case CPU_OPCODE_JBS_R_BIT: if ((cpu_bus_read(state, imm8_1) & temp8_3)) { newpc = cpu_replace_pc_low16(newpc, imm16_1); }
										 else { newpc += 1; }
                                         cpu_bus_post_id(state, imm8_1);
										 break;
							default:
								if ((instr1 & CPU_OPCODE_SCALL_MASK) == CPU_OPCODE_S0CALL) {
									cpu_push_state(state, newpc);
									newpc = instr1 & CPU_S0CALL_ADDRESS_MASK;
								}
								else {
									switch (instr1 & CPU_OPCODE_SHORT_GROUP_MASK) {
										case CPU_OPCODE_SCALL: cpu_push_state(state, newpc); /* scall uses the sjmp target decode */
										case CPU_OPCODE_SJMP: newpc = cpu_replace_pc_low13(newpc, instr1);
													 break;
										case CPU_OPCODE_MOVRP: imm5 = (instr1 & CPU_MOVR_PAGE_MASK) >> CPU_PC_MID_SHIFT;
                                                     /* If imm5 and imm8_1 are the same, we are in trouble. */
                                                     cpu_bus_write(state, imm5, cpu_bus_read(state, imm8_1));
                                                     cpu_bus_post_id(state, imm5);
                                                     cpu_bus_post_id(state, imm8_1);
													 break;
										case CPU_OPCODE_MOVPR: imm5 = (instr1 & CPU_MOVR_PAGE_MASK) >> CPU_PC_MID_SHIFT;
                                                     cpu_bus_write(state, imm8_1, cpu_bus_read(state, imm5));
                                                     cpu_bus_post_id(state, imm5);
                                                     cpu_bus_post_id(state, imm8_1);
													 break;
										default:
											core_diag_printf_state(&state->diag, "[Warning] Invalid instruction @ %4xh!\n", state->pc);
											break;
									}
								}
								break;
						}
					}
					break;
            }
            break;
        }
        break;
    }
	
	mempc = cpu_read_pc_registers_state(state);
	if (mempc == state->pc) { /* Not changed during instruction execution. */
		state->pc = newpc;
		cpu_write_pc_registers_state(state);
	}
	else {
		state->pc = mempc; /* Load PC with PC in the register set. */
	}
}


