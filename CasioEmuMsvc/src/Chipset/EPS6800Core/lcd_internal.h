/* Internal LCD state and API */
#ifndef FX_EMU_CORE_LCD_INTERNAL_H
#define FX_EMU_CORE_LCD_INTERNAL_H

#include "lcd_geometry.h"

#include <stdint.h>

enum {
	LCD_REG_COUNT = 0x30,
	LCD_FB_STRIDE = 128,
	LCD_FB_PAGES = FX_EMU_LCD_PAGE_COUNT,
	LCD_VISIBLE_WIDTH = FX_EMU_LCD_VISIBLE_WIDTH,
	LCD_VISIBLE_HEIGHT = FX_EMU_LCD_VISIBLE_HEIGHT,
	LCD_FB_SIZE = LCD_FB_STRIDE * LCD_FB_PAGES
};

struct mmio_state;

struct lcd_state {
	uint8_t fb[LCD_FB_SIZE];
	uint8_t reg[LCD_REG_COUNT]; /* many are not used */
	struct mmio_state *mmio;
};

void lcd_connect_mmio_state(struct lcd_state *state, struct mmio_state *mmio);
uint8_t lcd_read_byte_state(struct lcd_state *state, uint8_t addr);
void lcd_write_byte_state(struct lcd_state *state, uint8_t addr, uint8_t byte);
void lcd_process_postid_state(struct lcd_state *state);
uint8_t lcd_ram_read_byte_state(const struct lcd_state *state, uint16_t addr);
void lcd_reset_state(struct lcd_state *state);

#endif /* FX_EMU_CORE_LCD_INTERNAL_H */


