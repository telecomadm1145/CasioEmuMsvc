/* Emulated machine debugger boundary. */
#ifndef FX_EMU_CORE_MACHINE_DEBUG_H
#define FX_EMU_CORE_MACHINE_DEBUG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MACHINE_DEBUG_LOW_REGISTER_COUNT 0x13
#define MACHINE_DEBUG_CPUCON_REGISTER_INDEX 0x20
#define MACHINE_DEBUG_REGISTER_COUNT 0x80
#define MACHINE_DEBUG_WBK_REGISTER_COUNT 27
#define MACHINE_DEBUG_STACK_DEPTH 32
#define MACHINE_DEBUG_BANK_RAM_SIZE (64 * 128)
#define MACHINE_DEBUG_LINEAR_MEMORY_SIZE (0x80 + MACHINE_DEBUG_BANK_RAM_SIZE)

struct machine_debug_state {
	uint32_t pc;
	uint8_t acc;
	uint8_t status;
};

struct machine_debug_register_overview {
	uint8_t low_regs[MACHINE_DEBUG_LOW_REGISTER_COUNT];
	uint8_t cpucon;
};

struct machine_debug_snapshot {
	uint32_t pc;
	uint8_t registers[MACHINE_DEBUG_REGISTER_COUNT];
	uint8_t wbk_registers[MACHINE_DEBUG_WBK_REGISTER_COUNT];
	uint32_t stack[MACHINE_DEBUG_STACK_DEPTH];
	uint8_t stack_pointer;
};

struct machine_state;
typedef bool (*machine_debug_memory_access_callback)(
	void *user,
	uint32_t linear_address,
	uint8_t *value,
	bool write,
	bool before
);

void machine_state_debug_get_state(struct machine_state *machine, struct machine_debug_state *state);
uint32_t machine_state_debug_fetch_instruction(const struct machine_state *state, uint32_t pc);
void machine_state_debug_get_register_overview(
	struct machine_state *state,
	struct machine_debug_register_overview *overview
);
void machine_state_debug_get_snapshot(
	struct machine_state *state,
	struct machine_debug_snapshot *snapshot
);
uint8_t machine_state_debug_read_byte(struct machine_state *state, uint8_t addr);
void machine_state_debug_write_byte(struct machine_state *state, uint8_t addr, uint8_t byte);
uint8_t machine_state_debug_peek_memory(struct machine_state *state, uint32_t linear_addr);
bool machine_state_debug_write_memory(struct machine_state *state, uint32_t linear_addr, uint8_t byte);
uint16_t machine_state_debug_read_rom_word(const struct machine_state *state, uint32_t word_addr);
bool machine_state_debug_write_rom_word(struct machine_state *state, uint32_t word_addr, uint16_t word);
void machine_state_debug_step(struct machine_state *state);
void machine_state_debug_set_memory_access_callback(
	struct machine_state *state,
	machine_debug_memory_access_callback callback,
	void *user
);

#ifdef __cplusplus
}
#endif

#endif /* FX_EMU_CORE_MACHINE_DEBUG_H */
