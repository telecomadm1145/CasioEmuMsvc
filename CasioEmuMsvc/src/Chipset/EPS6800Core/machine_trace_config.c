/* Emulated machine trace support */

#include "machine_internal.h"
#include "machine_trace.h"

bool machine_state_trace_enable(struct machine_state *state, machine_trace_write_fn write_fn, void *user) {
	if (!state) {
		return false;
	}

	return cpu_trace_enable_state(&state->cpu, write_fn, user);
}

void machine_state_trace_disable(struct machine_state *state) {
	if (!state) {
		return;
	}

	cpu_trace_disable_state(&state->cpu);
}

uint64_t machine_state_trace_count(const struct machine_state *state) {
	if (!state) {
		return 0;
	}

	return cpu_trace_count_state(&state->cpu);
}

void machine_state_trace_set_verbose(struct machine_state *state, bool verbose) {
	if (!state) {
		return;
	}

	cpu_trace_set_verbose_state(&state->cpu, verbose);
}

void machine_state_trace_set_disassembler(
	struct machine_state *state,
	machine_trace_disasm_fn disasm_fn,
	void *user
) {
	if (!state) {
		return;
	}

	cpu_trace_set_disassembler_state(&state->cpu, disasm_fn, user);
}

uint32_t machine_state_trace_step(struct machine_state *state, uint32_t cycles) {
	if (!state) {
		return cycles;
	}

	machine_state_advance_cycles(state, cycles, true);
	return cpu_is_sleep_repeating_state(&state->cpu) ? 1 : cycles;
}


