/* Emulated machine boundary */

#include "eps6800.h"
#include "machine.h"
#include "machine_internal.h"

#include <stdlib.h>
#include <string.h>

static void machine_state_init(struct machine_state *state, enum eps_variant variant);

const struct eps_variant_traits *eps_get_variant_traits(enum eps_variant variant) {
	static const struct eps_variant_traits eps6800_traits = {
		REG_CPUCON, REG_POSTID, REG_LCDARL, REG_LCDDAT, REG_LCDARH, REG_LCDCON,
		REG_INTSTA, REG_INTSTA, REG_TR0CON, REG_TR1CON, REG_TRL0L, REG_TRL0H,
		REG_TRL1, REG_TR2WCON, REG_TRL2, REG_PORTA, REG_PORTB, REG_STBCON,
		REG_PACON, REG_PAWAKE, REG_PAINTEN, REG_PAINTSTA, REG_PBCON, REG_DCRB,
		1u, 1u, 1u, EPS_STACK_MODEL_LINEAR,
		{ 0x3fu, 0u, MMIO_LEGACY_RAM_COUNT, {{ 0x13u, 0x20u }, { 0x40u, 0x80u }} },
		(size_t)(96u * 4u),
		{ 0u, 0u, 0u, 1u, 0xc0u },
		{ 0u, 1u, 1u, 1u },
		{ EPS_LCD_ADDRESS_PAGED_128, 0x61u, 0u, 0u, 0u, 1u },
		{ EPS_KBD_MATRIX_GPIO, 0x7fu, 1000u, 0u, 0u, 0u },
		{ EPS_TIMER1_STANDARD, 0u, BIT_T1EN, BIT_TMR0IE, BIT_TMR1IE, BIT_TMR2IE,
			0u, 1u, 1u }
	};
	static const struct eps_variant_traits eps6009_traits = {
		0x31u, 0x30u, 0x09u, REG_LCDDAT, 0xffu, 0x2fu,
		0x22u, 0x21u, 0x23u, 0x23u, 0x24u, 0x25u,
		0x26u, 0x27u, 0x28u, 0x10u, 0x11u, 0x20u,
		0x29u, 0x2au, 0x2bu, 0x2cu, 0x2du, 0x2eu,
		0u, 0u, 0u, EPS_STACK_MODEL_LINEAR,
		{ 0x0fu, 0u, MMIO_LEGACY_RAM_COUNT, {{ 0x12u, 0x20u }, { 0x32u, 0x80u }} },
		0x88u,
		{ 1u, 1u, 1u, 0u, 0x30u },
		{ 1u, 0u, 0u, 0u },
		{ EPS_LCD_ADDRESS_LINEAR_WRAP, 0x87u, 0u, 0x0fu, 1u, 0u },
		{ EPS_KBD_MATRIX_EPS6009, 0x0fu, 0u, 1u, 0u, 1u },
		{ EPS_TIMER1_EPS6009, 4u, 0x40u, BIT_TMR0I, BIT_TMR1I, BIT_TMR2I,
			1u, 0u, 51u }
	};
	static const struct eps_variant_traits eps9500_traits = {
		REG_CPUCON, REG_POSTID, REG_LCDARL, REG_LCDDAT, REG_LCDARH, REG_LCDCON,
		REG_INTSTA, REG_INTSTA, REG_TR0CON, REG_TR1CON, REG_TRL0L, REG_TRL0H,
		REG_TRL1, REG_TR2WCON, REG_TRL2, REG_PORTA, REG_PORTB, REG_STBCON,
		REG_PACON, REG_PAWAKE, REG_PAINTEN, REG_PAINTSTA, REG_PBCON, REG_DCRB,
		1u, 1u, 1u, EPS_STACK_MODEL_DESCENDING_EVEN,
		{ 0x3fu, 1u, MMIO_EPS9500_RAM_COUNT, {{ 0x13u, 0x20u }, { 0x40u, 0x80u }} },
		(size_t)(98u * 4u),
		{ 0u, 0u, 1u, 1u, 0xc0u },
		{ 0u, 1u, 1u, 1u },
		{ EPS_LCD_ADDRESS_ROW_MAJOR, 0x61u, 98u, 0u, 1u, 1u },
		{ EPS_KBD_MATRIX_GPIO, 0xffu, 1000u, 1u, 1u, 0u },
		{ EPS_TIMER1_STANDARD, 0u, BIT_T1EN, BIT_TMR0IE, BIT_TMR1IE, BIT_TMR2IE,
			0u, 1u, 1u }
	};
	static const struct eps_variant_traits eps6800_w192_traits = {
		REG_CPUCON, REG_POSTID, REG_LCDARL, REG_LCDDAT, REG_LCDARH, REG_LCDCON,
		REG_INTSTA, REG_INTSTA, REG_TR0CON, REG_TR1CON, REG_TRL0L, REG_TRL0H,
		REG_TRL1, REG_TR2WCON, REG_TRL2, REG_PORTA, REG_PORTB, REG_STBCON,
		REG_PACON, REG_PAWAKE, REG_PAINTEN, REG_PAINTSTA, REG_PBCON, REG_DCRB,
		1u, 1u, 1u, EPS_STACK_MODEL_LINEAR,
		{ 0x3fu, 0u, MMIO_LEGACY_RAM_COUNT, {{ 0x13u, 0x20u }, { 0x40u, 0x80u }} },
		(size_t)(192u * 8u),
		{ 0u, 0u, 0u, 1u, 0xc0u },
		{ 0u, 1u, 1u, 1u },
		/* The internal LCDDAT block is retained as an SFR-compatible stub;
		 * visible pixels are supplied by the external PortD/PortE controller. */
		{ EPS_LCD_ADDRESS_PAGED_128, 0x61u, 0u, 0u, 0u, 1u },
		{ EPS_KBD_MATRIX_GPIO, 0x7fu, 1000u, 0u, 0u, 0u },
		{ EPS_TIMER1_STANDARD, 0u, BIT_T1EN, BIT_TMR0IE, BIT_TMR1IE, BIT_TMR2IE,
			0u, 1u, 1u }
	};

	switch (variant) {
	case EPS_VARIANT_6009:
		return &eps6009_traits;
	case EPS_VARIANT_9500:
		return &eps9500_traits;
	case EPS_VARIANT_6800_W192:
		return &eps6800_w192_traits;
	case EPS_VARIANT_6800:
	default:
		return &eps6800_traits;
	}
}

enum machine_reset_target {
	MACHINE_RESET_TARGET_MMIO = 0,
	MACHINE_RESET_TARGET_LCD,
	MACHINE_RESET_TARGET_KBD
};

enum machine_reset_register {
	MACHINE_RESET_REG_CPUCON = 0,
	MACHINE_RESET_REG_POSTID,
	MACHINE_RESET_REG_STBCON,
	MACHINE_RESET_REG_PORTA,
	MACHINE_RESET_REG_PACON,
	MACHINE_RESET_REG_DCRA,
	MACHINE_RESET_REG_PORTB,
	MACHINE_RESET_REG_PBCON,
	MACHINE_RESET_REG_DCRB,
	MACHINE_RESET_REG_PORTC,
	MACHINE_RESET_REG_PCCON,
	MACHINE_RESET_REG_DCRC,
	MACHINE_RESET_REG_PAWAKE
};

enum machine_reset_value_source {
	MACHINE_RESET_VALUE_LITERAL = 0,
	MACHINE_RESET_VALUE_CPUCON
};

struct machine_reset_write {
	enum machine_reset_target target;
	enum machine_reset_register reg;
	enum machine_reset_value_source value_source;
	uint8_t value;
};

struct machine_reset_profile {
	uint8_t cpucon_base;
	uint8_t cpucon_config_shift;
	const struct machine_reset_write *writes;
	size_t write_count;
};

#define MACHINE_RESET_LITERAL(target_, reg_, value_) \
	{ target_, reg_, MACHINE_RESET_VALUE_LITERAL, value_ }
#define MACHINE_RESET_CPUCON(target_, reg_) \
	{ target_, reg_, MACHINE_RESET_VALUE_CPUCON, 0u }

static const struct machine_reset_write machine_reset_eps6800[] = {
	MACHINE_RESET_CPUCON(MACHINE_RESET_TARGET_MMIO, MACHINE_RESET_REG_CPUCON),
	MACHINE_RESET_CPUCON(MACHINE_RESET_TARGET_KBD, MACHINE_RESET_REG_PAWAKE),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_LCD, MACHINE_RESET_REG_POSTID,
		BIT_FSR0ID | BIT_FSR1ID | BIT_LCDID | BIT_FSR2ID)
};

static const struct machine_reset_write machine_reset_eps6009[] = {
	MACHINE_RESET_CPUCON(MACHINE_RESET_TARGET_MMIO, MACHINE_RESET_REG_CPUCON),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_LCD, MACHINE_RESET_REG_POSTID,
		BIT_FSR0ID | BIT_FSR1ID | BIT_LCDID),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_KBD, MACHINE_RESET_REG_PACON, 0x0eu),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_KBD, MACHINE_RESET_REG_PBCON, 0x00u),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_KBD, MACHINE_RESET_REG_DCRB, 0x03u),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_KBD, MACHINE_RESET_REG_PORTA, 0xffu),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_KBD, MACHINE_RESET_REG_PORTB, 0xffu)
};

static const struct machine_reset_write machine_reset_eps9500[] = {
	MACHINE_RESET_CPUCON(MACHINE_RESET_TARGET_MMIO, MACHINE_RESET_REG_CPUCON),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_KBD, MACHINE_RESET_REG_STBCON, 0x20u),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_KBD, MACHINE_RESET_REG_PORTA, 0xffu),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_KBD, MACHINE_RESET_REG_PACON, 0x00u),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_KBD, MACHINE_RESET_REG_DCRA, 0xffu),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_KBD, MACHINE_RESET_REG_PORTB, 0xffu),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_KBD, MACHINE_RESET_REG_PBCON, 0x00u),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_KBD, MACHINE_RESET_REG_DCRB, 0xffu),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_KBD, MACHINE_RESET_REG_PORTC, 0xffu),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_KBD, MACHINE_RESET_REG_PCCON, 0x00u),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_KBD, MACHINE_RESET_REG_DCRC, 0xffu),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_KBD, MACHINE_RESET_REG_PAWAKE, 0x00u),
	MACHINE_RESET_LITERAL(MACHINE_RESET_TARGET_LCD, MACHINE_RESET_REG_POSTID,
		BIT_FSR0ID | BIT_FSR1ID | BIT_LCDID | BIT_FSR2ID)
};

/* enum eps_variant is intentionally contiguous and ordered 6800/6009/9500/6800_W192;
 * keep this table in the same order so MSVC's C compiler does not need C99
 * array-designator support. */
static const struct machine_reset_profile machine_reset_profiles[] = {
	{
		0x10u, 9u,
		machine_reset_eps6800, sizeof(machine_reset_eps6800) / sizeof(machine_reset_eps6800[0])
	},
	{
		0x00u, 1u,
		machine_reset_eps6009, sizeof(machine_reset_eps6009) / sizeof(machine_reset_eps6009[0])
	},
	{
		0x10u, 9u,
		machine_reset_eps9500, sizeof(machine_reset_eps9500) / sizeof(machine_reset_eps9500[0])
	},
	{
		0x10u, 9u,
		machine_reset_eps6800, sizeof(machine_reset_eps6800) / sizeof(machine_reset_eps6800[0])
	}
};

static const struct machine_reset_profile *machine_reset_profile(enum eps_variant variant) {
	const size_t count = sizeof(machine_reset_profiles) / sizeof(machine_reset_profiles[0]);
	const size_t index = (size_t)variant;
	return index < count ? &machine_reset_profiles[index] : &machine_reset_profiles[EPS_VARIANT_6800];
}

static uint8_t machine_reset_register_address(enum eps_variant variant, enum machine_reset_register reg) {
	switch (reg) {
	case MACHINE_RESET_REG_CPUCON:
		return eps_reg_cpucon(variant);
	case MACHINE_RESET_REG_POSTID:
		return eps_reg_postid(variant);
	case MACHINE_RESET_REG_STBCON:
		return eps_reg_stbcon(variant);
	case MACHINE_RESET_REG_PORTA:
		return eps_reg_porta(variant);
	case MACHINE_RESET_REG_PACON:
		return eps_reg_pacon(variant);
	case MACHINE_RESET_REG_DCRA:
		return REG_DCRA;
	case MACHINE_RESET_REG_PORTB:
		return eps_reg_portb(variant);
	case MACHINE_RESET_REG_PBCON:
		return eps_reg_pbcon(variant);
	case MACHINE_RESET_REG_DCRB:
		return eps_reg_dcrb(variant);
	case MACHINE_RESET_REG_PORTC:
		return REG_PORTC;
	case MACHINE_RESET_REG_PCCON:
		return REG_PCCON;
	case MACHINE_RESET_REG_DCRC:
		return REG_DCRC;
	case MACHINE_RESET_REG_PAWAKE:
		return eps_reg_pawake(variant);
	default:
		return 0xffu;
	}
}

static void machine_apply_reset_write(
	struct machine_state *state,
	const struct machine_reset_write *write,
	uint8_t cpucon
) {
	const uint8_t addr = machine_reset_register_address(state->mmio.variant, write->reg);
	const uint8_t value = write->value_source == MACHINE_RESET_VALUE_CPUCON ? cpucon : write->value;

	switch (write->target) {
	case MACHINE_RESET_TARGET_MMIO:
		mmio_write_byte_internal_state(&state->mmio, addr, value);
		break;
	case MACHINE_RESET_TARGET_LCD:
		lcd_write_byte_state(&state->lcd, addr, value);
		break;
	case MACHINE_RESET_TARGET_KBD:
		kbd_write_byte_state(&state->kbd, addr, value);
		break;
	default:
		break;
	}
}

struct machine_state *machine_state_create(void) {
	return machine_state_create_variant(EPS_VARIANT_6800);
}

struct machine_state *machine_state_create_variant(enum eps_variant variant) {
	struct machine_state *state;
	if (!eps_variant_is_valid(variant)) {
		return NULL;
	}

	state = (struct machine_state *)calloc(1, sizeof(*state));
	if (!state) {
		return NULL;
	}

	machine_state_init(state, variant);
	machine_state_reset(state);
	return state;
}

void machine_state_destroy(struct machine_state *state) {
	if (!state) {
		return;
	}

	free(state);
}

static void machine_state_init(struct machine_state *state, enum eps_variant variant) {
	rom_init(&state->rom);
	mmio_init_state(&state->mmio, variant);
	machine_state_bind_modules(state);
}

enum eps_variant machine_state_variant(const struct machine_state *state) {
	return state ? state->mmio.variant : EPS_VARIANT_6800;
}

void machine_state_reset(struct machine_state *state) {
	const struct machine_reset_profile *profile;
	uint16_t config_word;
	uint8_t cpucon;
	size_t i;

	if (!state) {
		return;
	}

	mmio_reset_state(&state->mmio);
	cpu_reset_state(&state->cpu);
	lcd_reset_state(&state->lcd);
	timer_reset_state(&state->timer);
	kbd_reset_state(&state->kbd);

	/* Reference ice.dll CIce::Reset: variant-specific SFRs must be written
	 * through their owning peripherals so the flat debugger mirror and device
	 * state stay synchronized.  The ordered profile below preserves those
	 * side effects while keeping silicon reset policy out of the control flow. */
	profile = machine_reset_profile(state->mmio.variant);
	config_word = rom_read_word(&state->rom, 12);
	cpucon = (uint8_t)(profile->cpucon_base |
		(uint8_t)((config_word >> profile->cpucon_config_shift) & 1u));
	for (i = 0; i < profile->write_count; ++i) {
		machine_apply_reset_write(state, &profile->writes[i], cpucon);
	}
}

void machine_state_clear_ram_and_reset(struct machine_state *state) {
	const struct eps_ram_profile *ram_profile;
	size_t i;

	if (!state)
		return;

	/* Clear the complete allocated backing storage, not only the active
	 * variant-visible window, matching the historical adapter behavior. */
	memset(state->mmio.ram, 0, sizeof(state->mmio.ram));
	memset(state->mmio.ram_wbk, 0, sizeof(state->mmio.ram_wbk));
	ram_profile = &eps_get_variant_traits(state->mmio.variant)->ram;
	for (i = 0; i < EPS_PERSISTENT_REGISTER_RANGE_COUNT; ++i) {
		const struct eps_register_range *range = &ram_profile->persistent_registers[i];
		memset(&state->mmio.regs[range->begin], 0, (size_t)(range->end - range->begin));
	}
	machine_state_reset(state);
}

size_t machine_state_ram_image_size(const struct machine_state *state) {
	if (!state)
		return 0;
	return eps_bank_ram_size(state->mmio.variant) + sizeof(state->mmio.ram_wbk) + MMIO_REG_COUNT;
}

bool machine_state_export_ram(
	const struct machine_state *state,
	uint8_t *data,
	size_t size
) {
	size_t bank_ram_size;
	const struct eps_ram_profile *ram_profile;
	size_t i;
	uint8_t *output;

	if (!state || !data || size < machine_state_ram_image_size(state))
		return false;

	bank_ram_size = eps_bank_ram_size(state->mmio.variant);
	output = data;
	memcpy(output, state->mmio.ram, bank_ram_size);
	output += bank_ram_size;
	memcpy(output, state->mmio.ram_wbk, sizeof(state->mmio.ram_wbk));
	output += sizeof(state->mmio.ram_wbk);
	memset(output, 0, MMIO_REG_COUNT);
	ram_profile = &eps_get_variant_traits(state->mmio.variant)->ram;
	for (i = 0; i < EPS_PERSISTENT_REGISTER_RANGE_COUNT; ++i) {
		const struct eps_register_range *range = &ram_profile->persistent_registers[i];
		memcpy(&output[range->begin], &state->mmio.regs[range->begin],
			(size_t)(range->end - range->begin));
	}
	return true;
}

bool machine_state_import_ram(
	struct machine_state *state,
	const uint8_t *data,
	size_t size
) {
	size_t bank_ram_size;
	size_t full_size;
	const struct eps_ram_profile *ram_profile;
	size_t i;
	const uint8_t *input;

	if (!state || !data)
		return false;

	bank_ram_size = eps_bank_ram_size(state->mmio.variant);
	full_size = machine_state_ram_image_size(state);
	if (size != full_size)
		return false;

	input = data;
	memcpy(state->mmio.ram, input, bank_ram_size);
	input += bank_ram_size;
	memcpy(state->mmio.ram_wbk, input, sizeof(state->mmio.ram_wbk));
	input += sizeof(state->mmio.ram_wbk);
	ram_profile = &eps_get_variant_traits(state->mmio.variant)->ram;
	for (i = 0; i < EPS_PERSISTENT_REGISTER_RANGE_COUNT; ++i) {
		const struct eps_register_range *range = &ram_profile->persistent_registers[i];
		memcpy(&state->mmio.regs[range->begin], &input[range->begin],
			(size_t)(range->end - range->begin));
	}
	return true;
}


