/* Emulated machine debugger boundary. */
#ifndef FX_EMU_CORE_MACHINE_DEBUG_H
#define FX_EMU_CORE_MACHINE_DEBUG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MACHINE_DEBUG_LOW_REGISTER_COUNT 0x13
#define MACHINE_DEBUG_CPUCON_REGISTER_INDEX 0x20

struct machine_debug_state {
	uint32_t pc;
	uint8_t acc;
	uint8_t status;
};

struct machine_debug_register_overview {
	uint8_t low_regs[MACHINE_DEBUG_LOW_REGISTER_COUNT];
	uint8_t cpucon;
};

struct machine_state;

void machine_state_debug_get_state(struct machine_state *machine, struct machine_debug_state *state);
uint32_t machine_state_debug_fetch_instruction(const struct machine_state *state, uint32_t pc);
void machine_state_debug_get_register_overview(
	struct machine_state *state,
	struct machine_debug_register_overview *overview
);
uint8_t machine_state_debug_read_byte(struct machine_state *state, uint8_t addr);
void machine_state_debug_write_byte(struct machine_state *state, uint8_t addr, uint8_t byte);
void machine_state_debug_step(struct machine_state *state);

#ifdef __cplusplus
}
#endif

#endif /* FX_EMU_CORE_MACHINE_DEBUG_H */


