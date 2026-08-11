/* ePS6800 LCD */

#include "eps6800.h"
#include "lcd_internal.h"
#include "mmio_internal.h"

#include <string.h>

enum {
	LCD_ADDRH_SHIFT = 7,
	LCD_ADDRL_MIN = 0x00,
	LCD_ADDRL_MAX = 0x61,
	LCD_INVALID_READ_VALUE = 0xff
};

void lcd_connect_mmio_state(struct lcd_state *state, struct mmio_state *mmio) {
	state->mmio = mmio;
}

static void lcd_bus_write_internal(struct lcd_state *state, uint8_t addr, uint8_t byte) {
	mmio_write_byte_internal_state(state->mmio, addr, byte);
}

static uint16_t lcd_data_address(const struct lcd_state *state) {
	return (uint16_t)(((uint16_t)(state->reg[REG_LCDARH] & MASK_LCD_ADDRESS_HIGH) << LCD_ADDRH_SHIFT) |
		state->reg[REG_LCDARL]);
}

static uint16_t lcd_visible_address(uint16_t addr) {
	return (uint16_t)((addr / LCD_VISIBLE_WIDTH) * LCD_FB_STRIDE + (addr % LCD_VISIBLE_WIDTH));
}

static void lcd_increment_address(struct lcd_state *state) {
	if (state->reg[REG_LCDARL] < LCD_ADDRL_MAX) {
		state->reg[REG_LCDARL]++;
	}
	else {
		state->reg[REG_LCDARL] = LCD_ADDRL_MIN;
	}
}

static void lcd_decrement_address(struct lcd_state *state) {
	if (state->reg[REG_LCDARL] > LCD_ADDRL_MIN) {
		state->reg[REG_LCDARL]--;
	}
	else {
		state->reg[REG_LCDARL] = LCD_ADDRL_MAX;
	}
}

uint8_t lcd_read_byte_state(struct lcd_state *state, uint8_t addr) {
	uint8_t byte;
	if (addr < LCD_REG_COUNT) {
		switch (addr) {
		case REG_LCDDAT:
			byte = state->fb[lcd_data_address(state)];
			break;
		default:
			byte = state->reg[addr];
			break;
		}
	}
	else {
		byte = LCD_INVALID_READ_VALUE;
		mmio_bad_read_byte_state(state->mmio, addr);
	}
	return byte;
}

void lcd_process_postid_state(struct lcd_state *state) {
	if (state->reg[REG_POSTID] & BIT_LCDPE) {
		if (state->reg[REG_POSTID] & BIT_LCDID) {
			lcd_increment_address(state);
		}
		else {
			lcd_decrement_address(state);
		}
		lcd_bus_write_internal(state, REG_LCDARL, state->reg[REG_LCDARL]);
	}
}

void lcd_write_byte_state(struct lcd_state *state, uint8_t addr, uint8_t byte) {
	if (addr < LCD_REG_COUNT) {
		state->reg[addr] = byte;
		/* Keep the debugger's flat SFR view synchronized with the peripheral. */
		lcd_bus_write_internal(state, addr, byte);
		switch (addr) {
		case REG_LCDDAT:
			state->fb[lcd_data_address(state)] = byte;
			break;
		}
	}
	else {
		mmio_bad_write_byte_state(state->mmio, addr);
	}
}

uint8_t lcd_ram_read_byte_state(const struct lcd_state *state, uint16_t addr) {
	return state->fb[lcd_visible_address(addr)];
}

void lcd_reset_state(struct lcd_state *state) {
	memset(state->reg, 0, sizeof(state->reg));
}

