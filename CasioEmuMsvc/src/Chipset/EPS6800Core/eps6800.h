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

enum eps_lcd_address_model {
	EPS_LCD_ADDRESS_PAGED_128 = 0,
	EPS_LCD_ADDRESS_LINEAR_WRAP,
	EPS_LCD_ADDRESS_ROW_MAJOR
};

enum eps_kbd_matrix_model {
	EPS_KBD_MATRIX_GPIO = 0,
	EPS_KBD_MATRIX_EPS6009
};

enum eps_timer1_model {
	EPS_TIMER1_STANDARD = 0,
	EPS_TIMER1_EPS6009
};

struct eps_cpu_profile {
	uint8_t table_read_wrap_16bit;
	uint8_t full_tabptrh;
	uint8_t sleep_advances_pc;
	uint8_t extended_fsr_binary_destination;
	uint8_t reset_status;
};

struct eps_mmio_profile {
	uint8_t has_direct_normal_registers;
	uint8_t has_lcd_address_high;
	uint8_t has_counter0_registers;
	uint8_t has_standard_gpio_registers;
};

struct eps_register_range {
	uint8_t begin;
	uint8_t end;
};

enum {
	EPS_PERSISTENT_REGISTER_RANGE_COUNT = 2
};

struct eps_ram_profile {
	uint8_t page_mask;
	uint8_t offset_uses_bit7;
	size_t bank_size;
	struct eps_register_range persistent_registers[EPS_PERSISTENT_REGISTER_RANGE_COUNT];
};

struct eps_lcd_profile {
	uint8_t address_model;
	uint8_t address_low_max;
	uint8_t row_width;
	uint8_t fixed_contrast;
	uint8_t host_linear;
	uint8_t has_address_high;
};

struct eps_kbd_profile {
	uint8_t matrix_model;
	uint8_t key_input_mask;
	uint16_t press_delay_cycles;
	uint8_t accept_explicit_sleep_mode;
	uint8_t refresh_on_contact_level;
	uint8_t level_sensitive_inputs;
};

struct eps_timer_profile {
	uint8_t timer1_model;
	uint8_t t1_control_shift;
	uint8_t t1_enable_bit;
	uint8_t t0_interrupt_enable_bit;
	uint8_t t1_interrupt_enable_bit;
	uint8_t t2_interrupt_enable_bit;
	uint8_t shared_interrupt_control;
	uint8_t counter0_readback;
	uint8_t idle_cycles_per_20ms;
};

/*
 * Static silicon description. Register locations, structural capabilities,
 * and module behavior profiles all live here so a new EPS family member is
 * registered once instead of being rediscovered independently by each module.
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
	uint8_t has_pch;
	uint8_t has_indf2;
	uint8_t has_wbk;
	uint8_t stack_model;
	struct eps_ram_profile ram;
	size_t lcd_raw_size;
	struct eps_cpu_profile cpu;
	struct eps_mmio_profile mmio;
	struct eps_lcd_profile lcd;
	struct eps_kbd_profile kbd;
	struct eps_timer_profile timer;
};

static inline int eps_variant_is_valid(enum eps_variant variant) {
	return variant == EPS_VARIANT_6800 || variant == EPS_VARIANT_6009 ||
		variant == EPS_VARIANT_9500;
}

static inline int eps_variant_is_6009(enum eps_variant variant) {
	return variant == EPS_VARIANT_6009;
}

static inline int eps_variant_is_9500(enum eps_variant variant) {
	return variant == EPS_VARIANT_9500;
}

#ifdef __cplusplus
extern "C" {
#endif
const struct eps_variant_traits *eps_get_variant_traits(enum eps_variant variant);
#ifdef __cplusplus
}
#endif

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
	return eps_get_variant_traits(variant)->kbd.matrix_model == EPS_KBD_MATRIX_EPS6009 ?
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
	return eps_get_variant_traits(variant)->ram.page_mask;
}

static inline size_t eps_bank_ram_size(enum eps_variant variant) {
	return eps_get_variant_traits(variant)->ram.bank_size;
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
