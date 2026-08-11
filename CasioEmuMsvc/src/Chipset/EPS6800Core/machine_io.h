/* Emulated machine execution, input, and LCD framebuffer boundary. */
#ifndef FX_EMU_CORE_MACHINE_IO_H
#define FX_EMU_CORE_MACHINE_IO_H

#include "lcd_geometry.h"

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

struct machine_lcd_control {
	uint8_t lcdarh;
	uint8_t lcdcon;
};

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
void machine_state_keydown(struct machine_state *state, uint8_t key);
void machine_state_keyup(struct machine_state *state, uint8_t key);
void machine_state_ondown(struct machine_state *state);
void machine_state_onup(struct machine_state *state);

#ifdef __cplusplus
}
#endif

#endif /* FX_EMU_CORE_MACHINE_IO_H */

