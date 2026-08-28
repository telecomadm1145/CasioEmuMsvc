/* Shared MMIO storage dimensions. */
#ifndef FX_EMU_CORE_MMIO_LAYOUT_H
#define FX_EMU_CORE_MMIO_LAYOUT_H

enum {
	MMIO_REG_COUNT = 0x80,
	MMIO_WBK_COUNT = 27,
	MMIO_LEGACY_RAM_COUNT = 64 * 128,
	/* ePS9500 addresses RAM as FSR + (BSR << 7).  Because FSR keeps all
	 * eight bits, page 3Fh extends through physical RAM address 207Fh. */
	MMIO_EPS9500_RAM_COUNT = MMIO_LEGACY_RAM_COUNT + 0x80,
	MMIO_RAM_COUNT = MMIO_EPS9500_RAM_COUNT
};

#endif /* FX_EMU_CORE_MMIO_LAYOUT_H */

