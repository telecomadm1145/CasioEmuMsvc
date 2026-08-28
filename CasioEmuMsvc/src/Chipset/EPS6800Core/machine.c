/* Emulated machine boundary */

#include "eps6800.h"
#include "machine.h"
#include "machine_internal.h"

#include <stdlib.h>
#include <string.h>

static void machine_state_init(struct machine_state *state);

enum {
	MACHINE_PERSISTENT_LOW_REG_BEGIN = 0x13,
	MACHINE_PERSISTENT_LOW_REG_END = 0x20,
	MACHINE_PERSISTENT_HIGH_REG_BEGIN = 0x40,
	MACHINE_PERSISTENT_HIGH_REG_END = 0x80
};

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

/* enum eps_variant is intentionally contiguous and ordered 6800/6009/9500;
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

static size_t machine_state_legacy_ram_image_size(const struct machine_state *state) {
	return eps_bank_ram_size(state->mmio.variant) + sizeof(state->mmio.ram_wbk);
}

struct machine_state *machine_state_create(void) {
	struct machine_state *state = (struct machine_state *)calloc(1, sizeof(*state));
	if (!state) {
		return NULL;
	}

	machine_state_init(state);
	return state;
}

void machine_state_destroy(struct machine_state *state) {
	if (!state) {
		return;
	}

	free(state);
}

static void machine_state_init(struct machine_state *state) {
	rom_init(&state->rom);
	mmio_init_state(&state->mmio);
	machine_state_bind_modules(state);
}

void machine_state_set_variant(struct machine_state *state, enum eps_variant variant) {
	if (!state) {
		return;
	}

	state->mmio.variant = variant;
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
	if (!state)
		return;

	/* Clear the complete allocated backing storage, not only the active
	 * variant-visible window, matching the historical adapter behavior. */
	memset(state->mmio.ram, 0, sizeof(state->mmio.ram));
	memset(state->mmio.ram_wbk, 0, sizeof(state->mmio.ram_wbk));
	memset(&state->mmio.regs[MACHINE_PERSISTENT_LOW_REG_BEGIN], 0,
		MACHINE_PERSISTENT_LOW_REG_END - MACHINE_PERSISTENT_LOW_REG_BEGIN);
	memset(&state->mmio.regs[MACHINE_PERSISTENT_HIGH_REG_BEGIN], 0,
		MACHINE_PERSISTENT_HIGH_REG_END - MACHINE_PERSISTENT_HIGH_REG_BEGIN);
	machine_state_reset(state);
}

size_t machine_state_ram_image_size(const struct machine_state *state) {
	if (!state)
		return 0;
	return machine_state_legacy_ram_image_size(state) +
		(MACHINE_PERSISTENT_LOW_REG_END - MACHINE_PERSISTENT_LOW_REG_BEGIN) +
		(MACHINE_PERSISTENT_HIGH_REG_END - MACHINE_PERSISTENT_HIGH_REG_BEGIN);
}

bool machine_state_export_ram(
	const struct machine_state *state,
	uint8_t *data,
	size_t size
) {
	size_t bank_ram_size;
	uint8_t *output;

	if (!state || !data || size < machine_state_ram_image_size(state))
		return false;

	bank_ram_size = eps_bank_ram_size(state->mmio.variant);
	output = data;
	memcpy(output, state->mmio.ram, bank_ram_size);
	output += bank_ram_size;
	memcpy(output, state->mmio.ram_wbk, sizeof(state->mmio.ram_wbk));
	output += sizeof(state->mmio.ram_wbk);
	memcpy(output, &state->mmio.regs[MACHINE_PERSISTENT_LOW_REG_BEGIN],
		MACHINE_PERSISTENT_LOW_REG_END - MACHINE_PERSISTENT_LOW_REG_BEGIN);
	output += MACHINE_PERSISTENT_LOW_REG_END - MACHINE_PERSISTENT_LOW_REG_BEGIN;
	memcpy(output, &state->mmio.regs[MACHINE_PERSISTENT_HIGH_REG_BEGIN],
		MACHINE_PERSISTENT_HIGH_REG_END - MACHINE_PERSISTENT_HIGH_REG_BEGIN);
	return true;
}

bool machine_state_import_ram(
	struct machine_state *state,
	const uint8_t *data,
	size_t size
) {
	size_t bank_ram_size;
	size_t legacy_size;
	size_t full_size;
	const uint8_t *input;

	if (!state || !data)
		return false;

	bank_ram_size = eps_bank_ram_size(state->mmio.variant);
	legacy_size = machine_state_legacy_ram_image_size(state);
	full_size = machine_state_ram_image_size(state);
	if (size != bank_ram_size && size != legacy_size && size != full_size)
		return false;

	input = data;
	memcpy(state->mmio.ram, input, bank_ram_size);
	input += bank_ram_size;
	if (size >= legacy_size) {
		memcpy(state->mmio.ram_wbk, input, sizeof(state->mmio.ram_wbk));
		input += sizeof(state->mmio.ram_wbk);
	}
	if (size == full_size) {
		memcpy(&state->mmio.regs[MACHINE_PERSISTENT_LOW_REG_BEGIN], input,
			MACHINE_PERSISTENT_LOW_REG_END - MACHINE_PERSISTENT_LOW_REG_BEGIN);
		input += MACHINE_PERSISTENT_LOW_REG_END - MACHINE_PERSISTENT_LOW_REG_BEGIN;
		memcpy(&state->mmio.regs[MACHINE_PERSISTENT_HIGH_REG_BEGIN], input,
			MACHINE_PERSISTENT_HIGH_REG_END - MACHINE_PERSISTENT_HIGH_REG_BEGIN);
	}
	return true;
}


