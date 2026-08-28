/* ePS6800 common hardware definitions */
#ifndef FX_EMU_CORE_EPS6800_H
#define FX_EMU_CORE_EPS6800_H

#include "mmio_layout.h"

#include <stddef.h>
#include <stdint.h>

#define FOSC 32768
#define FHOSC 500000

enum eps_variant {
	EPS_VARIANT_6800 = 0,
	EPS_VARIANT_6009 = 1,
	/* The EL-W531TL official simulator selects CIce core mode 4, the same
	 * CPU/SFR family as ePS6800, with a 0x18000-word ROM and 98x4 LCD RAM. */
	EPS_VARIANT_9500 = 2
};

enum {
	WAKE_TIMER = 0x00,
	WAKE_PAINT = 0x01,
	WAKE_ON = 0x02
};

enum {
	REG_INDF0 = 0x00,
	REG_FSR0 = 0x01,
	REG_BSR = 0x02,
	REG_INDF1 = 0x03,
	REG_FSR1 = 0x04,
	REG_BSR1 = 0x05,
	REG_STKPTR = 0x06,
	REG_PCL = 0x07,
	REG_PCM = 0x08,
	REG_PCH = 0x09,
	REG_ACC = 0x0A,
	REG_TABPTRL = 0x0B,
	REG_TABPTRM = 0x0C,
	REG_TABPTRH = 0x0D,
	REG_LCDDAT = 0x0E,
	REG_STATUS = 0x0F,
	REG_INDF2 = 0x10,
	REG_FSR2 = 0x11,
	REG_BSR2 = 0x12,
	REG_CPUCON = 0x20,
	REG_POSTID = 0x21,
	REG_LCDARL = 0x22,
	REG_LCDARH = 0x23,
	REG_INTSTA = 0x24,
	REG_TR0CON = 0x25,
	REG_TRL0L = 0x26,
	REG_TRL0H = 0x27,
	REG_T0CL = 0x28,
	REG_T0CH = 0x29,
	REG_TR1CON = 0x2A,
	REG_TRL1 = 0x2B,
	REG_TR2WCON = 0x2C,
	REG_TRL2 = 0x2D,
	REG_LCDCON = 0x2E,
	REG_STBCON = 0x30,
	REG_PORTA = 0x31,
	REG_PACON = 0x32,
	REG_DCRA = 0x33,
	REG_PAWAKE = 0x34,
	REG_PAINTEN = 0x35,
	REG_PAINTSTA = 0x36,
	REG_PORTB = 0x37,
	REG_PBCON = 0x38,
	REG_DCRB = 0x39,
	REG_PORTC = 0x3A,
	REG_PCCON = 0x3B,
	REG_DCRC = 0x3C,
	REG_PORTD = 0x3D,
	REG_PORTE = 0x3E,
	REG_DCRDE = 0x3F
};

/* POSTID */
enum {
	BIT_FSR0PE = 0x01,
	BIT_FSR1PE = 0x02,
	BIT_LCDPE = 0x04,
	BIT_FSR2PE = 0x08,
	BIT_FSR0ID = 0x10,
	BIT_FSR1ID = 0x20,
	BIT_LCDID = 0x40,
	BIT_FSR2ID = 0x80
};

/* LCDARH */
enum {
	MASK_LCD_CONTRAST = 0xf0,
	SHIFT_LCD_CONTRAST = 4,
	MASK_LCD_ADDRESS_HIGH = 0x03
};

/* LCDCON */
enum {
	BIT_LCD_R1EN = 0x80,
	BIT_LCD_BLANK = 0x40,
	BIT_LCD_ON = 0x20,
	MASK_LCD_FRAME_RATE = 0x1c,
	MASK_LCD_CHARGE_PUMP = 0x03
};

/* CPUCON */
enum {
	BIT_WBK = 0x80,
	BIT_GLINT = 0x04,
	BIT_MS1 = 0x02,
	BIT_MS0 = 0x01
};

enum {
	/* EPS6800 supports up to 96K 16-bit ROM words (192 KiB).  Table reads
	 * address bytes, so TABPTR needs two high bits to reach 0x20000..0x2ffff. */
	MASK_TABPTRH = 0x03
};

/* STBCON */
enum {
	BIT_KEY_INPUT_ENABLE = 0x80,
	BIT_AUTO_KEY_SCAN = 0x40,
	BIT_STROBE_ENABLE = 0x20,
	BIT_ALL_STROBES = 0x10
};

/* STATUS */
enum {
	BIT_STATUS_Z = 0x04,
	BIT_STATUS_C = 0x01,
	BIT_STATUS_DC = 0x02,
	BIT_STATUS_OV = 0x08,
	BIT_STATUS_SLE = 0x10,
	BIT_STATUS_SGE = 0x20,
	BIT_STATUS_PD = 0x40,
	BIT_STATUS_TO = 0x80
};

/* TR0CON */
enum {
	BIT_T0ENMD = 0x20,
	BIT_TMR0IE = 0x10,
	BIT_T0EN = 0x08,
	BIT_T0CS = 0x04,
	BIT_T0PSR1 = 0x02,
	BIT_T0PSR0 = 0x01
};

/* TR1CON */
enum {
	BIT_T1WKEN = 0x80,
	BIT_TMR1IE = 0x10,
	BIT_T1EN = 0x08,
	BIT_T1PSR1 = 0x02,
	BIT_T1PSR0 = 0x01
};

/* TR2WCON */
enum {
	BIT_WDTEN = 0x80,
	BIT_WDTPSR1 = 0x40,
	BIT_WDTPSR0 = 0x20,
	BIT_TMR2IE = 0x10,
	BIT_T2EN = 0x08,
	BIT_T2CS = 0x04,
	BIT_T2PSR1 = 0x02,
	BIT_T2PSR0 = 0x01
};

/* INTSTA */
enum {
	BIT_TMR2I = 0x04,
	BIT_TMR1I = 0x02,
	BIT_TMR0I = 0x01
};

enum {
	INT_LEVEL1_PAINT = 0x01,
	INT_LEVEL2_RSVD = 0x02,
	INT_LEVEL3_RSVD = 0x04,
	INT_LEVEL4_TIMINT = 0x08,
	INT_LEVEL5_RSVD = 0x10
};

enum {
	ADDR_PAINT = 0x00000002,
	ADDR_TIMINT = 0x00000008
};

enum eps_stack_model {
	EPS_STACK_MODEL_LINEAR = 0,
	EPS_STACK_MODEL_DESCENDING_EVEN = 1
};

/*
 * Static silicon description.  Keep register locations and structural
 * capabilities here so adding another EPS family member does not require
 * growing variant conditionals throughout the core.
 */
struct eps_variant_traits {
	uint8_t reg_cpucon;
	uint8_t reg_postid;
	uint8_t reg_lcdarl;
	uint8_t reg_lcddat;
	uint8_t reg_lcdarh;
	uint8_t reg_lcdcon;
	uint8_t reg_trintsta;
	uint8_t reg_trintcon;
	uint8_t reg_tr0con;
	uint8_t reg_tr1con;
	uint8_t reg_trl0l;
	uint8_t reg_trl0h;
	uint8_t reg_trl1;
	uint8_t reg_tr2wcon;
	uint8_t reg_trl2;
	uint8_t reg_porta;
	uint8_t reg_portb;
	uint8_t reg_stbcon;
	uint8_t reg_pacon;
	uint8_t reg_pawake;
	uint8_t reg_painten;
	uint8_t reg_paintsta;
	uint8_t reg_pbcon;
	uint8_t reg_dcrb;
	uint8_t ram_page_mask;
	uint8_t has_pch;
	uint8_t has_indf2;
	uint8_t has_wbk;
	uint8_t stack_model;
	size_t bank_ram_size;
	size_t lcd_raw_size;
};

static inline int eps_variant_is_6009(enum eps_variant variant) {
	return variant == EPS_VARIANT_6009;
}

static inline int eps_variant_is_9500(enum eps_variant variant) {
	return variant == EPS_VARIANT_9500;
}

static inline const struct eps_variant_traits *eps_get_variant_traits(enum eps_variant variant) {
	static const struct eps_variant_traits eps6800_traits = {
		REG_CPUCON, REG_POSTID, REG_LCDARL, REG_LCDDAT, REG_LCDARH, REG_LCDCON,
		REG_INTSTA, REG_INTSTA, REG_TR0CON, REG_TR1CON, REG_TRL0L, REG_TRL0H,
		REG_TRL1, REG_TR2WCON, REG_TRL2, REG_PORTA, REG_PORTB, REG_STBCON,
		REG_PACON, REG_PAWAKE, REG_PAINTEN, REG_PAINTSTA, REG_PBCON, REG_DCRB,
		0x3fu, 1u, 1u, 1u, EPS_STACK_MODEL_LINEAR,
		MMIO_LEGACY_RAM_COUNT, (size_t)(96u * 4u)
	};
	static const struct eps_variant_traits eps6009_traits = {
		0x31u, 0x30u, 0x09u, REG_LCDDAT, 0xffu, 0x2fu,
		0x22u, 0x21u, 0x23u, 0x23u, 0x24u, 0x25u,
		0x26u, 0x27u, 0x28u, 0x10u, 0x11u, 0x20u,
		0x29u, 0x2au, 0x2bu, 0x2cu, 0x2du, 0x2eu,
		0x0fu, 0u, 0u, 0u, EPS_STACK_MODEL_LINEAR,
		MMIO_LEGACY_RAM_COUNT, 0x88u
	};
	static const struct eps_variant_traits eps9500_traits = {
		REG_CPUCON, REG_POSTID, REG_LCDARL, REG_LCDDAT, REG_LCDARH, REG_LCDCON,
		REG_INTSTA, REG_INTSTA, REG_TR0CON, REG_TR1CON, REG_TRL0L, REG_TRL0H,
		REG_TRL1, REG_TR2WCON, REG_TRL2, REG_PORTA, REG_PORTB, REG_STBCON,
		REG_PACON, REG_PAWAKE, REG_PAINTEN, REG_PAINTSTA, REG_PBCON, REG_DCRB,
		0x3fu, 1u, 1u, 1u, EPS_STACK_MODEL_DESCENDING_EVEN,
		MMIO_EPS9500_RAM_COUNT, (size_t)(98u * 4u)
	};

	switch (variant) {
	case EPS_VARIANT_6009:
		return &eps6009_traits;
	case EPS_VARIANT_9500:
		return &eps9500_traits;
	case EPS_VARIANT_6800:
	default:
		return &eps6800_traits;
	}
}

static inline uint8_t eps_reg_cpucon(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_cpucon;
}

static inline uint8_t eps_reg_postid(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_postid;
}

static inline uint8_t eps_reg_lcdarl(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_lcdarl;
}

static inline uint8_t eps_reg_lcddat(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_lcddat;
}

static inline uint8_t eps_reg_lcdarh(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_lcdarh;
}

static inline uint8_t eps_reg_lcdcon(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_lcdcon;
}

static inline uint8_t eps_reg_trintsta(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_trintsta;
}

static inline uint8_t eps_reg_trintcon(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_trintcon;
}

static inline uint8_t eps_reg_tr0con(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_tr0con;
}

static inline uint8_t eps_reg_tr1con(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_tr1con;
}

static inline uint8_t eps_reg_trl0l(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_trl0l;
}

static inline uint8_t eps_reg_trl0h(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_trl0h;
}

static inline uint8_t eps_reg_trl1(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_trl1;
}

static inline uint8_t eps_reg_tr2wcon(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_tr2wcon;
}

static inline uint8_t eps_reg_trl2(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_trl2;
}

static inline uint8_t eps_reg_porta(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_porta;
}

static inline uint8_t eps_reg_portb(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_portb;
}

static inline uint8_t eps_reg_stbcon(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_stbcon;
}

static inline uint8_t eps_reg_pacon(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_pacon;
}

static inline uint8_t eps_reg_pawake(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_pawake;
}

static inline uint8_t eps_reg_painten(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_painten;
}

static inline uint8_t eps_reg_paintsta(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_paintsta;
}

static inline uint8_t eps_reg_pbcon(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_pbcon;
}

static inline uint8_t eps_reg_dcrb(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->reg_dcrb;
}

static inline int eps_key_input_enabled(enum eps_variant variant, const uint8_t *regs) {
	return eps_variant_is_6009(variant) ?
		(((regs[eps_reg_stbcon(variant)] & BIT_AUTO_KEY_SCAN) != 0) ||
			((regs[eps_reg_pacon(variant)] & 0x01u) != 0)) :
		((regs[eps_reg_stbcon(variant)] & BIT_KEY_INPUT_ENABLE) != 0);
}

static inline int eps_has_pch(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->has_pch != 0;
}

static inline int eps_has_indf2(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->has_indf2 != 0;
}

static inline int eps_has_wbk(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->has_wbk != 0;
}

static inline uint8_t eps_ram_page_mask(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->ram_page_mask;
}

static inline size_t eps_bank_ram_size(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->bank_ram_size;
}

static inline size_t eps_lcd_raw_size(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->lcd_raw_size;
}

static inline int eps_stack_is_descending_even(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->stack_model == EPS_STACK_MODEL_DESCENDING_EVEN;
}

static inline uint8_t eps_stack_depth_from_raw(enum eps_variant variant, uint8_t raw_stack_pointer) {
	if (eps_stack_is_descending_even(variant))
		return (uint8_t)(0u - raw_stack_pointer) / 2u;
	return raw_stack_pointer & 0x1fu;
}

#endif /* FX_EMU_CORE_EPS6800_H */
