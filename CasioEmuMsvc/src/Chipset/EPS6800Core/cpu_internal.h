/* Internal CPU state and API. */
#ifndef FX_EMU_CORE_CPU_INTERNAL_H
#define FX_EMU_CORE_CPU_INTERNAL_H

#include "diag_internal.h"
#include "mmio_layout.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum cpu_mode { CPU_MODE_SLOW, CPU_MODE_FAST, CPU_MODE_IDLE, CPU_MODE_SLEEP };

enum {
	CPU_STACK_DEPTH = 32,
	CPU_TRACE_VERBOSE_BUFFER_SIZE = 4096
};

typedef uint16_t (*cpu_rom_read_fn)(uint32_t addr, void *user);
typedef void (*cpu_trace_write_fn)(const char *data, size_t size, void *user);
typedef bool (*cpu_trace_disasm_fn)(uint16_t addr, uint32_t instr, char *out, size_t out_size, void *user);

struct mmio_state;

struct cpu_trace_state {
	uint8_t regs_before[MMIO_REG_COUNT];
	uint8_t regs_after[MMIO_REG_COUNT];
	uint8_t wbk_before[MMIO_WBK_COUNT];
	uint8_t wbk_after[MMIO_WBK_COUNT];
	uint8_t ram_before[MMIO_RAM_COUNT];
	uint8_t ram_after[MMIO_RAM_COUNT];
	bool net_pending;
	uint32_t net_pc;
	uint32_t net_instr;
	cpu_trace_write_fn write_fn;
	void *write_user;
	cpu_trace_disasm_fn disasm_fn;
	void *disasm_user;
	uint64_t lines;
	bool have_last;
	uint32_t last_pc;
	uint32_t last_instr;
	bool repeat_collapse_active;
	bool verbose;
	bool in_instruction;
	char verbose_buf[CPU_TRACE_VERBOSE_BUFFER_SIZE];
	size_t verbose_buf_pos;
};

struct cpu_state {
	uint8_t status; /* r0fh */
	uint32_t pc; /* program counter */
	uint32_t stack[CPU_STACK_DEPTH];
	uint8_t rpt_counter;
	enum cpu_mode mode;
	uint8_t int_pending;
	bool sleep_repeat_pc;
	cpu_rom_read_fn rom_read_fn;
	void *rom_read_user;
	struct mmio_state *mmio;
	struct core_diag_state diag;
	struct cpu_trace_state trace;
};

void cpu_loop_state(struct cpu_state *state, uint32_t count);
void cpu_reset_state(struct cpu_state *state);
void cpu_interrupt_state(struct cpu_state *state, uint8_t int_level);
void cpu_wake_state(struct cpu_state *state, uint8_t source);
uint8_t cpu_get_status_state(struct cpu_state *state);
void cpu_set_status_state(struct cpu_state *state, uint8_t status);
bool cpu_is_sleep_repeating_state(const struct cpu_state *state);
void cpu_set_rom_reader_state(struct cpu_state *state, cpu_rom_read_fn read_fn, void *user);
void cpu_connect_mmio_state(struct cpu_state *state, struct mmio_state *mmio);
void cpu_set_diag_writer_state(struct cpu_state *state, core_diag_write_fn write_fn, void *user);
bool cpu_trace_enable_state(struct cpu_state *state, cpu_trace_write_fn write_fn, void *user);
void cpu_trace_disable_state(struct cpu_state *state);
uint64_t cpu_trace_count_state(const struct cpu_state *state);
void cpu_trace_set_verbose_state(struct cpu_state *state, bool v);
void cpu_trace_set_disassembler_state(struct cpu_state *state, cpu_trace_disasm_fn disasm_fn, void *user);
void cpu_verbose_log_read_state(struct cpu_state *state, uint8_t addr, uint8_t val);

#endif /* FX_EMU_CORE_CPU_INTERNAL_H */


