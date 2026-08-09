/* Emulated machine trace boundary. */
#ifndef FX_EMU_CORE_MACHINE_TRACE_H
#define FX_EMU_CORE_MACHINE_TRACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct machine_state;

typedef void (*machine_trace_write_fn)(const char *data, size_t size, void *user);
typedef bool (*machine_trace_disasm_fn)(uint16_t addr, uint32_t instr, char *out, size_t out_size, void *user);

uint32_t machine_state_trace_step(struct machine_state *state, uint32_t cycles);
bool machine_state_trace_enable(struct machine_state *state, machine_trace_write_fn write_fn, void *user);
void machine_state_trace_disable(struct machine_state *state);
uint64_t machine_state_trace_count(const struct machine_state *state);
void machine_state_trace_set_verbose(struct machine_state *state, bool verbose);
void machine_state_trace_set_disassembler(
	struct machine_state *state,
	machine_trace_disasm_fn disasm_fn,
	void *user
);

#ifdef __cplusplus
}
#endif

#endif /* FX_EMU_CORE_MACHINE_TRACE_H */


