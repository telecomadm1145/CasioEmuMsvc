/* Internal emulated machine save-state serialization layout. */
#ifndef FX_EMU_CORE_MACHINE_SNAPSHOT_INTERNAL_H
#define FX_EMU_CORE_MACHINE_SNAPSHOT_INTERNAL_H

#include "machine_internal.h"

#include <stddef.h>
#include <stdint.h>

enum {
	MACHINE_SNAPSHOT_MAGIC = 0x46585353u,
	MACHINE_SNAPSHOT_LEGACY_VERSION = 3u,
	MACHINE_SNAPSHOT_V4_VERSION = 4u,
	MACHINE_SNAPSHOT_V5_VERSION = 5u,
	MACHINE_SNAPSHOT_VERSION = 6u
};

struct machine_cpu_snapshot {
	uint32_t pc;
	uint32_t stack[CPU_STACK_DEPTH];
	uint8_t status;
	uint8_t rpt_counter;
	uint32_t rpt_target_pc;
	uint8_t mode;
	uint8_t int_pending;
	uint8_t sleep_repeat_pc;
};

/* v3 predates ePS9500 and stored only the 32-entry legacy stack. */
struct machine_cpu_snapshot_v3_legacy {
	uint32_t pc;
	uint32_t stack[CPU_LEGACY_STACK_DEPTH];
	uint8_t status;
	uint8_t rpt_counter;
	uint32_t rpt_target_pc;
	uint8_t mode;
	uint8_t int_pending;
	uint8_t sleep_repeat_pc;
};

struct machine_kbd_snapshot {
	uint8_t reg[KBD_REG_COUNT];
	uint8_t matrix[KBD_ROW_COUNT];
	uint8_t key_pending_down[KBD_KEY_COUNT];
	uint8_t key_pending_up[KBD_KEY_COUNT];
	uint16_t key_press_cycles[KBD_KEY_COUNT];
	uint16_t key_release_cycles[KBD_KEY_COUNT];
	uint8_t pending_press_mask;
	uint8_t porta_latch;
	uint8_t portb_latch;
	uint8_t portc_latch;
	uint8_t on_pressed;
	uint8_t on_pending_down;
	uint8_t on_pending_up;
	uint16_t on_press_cycles;
};

struct machine_lcd_snapshot_v4 {
	uint8_t fb[LCD_FB_SIZE];
	uint8_t reg[LCD_REG_COUNT];
};

struct machine_lcd_snapshot_v5 {
	uint8_t fb[LCD_FB_SIZE];
	uint8_t reg[LCD_REG_COUNT];
	uint8_t w192_fb[LCD_W192_FB_SIZE];
	uint8_t w192_page;
	uint8_t w192_column;
	uint8_t w192_rmw_column;
	uint8_t w192_portd;
	uint8_t w192_porte;
	uint8_t w192_dcrde;
	uint8_t w192_display_on;
	uint8_t w192_all_pixels_on;
	uint8_t w192_rmw_active;
	uint8_t w192_segment_reverse;
	uint8_t w192_com_reverse;
	uint8_t w192_contrast;
	uint8_t w192_contrast_pending;
};

struct machine_lcd_snapshot {
	uint8_t fb[LCD_FB_SIZE];
	uint8_t reg[LCD_REG_COUNT];
	uint8_t w192_fb[LCD_W192_FB_SIZE];
	uint8_t w192_page;
	uint8_t w192_column;
	uint8_t w192_rmw_column;
	uint8_t w192_portd;
	uint8_t w192_porte;
	uint8_t w192_portd_latch;
	uint8_t w192_porte_latch;
	uint8_t w192_dcrde;
	uint8_t w192_read_valid;
	uint8_t w192_bus_phase;
	uint8_t w192_display_on;
	uint8_t w192_all_pixels_on;
	uint8_t w192_rmw_active;
	uint8_t w192_segment_reverse;
	uint8_t w192_com_reverse;
	uint8_t w192_contrast;
	uint8_t w192_contrast_pending;
};

struct machine_mmio_snapshot {
	uint8_t regs[MMIO_REG_COUNT];
	uint8_t ram_wbk[MMIO_WBK_COUNT];
	uint8_t ram[MMIO_RAM_COUNT];
};

struct machine_mmio_snapshot_v3_legacy {
	uint8_t regs[MMIO_REG_COUNT];
	uint8_t ram_wbk[MMIO_WBK_COUNT];
	uint8_t ram[MMIO_LEGACY_RAM_COUNT];
};

struct machine_timer_snapshot {
	uint8_t reg[TIMER_REG_COUNT];
	int32_t t0psc, t1psc, t2psc;
	int32_t t0prl, t1prl, t2prl;
	int32_t t0cnt, t1cnt, t2cnt;
	int32_t t0crl, t1crl, t2crl;
};

struct machine_snapshot {
	uint32_t magic;
	uint32_t version;
	struct machine_cpu_snapshot cpu;
	struct machine_mmio_snapshot mmio;
	struct machine_lcd_snapshot lcd;
	struct machine_timer_snapshot timer;
	struct machine_kbd_snapshot kbd;
};

struct machine_snapshot_v4 {
	uint32_t magic;
	uint32_t version;
	struct machine_cpu_snapshot cpu;
	struct machine_mmio_snapshot mmio;
	struct machine_lcd_snapshot_v4 lcd;
	struct machine_timer_snapshot timer;
	struct machine_kbd_snapshot kbd;
};

struct machine_snapshot_v5 {
	uint32_t magic;
	uint32_t version;
	struct machine_cpu_snapshot cpu;
	struct machine_mmio_snapshot mmio;
	struct machine_lcd_snapshot_v5 lcd;
	struct machine_timer_snapshot timer;
	struct machine_kbd_snapshot kbd;
};

/* Layout emitted by v3 builds before ePS9500 support. */
struct machine_snapshot_v3_legacy {
	uint32_t magic;
	uint32_t version;
	struct machine_cpu_snapshot_v3_legacy cpu;
	struct machine_mmio_snapshot_v3_legacy mmio;
	struct machine_lcd_snapshot_v4 lcd;
	struct machine_timer_snapshot timer;
	struct machine_kbd_snapshot kbd;
};

/* Layout emitted during early ePS9500 work: expanded stack, legacy RAM. */
struct machine_snapshot_v3_expanded_stack {
	uint32_t magic;
	uint32_t version;
	struct machine_cpu_snapshot cpu;
	struct machine_mmio_snapshot_v3_legacy mmio;
	struct machine_lcd_snapshot_v4 lcd;
	struct machine_timer_snapshot timer;
	struct machine_kbd_snapshot kbd;
};

bool machine_snapshot_valid_internal(const struct machine_snapshot *snapshot);
size_t machine_snapshot_payload_size_internal(void);
struct machine_snapshot *machine_snapshot_alloc_internal(void);
void machine_snapshot_set_size_internal(size_t *size, size_t value);

#endif /* FX_EMU_CORE_MACHINE_SNAPSHOT_INTERNAL_H */

