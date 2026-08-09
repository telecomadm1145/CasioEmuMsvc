/* Internal MMIO state and API. */
#ifndef FX_EMU_CORE_MMIO_INTERNAL_H
#define FX_EMU_CORE_MMIO_INTERNAL_H

#include "mmio_layout.h"

#include <stdint.h>

struct cpu_state;
struct kbd_state;
struct lcd_state;
struct timer_state;

struct mmio_state {
	uint8_t regs[MMIO_REG_COUNT];
	uint8_t ram_wbk[MMIO_WBK_COUNT]; /* Mapped into 0x25-0x3F if WBK=1 */
	uint8_t ram[MMIO_RAM_COUNT];
	struct cpu_state *cpu;
	struct kbd_state *kbd;
	struct lcd_state *lcd;
	struct timer_state *timer;
};

void mmio_connect_peripherals_state(
	struct mmio_state *state,
	struct kbd_state *kbd,
	struct lcd_state *lcd,
	struct timer_state *timer
);
void mmio_connect_cpu_state(struct mmio_state *state, struct cpu_state *cpu);
void mmio_bad_read_byte_state(struct mmio_state *state, uint8_t addr);
void mmio_bad_write_byte_state(struct mmio_state *state, uint8_t addr);
uint8_t mmio_read_byte_state(struct mmio_state *state, uint8_t addr);
void mmio_write_byte_state(struct mmio_state *state, uint8_t addr, uint8_t byte);
void mmio_write_byte_internal_state(struct mmio_state *state, uint8_t addr, uint8_t byte);
uint8_t mmio_read_byte_internal_state(struct mmio_state *state, uint8_t addr);
void mmio_post_id_state(struct mmio_state *state, uint8_t addr);
void mmio_carry_propagate_state(struct mmio_state *state, uint8_t addr);
void mmio_borrow_propagate_state(struct mmio_state *state, uint8_t addr);
void mmio_init_state(struct mmio_state *state);
void mmio_reset_state(struct mmio_state *state);
void mmio_trace_snapshot_state(
	const struct mmio_state *state,
	uint8_t *regs_out,
	uint8_t *ram_wbk_out,
	uint8_t *ram_out
);

#endif /* FX_EMU_CORE_MMIO_INTERNAL_H */


