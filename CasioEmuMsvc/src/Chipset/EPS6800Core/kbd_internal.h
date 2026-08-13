/* Internal keypad state and API. */
#ifndef FX_EMU_CORE_KBD_INTERNAL_H
#define FX_EMU_CORE_KBD_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

enum {
	KBD_REG_COUNT = 0x40,
	KBD_ROW_COUNT = 8,
	KBD_COL_COUNT = 8,
	KBD_KEY_COUNT = KBD_ROW_COUNT * KBD_COL_COUNT
};

struct cpu_state;
struct mmio_state;

struct kbd_state {
	uint8_t reg[KBD_REG_COUNT];
	uint8_t matrix[KBD_ROW_COUNT];
	struct cpu_state *cpu;
	struct mmio_state *mmio;
	bool key_pending_down[KBD_KEY_COUNT];
	bool key_pending_up[KBD_KEY_COUNT];
	uint16_t key_press_cycles[KBD_KEY_COUNT];
	uint16_t key_release_cycles[KBD_KEY_COUNT];
	uint8_t pending_press_mask;
	uint8_t porta_latch;
	uint8_t portb_latch;
	uint8_t portc_latch;
	uint8_t portc_input_mask;
	uint8_t portc_input_value;
	bool on_pressed;
	bool on_pending_down;
	bool on_pending_up;
	uint16_t on_press_cycles;
};

void kbd_connect_bus_state(struct kbd_state *state, struct cpu_state *cpu, struct mmio_state *mmio);
uint8_t kbd_read_byte_state(struct kbd_state *state, uint8_t addr);
void kbd_write_byte_state(struct kbd_state *state, uint8_t addr, uint8_t byte);
void kbd_keydown_state(struct kbd_state *state, uint8_t key);
void kbd_restore_keydown_state(struct kbd_state *state, uint8_t key);
void kbd_keyup_state(struct kbd_state *state, uint8_t key);
void kbd_tick_state(struct kbd_state *state, uint32_t cycles);
void kbd_ondown_state(struct kbd_state *state);
void kbd_onup_state(struct kbd_state *state);
void kbd_set_portc_input_state(struct kbd_state *state, uint8_t mask, uint8_t value);
void kbd_reset_state(struct kbd_state *state);

#endif /* FX_EMU_CORE_KBD_INTERNAL_H */


