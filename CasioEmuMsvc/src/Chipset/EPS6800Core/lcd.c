/* ePS6800 LCD */

#include "eps6800.h"
#include "lcd_internal.h"
#include "mmio_internal.h"

#include <stdio.h>
#include <string.h>

enum {
	LCD_ADDRH_SHIFT = 7,
	LCD_ADDRL_MIN = 0x00,
	LCD_ADDRL_MAX = 0x61,
	LCD_INVALID_READ_VALUE = 0xff
};

static const struct eps_lcd_profile *lcd_profile(enum eps_variant variant) {
	return &eps_get_variant_traits(variant)->lcd;
}

enum {
	LCD_W192_PORTD_WRITE = 0x01,
	LCD_W192_PORTD_SELECT = 0x10,
	LCD_W192_PORTD_DATA = 0x40,
	LCD_W192_PORTD_ENABLE = 0x80
};

static uint8_t lcd_w192_read_valid;
static uint8_t lcd_w192_bus_phase;

static size_t lcd_w192_address(const struct lcd_state *state) {
	return (size_t)(state->w192_page & 7u) * LCD_W192_WIDTH +
		(state->w192_column % LCD_W192_WIDTH);
}

static void lcd_w192_advance_column(struct lcd_state *state) {
	if (state->w192_column + 1u < LCD_W192_WIDTH) {
		state->w192_column++;
	}
	else {
		/* IQ-V9 clears and uploads the complete 192x64 RAM as one
		 * 1536-byte stream after selecting page 0.  The controller wraps
		 * column 191 to column 0 of the following page. */
		state->w192_column = 0;
		state->w192_page = (uint8_t)((state->w192_page + 1u) & 7u);
	}
}

static void lcd_w192_command(struct lcd_state *state, uint8_t byte) {
	if (state->w192_contrast_pending) {
		state->w192_contrast = (uint8_t)((byte & 0x3fu) >> 2);
		state->w192_contrast_pending = 0;
	}
	else if ((byte & 0xf8u) == 0xb0u) {
		state->w192_page = (uint8_t)(byte & 7u);
		lcd_w192_read_valid = 0;
	}
	else if ((byte & 0xf0u) == 0x10u) {
		state->w192_column = (uint8_t)(((byte & 0x0fu) << 4) |
			(state->w192_column & 0x0fu));
		lcd_w192_read_valid = 0;
	}
	else if ((byte & 0xf0u) == 0x00u) {
		state->w192_column = (uint8_t)((state->w192_column & 0xf0u) | (byte & 0x0fu));
		lcd_w192_read_valid = 0;
	}
	else if (byte == 0xaeu || byte == 0xafu) {
		state->w192_display_on = (uint8_t)(byte == 0xafu);
	}
	else if (byte == 0xa4u || byte == 0xa5u) {
		state->w192_all_pixels_on = (uint8_t)(byte == 0xa5u);
	}
	else if (byte == 0xa0u || byte == 0xa1u) {
		state->w192_segment_reverse = (uint8_t)(byte == 0xa1u);
	}
	else if (byte == 0xc0u || byte == 0xc8u) {
		state->w192_com_reverse = (uint8_t)(byte == 0xc8u);
	}
	else if (byte == 0x81u) {
		state->w192_contrast_pending = 1u;
	}
	else if (byte == 0xe0u) {
		state->w192_rmw_column = state->w192_column;
		state->w192_rmw_active = 1u;
		lcd_w192_read_valid = 0;
	}
	else if (byte == 0xeeu && state->w192_rmw_active) {
		state->w192_column = state->w192_rmw_column;
		state->w192_rmw_active = 0u;
		lcd_w192_read_valid = 0;
	}
}

uint8_t lcd_gpio_read_byte_state(struct lcd_state *state, uint8_t addr) {
	if (addr == REG_PORTD)
		return state->w192_portd;
	if (addr == REG_DCRDE)
		return state->w192_dcrde;
	return state->w192_porte;
}

void lcd_gpio_write_byte_state(struct lcd_state *state, uint8_t addr, uint8_t byte) {
	if (addr == REG_PORTE) {
		state->w192_porte = byte;
		return;
	}
	if (addr == REG_DCRDE) {
		state->w192_dcrde = byte;
		return;
	}
	if (addr == REG_PORTD) {
		state->w192_portd = byte;
		if (lcd_w192_bus_phase == 0 || lcd_w192_bus_phase == 1) {
			if (byte & LCD_W192_PORTD_SELECT) {
				lcd_w192_bus_phase = 0;
			}
			else if (byte & LCD_W192_PORTD_DATA) {
				if (byte & LCD_W192_PORTD_WRITE) {
					if (!(byte & LCD_W192_PORTD_ENABLE))
						lcd_w192_bus_phase = 32;
				}
				else if (byte & LCD_W192_PORTD_ENABLE) {
					if (lcd_w192_read_valid) {
						state->w192_porte = state->w192_fb[lcd_w192_address(state)];
						if (!state->w192_rmw_active)
							lcd_w192_advance_column(state);
					}
					lcd_w192_bus_phase = 33;
				}
			}
			else if (byte & LCD_W192_PORTD_WRITE) {
				if (!(byte & LCD_W192_PORTD_ENABLE))
					lcd_w192_bus_phase = 16;
			}
			else if (byte & LCD_W192_PORTD_ENABLE) {
				/* IQV9.exe CIce::RunLCD (sub_410020) drives Port E bit 4
				 * low while servicing a command/status read: not busy. */
				state->w192_porte &= (uint8_t)~0x10u;
				lcd_w192_bus_phase = 17;
			}
		}
		else if (lcd_w192_bus_phase == 16) {
			if (byte & LCD_W192_PORTD_ENABLE) {
				lcd_w192_command(state, state->w192_porte);
				lcd_w192_bus_phase = 1;
			}
		}
		else if (lcd_w192_bus_phase == 17) {
			/* IQV9.exe CIce::RunLCD (sub_410020, state 17) completes
			 * a command/status read cycle when WR returns high. */
			if (byte & LCD_W192_PORTD_WRITE)
				lcd_w192_bus_phase = 1;
		}
		else if (lcd_w192_bus_phase == 32) {
			if (byte & LCD_W192_PORTD_SELECT) {
				lcd_w192_bus_phase = 1;
			}
			else if ((byte & (LCD_W192_PORTD_DATA | LCD_W192_PORTD_WRITE |
				LCD_W192_PORTD_ENABLE)) == (LCD_W192_PORTD_DATA |
				LCD_W192_PORTD_WRITE | LCD_W192_PORTD_ENABLE)) {
				state->w192_fb[lcd_w192_address(state)] = state->w192_porte;
				lcd_w192_advance_column(state);
				lcd_w192_bus_phase = 1;
			}
		}
		else if (lcd_w192_bus_phase == 33) {
			if (byte & LCD_W192_PORTD_WRITE) {
				lcd_w192_read_valid = 1;
				lcd_w192_bus_phase = 1;
			}
		}
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
	const struct eps_lcd_profile *profile = lcd_profile(variant);
	const uint16_t raw_size = (uint16_t)eps_lcd_raw_size(variant);
	const uint8_t low = state->reg[eps_reg_lcdarl(variant)];

	switch (profile->address_model) {
	case EPS_LCD_ADDRESS_LINEAR_WRAP:
		return raw_size != 0 ? (uint16_t)(low % raw_size) : 0;
	case EPS_LCD_ADDRESS_ROW_MAJOR:
		/* Official mode-4 sub_420F00/sub_422870 addresses LCDDAT as
		 * LCDARL + 98 * (LCDARH & 3), directly in the 392-byte store. */
		if (low >= profile->row_width)
			return raw_size;
		return (uint16_t)(low +
			profile->row_width * (state->reg[eps_reg_lcdarh(variant)] & MASK_LCD_ADDRESS_HIGH));
	case EPS_LCD_ADDRESS_PAGED_128:
	default:
		return (uint16_t)(((uint16_t)(state->reg[eps_reg_lcdarh(variant)] & MASK_LCD_ADDRESS_HIGH) << LCD_ADDRH_SHIFT) |
			low);
	}
}

static bool lcd_data_address_valid(const struct lcd_state *state, uint16_t addr) {
	const enum eps_variant variant = state->mmio->variant;
	const struct eps_lcd_profile *profile = lcd_profile(variant);

	/* EPS6800 exposes 96 visible bytes per page to the host, but LCDDAT is
	 * addressed through four 128-byte hardware pages.  The fourth page starts
	 * at 0x180, which is already beyond the 384-byte host-visible image. */
	if (profile->address_model == EPS_LCD_ADDRESS_PAGED_128)
		return addr < LCD_FB_SIZE;
	return addr < eps_lcd_raw_size(variant);
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
			return lcd_data_address_valid(state, data_addr) ? state->fb[data_addr] : 0;
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
			if (lcd_data_address_valid(state, data_addr))
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
	const struct eps_lcd_profile *profile = lcd_profile(variant);
	const size_t raw_size = eps_lcd_raw_size(variant);
	const size_t copy_size = size < raw_size ? size : raw_size;

	if (eps_variant_is_6800_w192(variant)) {
		/* IQ-V9's glass wiring already accounts for the A1/C8 controller
		 * setup.  The official emulator consumes page RAM as-is; applying
		 * SEG/COM remapping here rotates the logical display a second time. */
		for (i = 0; i < copy_size; ++i)
			data[i] = state->w192_all_pixels_on ? 0xffu : state->w192_fb[i];
		{
			size_t nonzero = 0;
			for (i = 0; i < copy_size; ++i)
				nonzero += state->w192_fb[i] != 0;
			if (nonzero > 300) {
				FILE *fp = fopen("w192-fb.bin", "wb");
				if (fp) {
					fwrite(state->w192_fb, 1, copy_size, fp);
					fclose(fp);
				}
			}
		}
	}
	else {
		for (i = 0; i < copy_size; ++i)
			data[i] = lcd_ram_read_byte_state(state, (uint16_t)i);
	}
	if (lcdarh)
		*lcdarh = profile->has_address_high ? state->reg[eps_reg_lcdarh(variant)] : 0;
	if (lcdcon)
		*lcdcon = eps_variant_is_6800_w192(variant) ?
			(state->w192_display_on ? BIT_LCD_ON : 0u) : state->reg[eps_reg_lcdcon(variant)];
	if (contrast) {
		*contrast = eps_variant_is_6800_w192(variant) ? state->w192_contrast : profile->has_address_high ?
			(uint8_t)((state->reg[eps_reg_lcdarh(variant)] & MASK_LCD_CONTRAST) >> SHIFT_LCD_CONTRAST) :
			profile->fixed_contrast;
	}
	return copy_size;
}

uint8_t lcd_raw_read_byte_state(const struct lcd_state *state, size_t addr) {
	if (!state || addr >= eps_lcd_raw_size(state->mmio->variant))
		return LCD_INVALID_READ_VALUE;
	return eps_variant_is_6800_w192(state->mmio->variant) ? state->w192_fb[addr] : state->fb[addr];
}

bool lcd_raw_write_byte_state(struct lcd_state *state, size_t addr, uint8_t value) {
	if (!state || addr >= eps_lcd_raw_size(state->mmio->variant))
		return false;
	if (eps_variant_is_6800_w192(state->mmio->variant))
		state->w192_fb[addr] = value;
	else
		state->fb[addr] = value;
	return true;
}

void lcd_reset_state(struct lcd_state *state) {
	memset(state->fb, 0, sizeof(state->fb));
	memset(state->w192_fb, 0, sizeof(state->w192_fb));
	memset(state->reg, 0, sizeof(state->reg));
	state->w192_page = 0;
	state->w192_column = 0;
	state->w192_rmw_column = 0;
	state->w192_portd = 0xffu;
	state->w192_porte = 0xffu;
	state->w192_dcrde = 0x33u;
	state->w192_display_on = 0;
	state->w192_all_pixels_on = 0;
	state->w192_rmw_active = 0;
	state->w192_segment_reverse = 0;
	state->w192_com_reverse = 0;
	state->w192_contrast = 7u;
	state->w192_contrast_pending = 0;
	lcd_w192_read_valid = 0;
	lcd_w192_bus_phase = 0;
}

