/* ePS6800 Register & Memory */

#include "cpu_internal.h"
#include "eps6800.h"
#include "kbd_internal.h"
#include "lcd_internal.h"
#include "mmio_internal.h"
#include "timer_internal.h"

#include <string.h>

enum {
	MMIO_RAM_SELECT_MASK = 0x80,
	MMIO_RAM_OFFSET_MASK = 0x7F,
	MMIO_RAM_PAGE_MASK = 0x3F,
	MMIO_RAM_PAGE_SHIFT = 7,
	MMIO_WBK_FIRST_REG = REG_TR0CON,
	MMIO_WBK_LAST_REG = REG_DCRDE,
	MMIO_FSR_RAM_END = 0xFF,
	MMIO_POSTID_RESET = BIT_FSR0ID | BIT_FSR1ID | BIT_LCDID,
	MMIO_DCR_ALL_INPUTS = 0xFF,
	MMIO_DCRC_RESET = 0xFF
};

static uint32_t mmio_ram_address(uint8_t page, uint8_t offset) {
	return ((uint32_t)(page & MMIO_RAM_PAGE_MASK) << MMIO_RAM_PAGE_SHIFT) |
		(offset & MMIO_RAM_OFFSET_MASK);
}

static uint32_t mmio_direct_ram_address(const struct mmio_state *state, uint8_t addr) {
	return mmio_ram_address(state->regs[REG_BSR], addr);
}

static uint32_t mmio_fsr0_ram_address(const struct mmio_state *state) {
	return mmio_ram_address(state->regs[REG_BSR], state->regs[REG_FSR0]);
}

static uint32_t mmio_fsr1_ram_address(const struct mmio_state *state) {
	return mmio_ram_address(state->regs[REG_BSR1], state->regs[REG_FSR1]);
}

static uint32_t mmio_fsr2_ram_address(const struct mmio_state *state) {
	return mmio_ram_address(state->regs[REG_BSR2], state->regs[REG_FSR2]);
}

static uint8_t mmio_wbk_index(uint8_t addr) {
	return (uint8_t)(addr - MMIO_WBK_FIRST_REG);
}

static bool mmio_wbk_register_selected(const struct mmio_state *state, uint8_t addr) {
	return (addr >= MMIO_WBK_FIRST_REG) &&
		(addr <= MMIO_WBK_LAST_REG) &&
		(state->regs[REG_CPUCON] & BIT_WBK);
}

static uint32_t mmio_effective_linear_address(const struct mmio_state *state, uint8_t addr) {
	if (addr & MMIO_RAM_SELECT_MASK) {
		return 0x80u + mmio_direct_ram_address(state, addr);
	}
	switch (addr) {
	case REG_INDF0:
		if (state->regs[REG_FSR0] & MMIO_RAM_SELECT_MASK)
			return 0x80u + mmio_fsr0_ram_address(state);
		return state->regs[REG_FSR0] & 0x7fu;
	case REG_INDF1:
		return 0x80u + mmio_fsr1_ram_address(state);
	case REG_INDF2:
		return 0x80u + mmio_fsr2_ram_address(state);
	default:
		return addr;
	}
}

static bool mmio_notify_debug_access(
	struct mmio_state *state,
	uint32_t linear_address,
	uint8_t *value,
	bool write,
	bool before
) {
	if (state->debug_access && state->debug_access_suppression == 0)
		return state->debug_access(state->debug_access_user, linear_address, value, write, before);
	return false;
}

static bool mmio_lcd_register(uint8_t addr) {
	switch (addr) {
	case REG_POSTID:
	case REG_LCDARH:
	case REG_LCDARL:
	case REG_LCDDAT:
	case REG_LCDCON:
		return true;
	default:
		return false;
	}
}

static bool mmio_timer_register(uint8_t addr) {
	switch (addr) {
	case REG_INTSTA:
	case REG_TR0CON:
	case REG_TRL0H:
	case REG_TRL0L:
	case REG_T0CH:
	case REG_T0CL:
	case REG_TR1CON:
	case REG_TRL1:
	case REG_TR2WCON:
	case REG_TRL2:
		return true;
	default:
		return false;
	}
}

static bool mmio_kbd_register(uint8_t addr) {
	switch (addr) {
	case REG_STBCON:
	case REG_PORTA:
	case REG_PACON:
	case REG_DCRA:
	case REG_PAWAKE:
	case REG_PAINTEN:
	case REG_PAINTSTA:
	case REG_PORTB:
	case REG_PBCON:
	case REG_DCRB:
	case REG_PORTC:
	case REG_PCCON:
	case REG_DCRC:
		return true;
	default:
		return false;
	}
}

static uint8_t mmio_read_indf0(struct mmio_state *state) {
	if (state->regs[REG_FSR0] & MMIO_RAM_SELECT_MASK) {
		return state->ram[mmio_fsr0_ram_address(state)];
	}

	return state->regs[state->regs[REG_FSR0]]; /* indirect SFR read */
}

static uint8_t mmio_read_indf1(struct mmio_state *state) {
	state->regs[REG_FSR1] |= MMIO_RAM_SELECT_MASK;
	return state->ram[mmio_fsr1_ram_address(state)];
}

static uint8_t mmio_read_indf2(struct mmio_state *state) {
	state->regs[REG_FSR2] |= MMIO_RAM_SELECT_MASK;
	return state->ram[mmio_fsr2_ram_address(state)];
}

static void mmio_write_indf0(struct mmio_state *state, uint8_t byte) {
	if (state->regs[REG_FSR0] & MMIO_RAM_SELECT_MASK) {
		state->ram[mmio_fsr0_ram_address(state)] = byte;
	}
	else {
		uint8_t target = state->regs[REG_FSR0];
		state->regs[target] = byte; /* indirect SFR write */
	}
}

static void mmio_write_indf1(struct mmio_state *state, uint8_t byte) {
	state->regs[REG_FSR1] |= MMIO_RAM_SELECT_MASK;
	state->ram[mmio_fsr1_ram_address(state)] = byte;
}

static void mmio_write_indf2(struct mmio_state *state, uint8_t byte) {
	state->regs[REG_FSR2] |= MMIO_RAM_SELECT_MASK;
	state->ram[mmio_fsr2_ram_address(state)] = byte;
}

static bool mmio_read_peripheral(struct mmio_state *state, uint8_t addr, uint8_t *byte) {
	if (mmio_lcd_register(addr)) {
		*byte = lcd_read_byte_state(state->lcd, addr);
		return true;
	}
	if (mmio_timer_register(addr)) {
		*byte = timer_read_byte_state(state->timer, addr);
		return true;
	}
	if (mmio_kbd_register(addr)) {
		*byte = kbd_read_byte_state(state->kbd, addr);
		return true;
	}

	return false;
}

static bool mmio_write_peripheral(struct mmio_state *state, uint8_t addr, uint8_t byte) {
	if (mmio_lcd_register(addr)) {
		lcd_write_byte_state(state->lcd, addr, byte);
		return true;
	}
	if (mmio_timer_register(addr)) {
		timer_write_byte_state(state->timer, addr, byte);
		return true;
	}
	if (mmio_kbd_register(addr)) {
		kbd_write_byte_state(state->kbd, addr, byte);
		return true;
	}

	return false;
}

void mmio_connect_peripherals_state(
	struct mmio_state *state,
	struct kbd_state *kbd,
	struct lcd_state *lcd,
	struct timer_state *timer
) {
	state->kbd = kbd;
	state->lcd = lcd;
	state->timer = timer;
}

void mmio_connect_cpu_state(struct mmio_state *state, struct cpu_state *cpu) {
	state->cpu = cpu;
}

void mmio_set_debug_access_callback_state(
	struct mmio_state *state,
	mmio_debug_access_callback callback,
	void *user
) {
	state->debug_access = callback;
	state->debug_access_user = user;
}

void mmio_suppress_debug_access_state(struct mmio_state *state, bool suppress) {
	if (suppress) {
		state->debug_access_suppression++;
	}
	else if (state->debug_access_suppression > 0) {
		state->debug_access_suppression--;
	}
}

static uint8_t mmio_cpu_get_status(struct mmio_state *state) {
	return cpu_get_status_state(state->cpu);
}

static void mmio_cpu_set_status(struct mmio_state *state, uint8_t status) {
	cpu_set_status_state(state->cpu, status);
}

static struct core_diag_state *mmio_diag(struct mmio_state *state) {
	return state->cpu ? &state->cpu->diag : NULL;
}

static void mmio_report_bad_access(struct mmio_state *state, const char *access, uint8_t addr) {
	core_diag_printf_state(mmio_diag(state), "Error: bad %s byte @ %04x\n", access, addr);
	core_diag_printf_state(mmio_diag(state), "PC: %02x%02x, ALU: %02x, STATUS: %02x\n",
		mmio_read_byte_internal_state(state, REG_PCM),
		mmio_read_byte_internal_state(state, REG_PCL),
		mmio_read_byte_internal_state(state, REG_ACC),
		mmio_read_byte_internal_state(state, REG_STATUS));
}

void mmio_bad_read_byte_state(struct mmio_state *state, uint8_t addr) {
	mmio_report_bad_access(state, "read", addr);
}

void mmio_bad_write_byte_state(struct mmio_state *state, uint8_t addr) {
	mmio_report_bad_access(state, "write", addr);
}

static void mmio_postid_step_fsr0(struct mmio_state *state) {
	if (state->regs[REG_POSTID] & BIT_FSR0PE) {
		if (state->regs[REG_POSTID] & BIT_FSR0ID) {
			state->regs[REG_FSR0]++;
		}
		else {
			state->regs[REG_FSR0]--;
		}
	}
}

static void mmio_increment_extended_fsr(struct mmio_state *state, uint8_t bsr_reg, uint8_t fsr_reg) {
	if (state->regs[fsr_reg] == MMIO_FSR_RAM_END) {
		state->regs[bsr_reg]++;
		state->regs[fsr_reg] = MMIO_RAM_SELECT_MASK;
	}
	else {
		state->regs[fsr_reg]++;
	}
}

static void mmio_decrement_extended_fsr(struct mmio_state *state, uint8_t bsr_reg, uint8_t fsr_reg) {
	if (state->regs[fsr_reg] == MMIO_RAM_SELECT_MASK) {
		state->regs[bsr_reg]--;
		state->regs[fsr_reg] = MMIO_FSR_RAM_END;
	}
	else {
		state->regs[fsr_reg]--;
	}
}

static void mmio_postid_step_extended_fsr(
	struct mmio_state *state,
	uint8_t post_enable_bit,
	uint8_t post_increment_bit,
	uint8_t bsr_reg,
	uint8_t fsr_reg
) {
	if (state->regs[REG_POSTID] & post_enable_bit) {
		state->regs[fsr_reg] |= MMIO_RAM_SELECT_MASK;
		if (state->regs[REG_POSTID] & post_increment_bit) {
			mmio_increment_extended_fsr(state, bsr_reg, fsr_reg);
		}
		else {
			mmio_decrement_extended_fsr(state, bsr_reg, fsr_reg);
		}
	}
}

static void mmio_postid_step_fsr1(struct mmio_state *state) {
	mmio_postid_step_extended_fsr(state, BIT_FSR1PE, BIT_FSR1ID, REG_BSR1, REG_FSR1);
}

static void mmio_postid_step_fsr2(struct mmio_state *state) {
	mmio_postid_step_extended_fsr(state, BIT_FSR2PE, BIT_FSR2ID, REG_BSR2, REG_FSR2);
}

/* Address 00-FF: regular registers and memory bank 0. */
uint8_t mmio_read_byte_state(struct mmio_state *state, uint8_t addr) {
	uint8_t byte = 0;
	const uint32_t linear_address = mmio_effective_linear_address(state, addr);
	if (mmio_notify_debug_access(state, linear_address, &byte, false, true)) {
		mmio_notify_debug_access(state, linear_address, &byte, false, false);
		return byte;
	}
	if (addr & MMIO_RAM_SELECT_MASK) {
		byte = state->ram[mmio_direct_ram_address(state, addr)];
		cpu_verbose_log_read_state(state->cpu, addr, byte);
		mmio_notify_debug_access(state, linear_address, &byte, false, false);
		return byte;
	}
	if (mmio_wbk_register_selected(state, addr)) {
		uint8_t wbk_byte = state->ram_wbk[mmio_wbk_index(addr)];
		cpu_verbose_log_read_state(state->cpu, addr, wbk_byte);
		mmio_notify_debug_access(state, linear_address, &wbk_byte, false, false);
		return wbk_byte;
	}

	switch (addr) {
	case REG_STATUS:
		byte = mmio_cpu_get_status(state);
		break;
	case REG_INDF0:
		byte = mmio_read_indf0(state);
		break;
	case REG_INDF1:
		byte = mmio_read_indf1(state);
		break;
	case REG_INDF2:
		byte = mmio_read_indf2(state);
		break;
	default:
		if (!mmio_read_peripheral(state, addr, &byte)) {
			byte = state->regs[addr];
		}
		break;
	}
	cpu_verbose_log_read_state(state->cpu, addr, byte);
	mmio_notify_debug_access(state, linear_address, &byte, false, false);
	return byte;
}

void mmio_write_byte_internal_state(struct mmio_state *state, uint8_t addr, uint8_t byte) {
	state->regs[addr] = byte;
}

uint8_t mmio_read_byte_internal_state(struct mmio_state *state, uint8_t addr) {
	uint8_t byte = state->regs[addr];
	cpu_verbose_log_read_state(state->cpu, addr, byte);
	return byte;
}

void mmio_post_id_state(struct mmio_state *state, uint8_t addr) {
	switch (addr) {
	case REG_INDF0:
		mmio_postid_step_fsr0(state);
		break;
	case REG_INDF1:
		mmio_postid_step_fsr1(state);
		break;
	case REG_INDF2:
		mmio_postid_step_fsr2(state);
		break;
	case REG_LCDDAT:
		lcd_process_postid_state(state->lcd);
		break;
	}
}

static void mmio_carry_fsr0(struct mmio_state *state) {
	state->regs[REG_BSR]++;
}

static void mmio_carry_extended_fsr(struct mmio_state *state, uint8_t bsr_reg, uint8_t fsr_reg) {
	state->regs[bsr_reg]++;
	state->regs[fsr_reg] |= MMIO_RAM_SELECT_MASK;
}

static void mmio_borrow_fsr0(struct mmio_state *state) {
	state->regs[REG_BSR]--;
	if (state->regs[REG_BSR] != 0) {
		state->regs[REG_FSR0] |= MMIO_RAM_SELECT_MASK;
	}
}

static void mmio_borrow_extended_fsr(struct mmio_state *state, uint8_t bsr_reg, uint8_t fsr_reg) {
	state->regs[bsr_reg]--;
	if (state->regs[bsr_reg] != 0) {
		state->regs[fsr_reg] |= MMIO_RAM_SELECT_MASK;
	}
}

void mmio_carry_propagate_state(struct mmio_state *state, uint8_t addr) {
	switch (addr) {
	case REG_FSR0:
		mmio_carry_fsr0(state);
		break;
	case REG_FSR1:
		mmio_carry_extended_fsr(state, REG_BSR1, REG_FSR1);
		break;
	case REG_PCL:
		state->regs[REG_PCM]++;
		break;
	case REG_PCM:
		state->regs[REG_PCH]++;
		break;
	case REG_TABPTRL:
		state->regs[REG_TABPTRM]++;
		break;
	case REG_TABPTRM:
		state->regs[REG_TABPTRH]++;
		break;
	case REG_FSR2:
		mmio_carry_extended_fsr(state, REG_BSR2, REG_FSR2);
		break;
	default:
		break;
	}
}

void mmio_borrow_propagate_state(struct mmio_state *state, uint8_t addr) {
	switch (addr) {
	case REG_FSR0:
		mmio_borrow_fsr0(state);
		break;
	case REG_FSR1:
		mmio_borrow_extended_fsr(state, REG_BSR1, REG_FSR1);
		break;
	case REG_PCL:
		state->regs[REG_PCM]--;
		break;
	case REG_PCM:
		state->regs[REG_PCH]--;
		break;
	case REG_TABPTRL:
		state->regs[REG_TABPTRM] -= 2; /* Preserve observed TABPTR borrow adjustment. */
		break;
	case REG_TABPTRM:
		state->regs[REG_TABPTRH]--;
		break;
	case REG_FSR2:
		mmio_borrow_extended_fsr(state, REG_BSR2, REG_FSR2);
		break;
	default:
		break;
	}
}

void mmio_write_byte_state(struct mmio_state *state, uint8_t addr, uint8_t byte) {
	const uint32_t linear_address = mmio_effective_linear_address(state, addr);
	if (mmio_notify_debug_access(state, linear_address, &byte, true, true)) {
		return;
	}
	if (addr & MMIO_RAM_SELECT_MASK) {
		uint32_t mem_addr = mmio_direct_ram_address(state, addr);
		state->ram[mem_addr] = byte;
	}
	else if (mmio_wbk_register_selected(state, addr)) {
		state->ram_wbk[mmio_wbk_index(addr)] = byte;
	}
	else {
		if (addr != REG_LCDDAT) {
			state->regs[addr] = byte; /* Do all write. */
		}
		switch (addr) {
		case REG_STATUS:
			mmio_cpu_set_status(state, byte);
			break;
		case REG_INDF0:
			mmio_write_indf0(state, byte);
			break;
		case REG_INDF1:
			mmio_write_indf1(state, byte);
			break;
		case REG_INDF2:
			mmio_write_indf2(state, byte);
			break;
		default:
			mmio_write_peripheral(state, addr, byte);
			break;
		}
	}
	mmio_notify_debug_access(state, linear_address, &byte, true, false);
}

static void mmio_clear_registers(struct mmio_state *state) {
	memset(state->regs, 0x00, sizeof(state->regs));
}

static void mmio_apply_reset_defaults(struct mmio_state *state) {
	state->regs[REG_FSR1] = MMIO_RAM_SELECT_MASK;
	state->regs[REG_FSR2] = MMIO_RAM_SELECT_MASK;
	state->regs[REG_POSTID] = MMIO_POSTID_RESET;
	state->regs[REG_DCRA] = MMIO_DCR_ALL_INPUTS;
	state->regs[REG_DCRB] = MMIO_DCR_ALL_INPUTS;
	state->regs[REG_DCRC] = MMIO_DCRC_RESET;
}

void mmio_reset_state(struct mmio_state *state) {
	mmio_clear_registers(state);
	mmio_apply_reset_defaults(state);
}

void mmio_init_state(struct mmio_state *state) {
	mmio_reset_state(state);
	memset(state->ram_wbk, 0x00, sizeof(state->ram_wbk));
	memset(state->ram, 0x00, sizeof(state->ram));
	state->cpu = NULL;
	state->kbd = NULL;
	state->lcd = NULL;
	state->timer = NULL;
	state->debug_access = NULL;
	state->debug_access_user = NULL;
	state->debug_access_suppression = 0;
}

void mmio_trace_snapshot_state(
	const struct mmio_state *state,
	uint8_t *regs_out,
	uint8_t *ram_wbk_out,
	uint8_t *ram_out
) {
	if (regs_out) {
		memcpy(regs_out, state->regs, MMIO_REG_COUNT);
	}
	if (ram_wbk_out) {
		memcpy(ram_wbk_out, state->ram_wbk, MMIO_WBK_COUNT);
	}
	if (ram_out) {
		memcpy(ram_out, state->ram, MMIO_RAM_COUNT);
	}
}
