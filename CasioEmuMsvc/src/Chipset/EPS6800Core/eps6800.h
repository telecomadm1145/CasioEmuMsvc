/* ePS6800 common hardware definitions */
#ifndef FX_EMU_CORE_EPS6800_H
#define FX_EMU_CORE_EPS6800_H

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

static inline int eps_variant_is_6009(enum eps_variant variant) {
	return variant == EPS_VARIANT_6009;
}

static inline int eps_variant_is_9500(enum eps_variant variant) {
	return variant == EPS_VARIANT_9500;
}

static inline uint8_t eps_reg_cpucon(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x31u : REG_CPUCON;
}

static inline uint8_t eps_reg_postid(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x30u : REG_POSTID;
}

static inline uint8_t eps_reg_lcdarl(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x09u : REG_LCDARL;
}

static inline uint8_t eps_reg_lcddat(enum eps_variant variant) {
	(void)variant;
	return REG_LCDDAT;
}

static inline uint8_t eps_reg_lcdarh(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0xffu : REG_LCDARH;
}

static inline uint8_t eps_reg_lcdcon(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x2fu : REG_LCDCON;
}

static inline uint8_t eps_reg_trintsta(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x22u : REG_INTSTA;
}

static inline uint8_t eps_reg_trintcon(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x21u : REG_INTSTA;
}

static inline uint8_t eps_reg_tr0con(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x23u : REG_TR0CON;
}

static inline uint8_t eps_reg_tr1con(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x23u : REG_TR1CON;
}

static inline uint8_t eps_reg_trl0l(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x24u : REG_TRL0L;
}

static inline uint8_t eps_reg_trl0h(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x25u : REG_TRL0H;
}

static inline uint8_t eps_reg_trl1(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x26u : REG_TRL1;
}

static inline uint8_t eps_reg_tr2wcon(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x27u : REG_TR2WCON;
}

static inline uint8_t eps_reg_trl2(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x28u : REG_TRL2;
}

static inline uint8_t eps_reg_porta(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x10u : REG_PORTA;
}

static inline uint8_t eps_reg_portb(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x11u : REG_PORTB;
}

static inline uint8_t eps_reg_stbcon(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x20u : REG_STBCON;
}

static inline uint8_t eps_reg_pacon(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x29u : REG_PACON;
}

static inline uint8_t eps_reg_pawake(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x2au : REG_PAWAKE;
}

static inline uint8_t eps_reg_painten(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x2bu : REG_PAINTEN;
}

static inline uint8_t eps_reg_paintsta(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x2cu : REG_PAINTSTA;
}

static inline uint8_t eps_reg_pbcon(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x2du : REG_PBCON;
}

static inline uint8_t eps_reg_dcrb(enum eps_variant variant) {
	return eps_variant_is_6009(variant) ? 0x2eu : REG_DCRB;
}

static inline int eps_key_input_enabled(enum eps_variant variant, const uint8_t *regs) {
	return eps_variant_is_6009(variant) ?
		(((regs[eps_reg_stbcon(variant)] & BIT_AUTO_KEY_SCAN) != 0) ||
			((regs[eps_reg_pacon(variant)] & 0x01u) != 0)) :
		((regs[eps_reg_stbcon(variant)] & BIT_KEY_INPUT_ENABLE) != 0);
}

static inline int eps_has_pch(enum eps_variant variant) {
	return !eps_variant_is_6009(variant);
}

static inline int eps_has_indf2(enum eps_variant variant) {
	return !eps_variant_is_6009(variant);
}

static inline size_t eps_lcd_raw_size(enum eps_variant variant) {
	if (eps_variant_is_6009(variant))
		return 0x88u;
	if (eps_variant_is_9500(variant))
		return (size_t)(98u * 4u);
	return (size_t)(96u * 4u);
}

#endif /* FX_EMU_CORE_EPS6800_H */
