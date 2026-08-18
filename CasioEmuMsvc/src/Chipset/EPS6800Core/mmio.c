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
	MMIO_EPS6009_RAM_PAGE_MASK = 0x0F,
	MMIO_RAM_PAGE_SHIFT = 7,
	MMIO_WBK_FIRST_REG = REG_TR0CON,
	MMIO_WBK_LAST_REG = REG_DCRDE,
	MMIO_FSR_RAM_END = 0xFF,
	MMIO_POSTID_RESET = BIT_FSR0ID | BIT_FSR1ID | BIT_LCDID | BIT_FSR2ID,
	MMIO_DCR_ALL_INPUTS = 0xFF,
	MMIO_DCRC_RESET = 0xFF,
	MMIO_DCRDE_RESET = 0x33
};

static uint32_t mmio_ram_address(uint8_t page, uint8_t offset) {
	return ((uint32_t)(page & MMIO_RAM_PAGE_MASK) << MMIO_RAM_PAGE_SHIFT) |
		(offset & MMIO_RAM_OFFSET_MASK);
}

static uint32_t mmio_variant_ram_address(const struct mmio_state *state, uint8_t page, uint8_t offset) {
	const uint8_t page_mask = eps_variant_is_6009(state->variant) ? MMIO_EPS6009_RAM_PAGE_MASK : MMIO_RAM_PAGE_MASK;
	return ((uint32_t)(page & page_mask) << MMIO_RAM_PAGE_SHIFT) |
		(offset & MMIO_RAM_OFFSET_MASK);
}

static bool mmio_eps6009_normal_register(uint8_t addr) {
	return (addr >= 0x12u && addr <= 0x1fu) ||
		(addr >= 0x32u && addr <= 0x7fu);
}

static bool mmio_direct_normal_register(const struct mmio_state *state, uint8_t addr) {
	return eps_variant_is_6009(state->variant) &&
		mmio_eps6009_normal_register(addr) &&
		addr != eps_reg_lcddat(state->variant);
}

static bool mmio_read_peripheral(struct mmio_state *state, uint8_t addr, uint8_t *byte);

static uint32_t mmio_direct_ram_address(const struct mmio_state *state, uint8_t addr) {
	return mmio_variant_ram_address(state, state->regs[REG_BSR], addr);
}

static uint32_t mmio_fsr0_ram_address(const struct mmio_state *state) {
	return mmio_variant_ram_address(state, state->regs[REG_BSR], state->regs[REG_FSR0]);
}

static uint32_t mmio_fsr1_ram_address(const struct mmio_state *state) {
	return mmio_variant_ram_address(state, state->regs[REG_BSR1], state->regs[REG_FSR1]);
}

static uint32_t mmio_fsr2_ram_address(const struct mmio_state *state) {
	return mmio_variant_ram_address(state, state->regs[REG_BSR2], state->regs[REG_FSR2]);
}

static uint8_t mmio_wbk_index(uint8_t addr) {
	return (uint8_t)(addr - MMIO_WBK_FIRST_REG);
}

static bool mmio_wbk_register_selected(const struct mmio_state *state, uint8_t addr) {
	return !eps_variant_is_6009(state->variant) &&
		(addr >= MMIO_WBK_FIRST_REG) &&
		(addr <= MMIO_WBK_LAST_REG) &&
		(state->regs[eps_reg_cpucon(state->variant)] & BIT_WBK);
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
		if (!eps_has_indf2(state->variant))
			return addr;
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

static bool mmio_lcd_register(const struct mmio_state *state, uint8_t addr) {
	return addr == eps_reg_lcddat(state->variant) ||
		addr == eps_reg_postid(state->variant) ||
		addr == eps_reg_lcdarl(state->variant) ||
		addr == eps_reg_lcdcon(state->variant) ||
		(!eps_variant_is_6009(state->variant) && addr == eps_reg_lcdarh(state->variant));
}

static bool mmio_timer_register(const struct mmio_state *state, uint8_t addr) {
	if (addr == eps_reg_trintcon(state->variant) ||
		addr == eps_reg_trintsta(state->variant) ||
		addr == eps_reg_tr0con(state->variant) ||
		addr == eps_reg_trl0h(state->variant) ||
		addr == eps_reg_trl0l(state->variant) ||
		addr == eps_reg_trl1(state->variant) ||
		addr == eps_reg_tr2wcon(state->variant) ||
		addr == eps_reg_trl2(state->variant))
		return true;
	if (!eps_variant_is_6009(state->variant) && (addr == REG_T0CH || addr == REG_T0CL))
		return true;
	return false;
}

static bool mmio_kbd_register(const struct mmio_state *state, uint8_t addr) {
	if (addr == eps_reg_stbcon(state->variant) ||
		addr == eps_reg_porta(state->variant) ||
		addr == eps_reg_pacon(state->variant) ||
		addr == eps_reg_pawake(state->variant) ||
		addr == eps_reg_painten(state->variant) ||
		addr == eps_reg_paintsta(state->variant) ||
		addr == eps_reg_portb(state->variant) ||
		addr == eps_reg_pbcon(state->variant) ||
		addr == eps_reg_dcrb(state->variant))
		return true;
	if (!eps_variant_is_6009(state->variant) &&
		(addr == REG_DCRA || addr == REG_PORTC || addr == REG_PCCON || addr == REG_DCRC))
		return true;
	return false;
}

static uint8_t mmio_read_indf0(struct mmio_state *state) {
	if (state->regs[REG_FSR0] & MMIO_RAM_SELECT_MASK) {
		return state->ram[mmio_fsr0_ram_address(state)];
	}

	uint8_t byte = 0;
	const uint8_t target = state->regs[REG_FSR0];
	if (!mmio_read_peripheral(state, target, &byte)) {
		byte = state->regs[target];
	}
	return byte;
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
		mmio_write_byte_state(state, target, byte); /* indirect SFR write */
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
	if (mmio_lcd_register(state, addr)) {
		*byte = lcd_read_byte_state(state->lcd, addr);
		return true;
	}
	if (mmio_timer_register(state, addr)) {
		*byte = timer_read_byte_state(state->timer, addr);
		return true;
	}
	if (mmio_kbd_register(state, addr)) {
		*byte = kbd_read_byte_state(state->kbd, addr);
		return true;
	}

	return false;
}

static bool mmio_write_peripheral(struct mmio_state *state, uint8_t addr, uint8_t byte) {
	if (mmio_lcd_register(state, addr)) {
		lcd_write_byte_state(state->lcd, addr, byte);
		return true;
	}
	if (mmio_timer_register(state, addr)) {
		timer_write_byte_state(state->timer, addr, byte);
		return true;
	}
	if (mmio_kbd_register(state, addr)) {
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

static uint8_t mmio_filter_register_write(const struct mmio_state *state, uint8_t addr, uint8_t byte) {
	if (eps_variant_is_6009(state->variant) && (addr == REG_BSR || addr == REG_BSR1)) {
		return (uint8_t)(byte & MMIO_EPS6009_RAM_PAGE_MASK);
	}
	if (eps_variant_is_6009(state->variant) && addr == eps_reg_cpucon(state->variant)) {
		return (uint8_t)(byte & (BIT_GLINT | BIT_MS1 | BIT_MS0));
	}
	return byte;
}

void mmio_bad_read_byte_state(struct mmio_state *state, uint8_t addr) {
	mmio_report_bad_access(state, "read", addr);
}

void mmio_bad_write_byte_state(struct mmio_state *state, uint8_t addr) {
	mmio_report_bad_access(state, "write", addr);
}

static void mmio_postid_step_fsr0(struct mmio_state *state) {
	const uint8_t postid = state->regs[eps_reg_postid(state->variant)];
	if (postid & BIT_FSR0PE) {
		if (postid & BIT_FSR0ID) {
			state->regs[REG_FSR0]++;
		}
		else {
			state->regs[REG_FSR0]--;
		}
	}
}

static void mmio_increment_extended_fsr(struct mmio_state *state, uint8_t bsr_reg, uint8_t fsr_reg) {
	if (eps_variant_is_6009(state->variant)) {
		if (state->regs[fsr_reg] == MMIO_FSR_RAM_END) {
			state->regs[bsr_reg] = (uint8_t)((state->regs[bsr_reg] + 1u) & MMIO_EPS6009_RAM_PAGE_MASK);
			state->regs[fsr_reg] = MMIO_RAM_SELECT_MASK;
		}
		else {
			state->regs[fsr_reg]++;
		}
		return;
	}
	if (state->regs[fsr_reg] == MMIO_FSR_RAM_END) {
		state->regs[bsr_reg]++;
		state->regs[fsr_reg] = MMIO_RAM_SELECT_MASK;
	}
	else {
		state->regs[fsr_reg]++;
	}
}

static void mmio_decrement_extended_fsr(struct mmio_state *state, uint8_t bsr_reg, uint8_t fsr_reg) {
	if (eps_variant_is_6009(state->variant)) {
		if (state->regs[fsr_reg] == MMIO_RAM_SELECT_MASK) {
			state->regs[bsr_reg] = (uint8_t)((state->regs[bsr_reg] - 1u) & MMIO_EPS6009_RAM_PAGE_MASK);
			state->regs[fsr_reg] = MMIO_FSR_RAM_END;
		}
		else {
			state->regs[fsr_reg]--;
		}
		return;
	}
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
	const uint8_t postid = state->regs[eps_reg_postid(state->variant)];
	if (postid & post_enable_bit) {
		state->regs[fsr_reg] |= MMIO_RAM_SELECT_MASK;
		if (postid & post_increment_bit) {
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
		if (eps_has_indf2(state->variant))
			byte = mmio_read_indf2(state);
		else if (!mmio_read_peripheral(state, addr, &byte))
			byte = state->regs[addr];
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
	if (addr == REG_TABPTRH) {
		byte &= MASK_TABPTRH;
	}
	byte = mmio_filter_register_write(state, addr, byte);
	state->regs[addr] = byte;
}

uint8_t mmio_read_byte_internal_state(struct mmio_state *state, uint8_t addr) {
	uint8_t byte = state->regs[addr];
	cpu_verbose_log_read_state(state->cpu, addr, byte);
	return byte;
}

void mmio_post_id_state(struct mmio_state *state, uint8_t addr) {
	if (addr == eps_reg_lcddat(state->variant)) {
		lcd_process_postid_state(state->lcd);
		return;
	}
	switch (addr) {
	case REG_INDF0:
		mmio_postid_step_fsr0(state);
		break;
	case REG_INDF1:
		mmio_postid_step_fsr1(state);
		break;
	case REG_INDF2:
		if (eps_has_indf2(state->variant))
			mmio_postid_step_fsr2(state);
		break;
	case REG_LCDDAT:
		lcd_process_postid_state(state->lcd);
		break;
	}
}

static void mmio_carry_fsr0(struct mmio_state *state) {
	state->regs[REG_BSR]++;
	if (eps_variant_is_6009(state->variant))
		state->regs[REG_BSR] &= MMIO_EPS6009_RAM_PAGE_MASK;
}

static void mmio_carry_extended_fsr(struct mmio_state *state, uint8_t bsr_reg, uint8_t fsr_reg) {
	state->regs[bsr_reg]++;
	if (eps_variant_is_6009(state->variant))
		state->regs[bsr_reg] &= MMIO_EPS6009_RAM_PAGE_MASK;
	state->regs[fsr_reg] |= MMIO_RAM_SELECT_MASK;
}

static void mmio_borrow_fsr0(struct mmio_state *state) {
	state->regs[REG_BSR]--;
	if (eps_variant_is_6009(state->variant)) {
		state->regs[REG_BSR] &= MMIO_EPS6009_RAM_PAGE_MASK;
		return;
	}
	if (state->regs[REG_BSR] != 0) {
		state->regs[REG_FSR0] |= MMIO_RAM_SELECT_MASK;
	}
}

static void mmio_borrow_extended_fsr(struct mmio_state *state, uint8_t bsr_reg, uint8_t fsr_reg) {
	state->regs[bsr_reg]--;
	if (eps_variant_is_6009(state->variant)) {
		state->regs[bsr_reg] &= MMIO_EPS6009_RAM_PAGE_MASK;
	}
	else if (state->regs[bsr_reg] != 0) {
		state->regs[fsr_reg] |= MMIO_RAM_SELECT_MASK;
	}
	if (eps_variant_is_6009(state->variant)) {
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
		if (eps_has_pch(state->variant))
			state->regs[REG_PCH]++;
		break;
	case REG_TABPTRL:
		state->regs[REG_TABPTRM]++;
		break;
	case REG_TABPTRM:
		state->regs[REG_TABPTRH]++;
		break;
	case REG_FSR2:
		if (eps_has_indf2(state->variant))
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
		if (eps_has_pch(state->variant))
			state->regs[REG_PCH]--;
		break;
	case REG_TABPTRL:
		state->regs[REG_TABPTRM] -= 2; /* Preserve observed TABPTR borrow adjustment. */
		break;
	case REG_TABPTRM:
		state->regs[REG_TABPTRH]--;
		break;
	case REG_FSR2:
		if (eps_has_indf2(state->variant))
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
		if (addr == REG_TABPTRH) {
			byte &= MASK_TABPTRH;
		}
		byte = mmio_filter_register_write(state, addr, byte);
		if (addr != eps_reg_lcddat(state->variant)) {
			state->regs[addr] = byte; /* Do all write. */
		}
		switch (addr) {
		case 0x12:
		case 0x13:
		case 0x14:
		case 0x15:
		case 0x16:
		case 0x17:
		case 0x18:
		case 0x19:
		case 0x1A:
		case 0x1B:
		case 0x1C:
		case 0x1D:
		case 0x1E:
		case 0x1F:
			if (mmio_direct_normal_register(state, addr))
				break;
			break;
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
			if (eps_has_indf2(state->variant))
				mmio_write_indf2(state, byte);
			else
				mmio_write_peripheral(state, addr, byte);
			break;
		default:
			mmio_write_peripheral(state, addr, byte);
			break;
		}
	}
	mmio_notify_debug_access(state, linear_address, &byte, true, false);
}

static void mmio_clear_function_registers(struct mmio_state *state) {
	if (eps_variant_is_6009(state->variant)) {
		/* ePS6009.h defines 12h-1Fh and 32h-7Fh as normal registers. */
		memset(&state->regs[0x00], 0x00, 0x12);
		memset(&state->regs[0x20], 0x00, 0x12);
	}
	else {
		/* ePS6800.h defines 13h-1Fh and 40h-7Fh as normal registers (RAM).
		 * Hardware reset clears SFRs but must retain those ranges. */
		memset(&state->regs[0x00], 0x00, 0x13);
		memset(&state->regs[0x20], 0x00, 0x20);
	}
}

static void mmio_apply_reset_defaults(struct mmio_state *state) {
	state->regs[REG_FSR1] = MMIO_RAM_SELECT_MASK;
	if (eps_variant_is_6009(state->variant)) {
		state->regs[REG_STATUS] = BIT_STATUS_SGE | BIT_STATUS_SLE;
		state->regs[eps_reg_porta(state->variant)] = 0x8fu;
		state->regs[eps_reg_portb(state->variant)] = 0x00u;
		state->regs[eps_reg_pacon(state->variant)] = 0x0eu;
		state->regs[eps_reg_pbcon(state->variant)] = 0x00u;
		state->regs[eps_reg_dcrb(state->variant)] = 0x03u;
		state->regs[eps_reg_postid(state->variant)] = BIT_FSR0ID | BIT_FSR1ID | BIT_LCDID;
	}
	else {
		state->regs[REG_FSR2] = MMIO_RAM_SELECT_MASK;
		state->regs[REG_POSTID] = MMIO_POSTID_RESET;
		state->regs[REG_DCRA] = MMIO_DCR_ALL_INPUTS;
		state->regs[REG_DCRB] = MMIO_DCR_ALL_INPUTS;
		state->regs[REG_DCRC] = MMIO_DCRC_RESET;
		state->regs[REG_DCRDE] = MMIO_DCRDE_RESET;
	}
}

void mmio_reset_state(struct mmio_state *state) {
	mmio_clear_function_registers(state);
	mmio_apply_reset_defaults(state);
}

void mmio_init_state(struct mmio_state *state) {
	memset(state->regs, 0x00, sizeof(state->regs));
	state->variant = EPS_VARIANT_6800;
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
