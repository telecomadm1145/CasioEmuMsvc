/* Internal LCD state and API */
#ifndef FX_EMU_CORE_LCD_INTERNAL_H
#define FX_EMU_CORE_LCD_INTERNAL_H

#include "lcd_geometry.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
	LCD_REG_COUNT = 0x32,
	LCD_FB_STRIDE = 128,
	LCD_FB_PAGES = FX_EMU_LCD_PAGE_COUNT,
	LCD_VISIBLE_WIDTH = FX_EMU_LCD_VISIBLE_WIDTH,
	LCD_VISIBLE_HEIGHT = FX_EMU_LCD_VISIBLE_HEIGHT,
	LCD_FB_SIZE = LCD_FB_STRIDE * LCD_FB_PAGES,
	LCD_W192_WIDTH = 192,
	LCD_W192_PAGE_COUNT = 8,
	LCD_W192_FB_SIZE = LCD_W192_WIDTH * LCD_W192_PAGE_COUNT
};

struct mmio_state;

struct lcd_state {
	uint8_t fb[LCD_FB_SIZE];
	uint8_t w192_fb[LCD_W192_FB_SIZE];
	uint8_t reg[LCD_REG_COUNT]; /* many are not used */
	uint8_t w192_page;
	uint8_t w192_column;
	uint8_t w192_rmw_column;
	uint8_t w192_portd;
	uint8_t w192_porte;
	uint8_t w192_portd_latch;
	uint8_t w192_porte_latch;
	uint8_t w192_dcrde;
	uint8_t w192_read_valid;
	uint8_t w192_bus_phase;
	uint8_t w192_display_on;
	uint8_t w192_all_pixels_on;
	uint8_t w192_rmw_active;
	uint8_t w192_segment_reverse;
	uint8_t w192_com_reverse;
	uint8_t w192_contrast;
	uint8_t w192_contrast_pending;
	struct mmio_state *mmio;
};

void lcd_connect_mmio_state(struct lcd_state *state, struct mmio_state *mmio);
uint8_t lcd_read_byte_state(struct lcd_state *state, uint8_t addr);
void lcd_write_byte_state(struct lcd_state *state, uint8_t addr, uint8_t byte);
uint8_t lcd_gpio_read_byte_state(struct lcd_state *state, uint8_t addr);
void lcd_gpio_write_byte_state(struct lcd_state *state, uint8_t addr, uint8_t byte);
void lcd_process_postid_state(struct lcd_state *state);
uint8_t lcd_ram_read_byte_state(const struct lcd_state *state, uint16_t addr);
size_t lcd_copy_display_state(
	const struct lcd_state *state,
	uint8_t *data,
	size_t size,
	uint8_t *lcdarh,
	uint8_t *lcdcon,
	uint8_t *contrast
);
uint8_t lcd_raw_read_byte_state(const struct lcd_state *state, size_t addr);
bool lcd_raw_write_byte_state(struct lcd_state *state, size_t addr, uint8_t value);
void lcd_reset_state(struct lcd_state *state);

#endif /* FX_EMU_CORE_LCD_INTERNAL_H */


