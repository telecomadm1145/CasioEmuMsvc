/* Emulated machine runtime execution */

#include "machine_io.h"
#include "machine_internal.h"

enum {
	MACHINE_FRAME_ACTIVE_CHUNKS = 20,
	MACHINE_FRAME_IDLE_CHUNKS = 20,
	MACHINE_FRAME_CHUNK_CYCLES = 100
};

static uint16_t machine_read_rom_word(uint32_t addr, void *user) {
	struct machine_state *state = (struct machine_state *)user;
	return rom_read_word(&state->rom, addr);
}

void machine_state_bind_modules(struct machine_state *state) {
	cpu_connect_mmio_state(&state->cpu, &state->mmio);
	cpu_set_rom_reader_state(&state->cpu, machine_read_rom_word, state);
	kbd_connect_bus_state(&state->kbd, &state->cpu, &state->mmio);
	lcd_connect_mmio_state(&state->lcd, &state->mmio);
	timer_connect_bus_state(&state->timer, &state->cpu, &state->mmio);
	mmio_connect_cpu_state(&state->mmio, &state->cpu);
	mmio_connect_peripherals_state(&state->mmio, &state->kbd, &state->lcd, &state->timer);
}

void machine_state_advance_cycles_split(
	struct machine_state *state,
	uint32_t cycles,
	bool tick_fast_timers,
	bool tick_timer1
) {
	cpu_loop_state(&state->cpu, cycles);
	if (state->cpu.mode == CPU_MODE_IDLE) {
		if (tick_timer1)
			timer_tick_idle_state(&state->timer, cycles);
	}
	else if (state->cpu.mode != CPU_MODE_SLEEP) {
		if (tick_fast_timers)
			timer_tick_fast_state(&state->timer, cycles);
		if (tick_timer1)
			timer_tick_idle_state(&state->timer, cycles);
	}
	kbd_tick_state(&state->kbd, cycles);
}

void machine_state_advance_instruction_cycles(
	struct machine_state *state,
	uint32_t timer_cycles,
	bool tick_fast_timers,
	bool tick_timer1
) {
	if (!state) {
		return;
	}

	cpu_loop_state(&state->cpu, 1);
	if (state->cpu.mode == CPU_MODE_IDLE) {
		if (tick_timer1)
			timer_tick_idle_state(&state->timer, timer_cycles);
	}
	else if (state->cpu.mode != CPU_MODE_SLEEP) {
		if (tick_fast_timers)
			timer_tick_fast_state(&state->timer, timer_cycles);
		if (tick_timer1)
			timer_tick_idle_state(&state->timer, timer_cycles);
	}
	kbd_tick_state(&state->kbd, 1);
}

void machine_state_advance_cycles(struct machine_state *state, uint32_t cycles, bool tick_timer) {
	machine_state_advance_cycles_split(state, cycles, tick_timer, tick_timer);
}

static void machine_state_run_chunks(struct machine_state *state, int chunks, bool tick_timer) {
	int i;

	for (i = 0; i < chunks; i++) {
		machine_state_advance_cycles(state, MACHINE_FRAME_CHUNK_CYCLES, tick_timer);
	}
}

void machine_state_run_frame(struct machine_state *state) {
	if (!state) {
		return;
	}

	machine_state_run_chunks(state, MACHINE_FRAME_ACTIVE_CHUNKS, true);
	machine_state_run_chunks(state, MACHINE_FRAME_IDLE_CHUNKS, false);
}


