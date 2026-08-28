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

enum lcd_address_model {
	LCD_ADDRESS_PAGED_128 = 0,
	LCD_ADDRESS_LINEAR_WRAP,
	LCD_ADDRESS_ROW_MAJOR
};

struct lcd_variant_profile {
	enum lcd_address_model address_model;
	uint8_t address_low_max;
	uint8_t row_width;
	uint8_t fixed_contrast;
	bool host_linear;
	bool has_address_high;
};

static const struct lcd_variant_profile *lcd_profile(enum eps_variant variant) {
	static const struct lcd_variant_profile eps6800_profile = {
		LCD_ADDRESS_PAGED_128, LCD_ADDRL_MAX, 0u, 0u, false, true
	};
	static const struct lcd_variant_profile eps6009_profile = {
		LCD_ADDRESS_LINEAR_WRAP, 0x87u, 0u, 0x0fu, true, false
	};
	static const struct lcd_variant_profile eps9500_profile = {
		LCD_ADDRESS_ROW_MAJOR, LCD_ADDRL_MAX, 98u, 0u, true, true
	};

	switch (variant) {
	case EPS_VARIANT_6009:
		return &eps6009_profile;
	case EPS_VARIANT_9500:
		return &eps9500_profile;
	case EPS_VARIANT_6800:
	default:
		return &eps6800_profile;
	}
}

void lcd_connect_mmio_state(struct lcd_state *state, struct mmio_state *mmio) {
	state->mmio = mmio;
}

static void lcd_bus_write_internal(struct lcd_state *state, uint8_t addr, uint8_t byte) {
	mmio_write_byte_internal_state(state->mmio, addr, byte);
}

static uint16_t lcd_data_address(const struct lcd_state *state) {
	const enum eps_variant variant = state->mmio->variant;
	const struct lcd_variant_profile *profile = lcd_profile(variant);
	const uint16_t raw_size = (uint16_t)eps_lcd_raw_size(variant);
	const uint8_t low = state->reg[eps_reg_lcdarl(variant)];

	switch (profile->address_model) {
	case LCD_ADDRESS_LINEAR_WRAP:
		return raw_size != 0 ? (uint16_t)(low % raw_size) : 0;
	case LCD_ADDRESS_ROW_MAJOR:
		/* Official mode-4 sub_420F00/sub_422870 addresses LCDDAT as
		 * LCDARL + 98 * (LCDARH & 3), directly in the 392-byte store. */
		if (low >= profile->row_width)
			return raw_size;
		return (uint16_t)(low +
			profile->row_width * (state->reg[eps_reg_lcdarh(variant)] & MASK_LCD_ADDRESS_HIGH));
	case LCD_ADDRESS_PAGED_128:
	default:
		return (uint16_t)(((uint16_t)(state->reg[eps_reg_lcdarh(variant)] & MASK_LCD_ADDRESS_HIGH) << LCD_ADDRH_SHIFT) |
			low);
	}
}

static uint16_t lcd_visible_address(uint16_t addr) {
	return (uint16_t)((addr / LCD_VISIBLE_WIDTH) * LCD_FB_STRIDE + (addr % LCD_VISIBLE_WIDTH));
}

static void lcd_increment_address(struct lcd_state *state) {
	const enum eps_variant variant = state->mmio->variant;
	const uint8_t reg_lcdarl = eps_reg_lcdarl(variant);
	const uint8_t max_addr = lcd_profile(variant)->address_low_max;
	if (state->reg[reg_lcdarl] < max_addr) {
		state->reg[reg_lcdarl]++;
	}
	else {
		state->reg[reg_lcdarl] = LCD_ADDRL_MIN;
	}
}

static void lcd_decrement_address(struct lcd_state *state) {
	const enum eps_variant variant = state->mmio->variant;
	const uint8_t reg_lcdarl = eps_reg_lcdarl(variant);
	const uint8_t max_addr = lcd_profile(variant)->address_low_max;
	if (state->reg[reg_lcdarl] > LCD_ADDRL_MIN) {
		state->reg[reg_lcdarl]--;
	}
	else {
		state->reg[reg_lcdarl] = max_addr;
	}
}

uint8_t lcd_read_byte_state(struct lcd_state *state, uint8_t addr) {
	uint8_t byte;
	if (addr < LCD_REG_COUNT) {
		if (addr == eps_reg_lcddat(state->mmio->variant)) {
			const uint16_t data_addr = lcd_data_address(state);
			return data_addr < eps_lcd_raw_size(state->mmio->variant) ? state->fb[data_addr] : 0;
		}
		byte = state->reg[addr];
	}
	else {
		byte = LCD_INVALID_READ_VALUE;
		mmio_bad_read_byte_state(state->mmio, addr);
	}
	return byte;
}

void lcd_process_postid_state(struct lcd_state *state) {
	const uint8_t reg_postid = eps_reg_postid(state->mmio->variant);
	if (state->reg[reg_postid] & BIT_LCDPE) {
		if (state->reg[reg_postid] & BIT_LCDID) {
			lcd_increment_address(state);
		}
		else {
			lcd_decrement_address(state);
		}
		lcd_bus_write_internal(state, eps_reg_lcdarl(state->mmio->variant),
			state->reg[eps_reg_lcdarl(state->mmio->variant)]);
	}
}

void lcd_write_byte_state(struct lcd_state *state, uint8_t addr, uint8_t byte) {
	if (addr < LCD_REG_COUNT) {
		state->reg[addr] = byte;
		/* Keep the debugger's flat SFR view synchronized with the peripheral. */
		lcd_bus_write_internal(state, addr, byte);
		if (addr == eps_reg_lcddat(state->mmio->variant)) {
			const uint16_t data_addr = lcd_data_address(state);
			if (data_addr < eps_lcd_raw_size(state->mmio->variant))
				state->fb[data_addr] = byte;
		}
	}
	else {
		mmio_bad_write_byte_state(state->mmio, addr);
	}
}

uint8_t lcd_ram_read_byte_state(const struct lcd_state *state, uint16_t addr) {
	const enum eps_variant variant = state->mmio->variant;
	if (lcd_profile(variant)->host_linear)
		return addr < eps_lcd_raw_size(variant) ? state->fb[addr] : LCD_INVALID_READ_VALUE;
	return state->fb[lcd_visible_address(addr)];
}

size_t lcd_copy_display_state(
	const struct lcd_state *state,
	uint8_t *data,
	size_t size,
	uint8_t *lcdarh,
	uint8_t *lcdcon,
	uint8_t *contrast
) {
	size_t i;
	const enum eps_variant variant = state->mmio->variant;
	const struct lcd_variant_profile *profile = lcd_profile(variant);
	const size_t raw_size = eps_lcd_raw_size(variant);
	const size_t copy_size = size < raw_size ? size : raw_size;

	for (i = 0; i < copy_size; ++i)
		data[i] = lcd_ram_read_byte_state(state, (uint16_t)i);
	if (lcdarh)
		*lcdarh = profile->has_address_high ? state->reg[eps_reg_lcdarh(variant)] : 0;
	if (lcdcon)
		*lcdcon = state->reg[eps_reg_lcdcon(variant)];
	if (contrast) {
		*contrast = profile->has_address_high ?
			(uint8_t)((state->reg[eps_reg_lcdarh(variant)] & MASK_LCD_CONTRAST) >> SHIFT_LCD_CONTRAST) :
			profile->fixed_contrast;
	}
	return copy_size;
}

uint8_t lcd_raw_read_byte_state(const struct lcd_state *state, size_t addr) {
	if (!state || addr >= eps_lcd_raw_size(state->mmio->variant))
		return LCD_INVALID_READ_VALUE;
	return state->fb[addr];
}

bool lcd_raw_write_byte_state(struct lcd_state *state, size_t addr, uint8_t value) {
	if (!state || addr >= eps_lcd_raw_size(state->mmio->variant))
		return false;
	state->fb[addr] = value;
	return true;
}

void lcd_reset_state(struct lcd_state *state) {
	memset(state->reg, 0, sizeof(state->reg));
}

