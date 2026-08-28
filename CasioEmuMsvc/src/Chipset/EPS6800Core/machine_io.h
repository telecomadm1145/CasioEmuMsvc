/* Emulated machine execution, input, and LCD framebuffer boundary. */
#ifndef FX_EMU_CORE_MACHINE_IO_H
#define FX_EMU_CORE_MACHINE_IO_H

#include "lcd_geometry.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LCD geometry exposed to host frontends */
#define MACHINE_LCD_VISIBLE_WIDTH FX_EMU_LCD_VISIBLE_WIDTH
#define MACHINE_LCD_VISIBLE_HEIGHT FX_EMU_LCD_VISIBLE_HEIGHT
#define MACHINE_LCD_PAGE_HEIGHT FX_EMU_LCD_PAGE_HEIGHT
#define MACHINE_LCD_PAGE_COUNT (MACHINE_LCD_VISIBLE_HEIGHT / MACHINE_LCD_PAGE_HEIGHT)
#define MACHINE_LCD_FRAMEBUFFER_SIZE (MACHINE_LCD_VISIBLE_WIDTH * MACHINE_LCD_PAGE_COUNT)

struct machine_state;

enum machine_cpu_mode {
	MACHINE_CPU_MODE_SLOW = 0,
	MACHINE_CPU_MODE_FAST = 1,
	MACHINE_CPU_MODE_IDLE = 2,
	MACHINE_CPU_MODE_SLEEP = 3
};

struct machine_lcd_control {
	uint8_t lcdarh;
	uint8_t lcdcon;
};

/* Host execution boundary.  Callers can schedule the machine without
 * depending on the internal cpu_state/timer_state representation. */
enum machine_cpu_mode machine_state_cpu_mode(const struct machine_state *state);
uint8_t machine_state_interrupt_pending(const struct machine_state *state);
void machine_state_advance_cycles_split(
	struct machine_state *state,
	uint32_t cycles,
	bool tick_fast_timers,
	bool tick_timer1
);
void machine_state_advance_instruction_cycles(
	struct machine_state *state,
	uint32_t timer_cycles,
	bool tick_fast_timers,
	bool tick_timer1
);
void machine_state_tick_idle_timer1(struct machine_state *state, uint32_t cycles);
void machine_state_run_frame(struct machine_state *state);

/* Copies page-major visible LCD bytes: page * MACHINE_LCD_VISIBLE_WIDTH + column. */
size_t machine_state_lcd_copy_framebuffer(const struct machine_state *state, uint8_t *data, size_t size);
/* Copies the visible framebuffer and its associated control registers together. */
size_t machine_state_lcd_copy_display(
	const struct machine_state *state,
	uint8_t *data,
	size_t size,
	struct machine_lcd_control *control
);
/* Raw LCD RAM access used by debugger/editor surfaces. */
uint8_t machine_state_lcd_read_memory(const struct machine_state *state, size_t address);
bool machine_state_lcd_write_memory(struct machine_state *state, size_t address, uint8_t value);

void machine_state_keydown(struct machine_state *state, uint8_t key);
void machine_state_restore_keydown(struct machine_state *state, uint8_t key);
void machine_state_keyup(struct machine_state *state, uint8_t key);
void machine_state_ondown(struct machine_state *state);
void machine_state_onup(struct machine_state *state);
void machine_state_set_portb_input(struct machine_state *state, uint8_t mask, uint8_t value);
void machine_state_set_portc_input(struct machine_state *state, uint8_t mask, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* FX_EMU_CORE_MACHINE_IO_H */

