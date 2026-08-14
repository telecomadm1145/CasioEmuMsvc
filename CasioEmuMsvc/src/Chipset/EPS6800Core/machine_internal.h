/* Internal emulated machine storage */
#ifndef FX_EMU_CORE_MACHINE_INTERNAL_H
#define FX_EMU_CORE_MACHINE_INTERNAL_H

#include "cpu_internal.h"
#include "kbd_internal.h"
#include "lcd_internal.h"
#include "mmio_internal.h"
#include "rom_internal.h"
#include "timer_internal.h"

#include <stdbool.h>
#include <stdint.h>

struct machine_state {
	struct cpu_state cpu;
	struct rom_state rom;
	struct kbd_state kbd;
	struct lcd_state lcd;
	struct mmio_state mmio;
	struct timer_state timer;
};

#ifdef __cplusplus
extern "C" {
#endif

void machine_state_bind_modules(struct machine_state *state);
void machine_state_advance_cycles(struct machine_state *state, uint32_t cycles, bool tick_timer);
void machine_state_advance_cycles_split(
	struct machine_state *state,
	uint32_t cycles,
	bool tick_fast_timers,
	bool tick_timer1
);
/* Run exactly one instruction. The timers advance by `timer_cycles` (the
 * instruction's weighted cycle count, 1 or 2), gated by the two flags;
 * keyboard debounce keeps a per-instruction cadence. */
void machine_state_advance_instruction_cycles(
	struct machine_state *state,
	uint32_t timer_cycles,
	bool tick_fast_timers,
	bool tick_timer1
);

#ifdef __cplusplus
}
#endif

#endif /* FX_EMU_CORE_MACHINE_INTERNAL_H */

