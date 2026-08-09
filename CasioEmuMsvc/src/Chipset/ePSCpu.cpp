/*
 * EPS6800 machine adapter for CasioEmuMsvc.
 *
 * The machine implementation in EPS6800Core is derived from eps-emu-0601
 * and is licensed under GPL-3.0-or-later, matching this project.
 */
#include "ePSCpu.h"

#include "EPS6800Core/eps6800.h"
#include "EPS6800Core/machine.h"
#include "EPS6800Core/machine_debug.h"
#include "EPS6800Core/machine_internal.h"
#include "EPS6800Core/machine_io.h"
#include "EPS6800Core/machine_rom.h"
#include "EPS6800Core/machine_snapshot.h"

#include <istream>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <vector>

namespace {
	constexpr uint32_t kSnapshotMagic = 0x31535045; // "EPS1", little-endian
	constexpr uint32_t kMaximumSnapshotSize = 1024 * 1024;

	machine_state* CreateMachineOrThrow() {
		auto* state = machine_state_create();
		if (!state)
			throw std::runtime_error("Failed to create EPS6800 machine state");
		return state;
	}
}

namespace casioemu {
	ePSCPU::ePSCPU()
		: state_(CreateMachineOrThrow()),
		  regs(state_->mmio.regs),
		  ram(state_->mmio.ram),
		  wbk(state_->mmio.ram_wbk),
		  vram(state_->lcd.fb),
		  stack(state_->cpu.stack),
		  FSR(state_->mmio.regs[REG_FSR0]),
		  BSR(state_->mmio.regs[REG_BSR]),
		  FSR1(state_->mmio.regs[REG_FSR1]),
		  BSR1(state_->mmio.regs[REG_BSR1]),
		  STKPTR(state_->mmio.regs[REG_STKPTR]),
		  ACC(state_->mmio.regs[REG_ACC]),
		  STATUS(state_->cpu.status),
		  FSR2(state_->mmio.regs[REG_FSR2]),
		  BSR2(state_->mmio.regs[REG_BSR2]),
		  LCDARL(state_->mmio.regs[REG_LCDARL]),
		  LCDARH(state_->mmio.regs[REG_LCDARH]) {
	}

	ePSCPU::~ePSCPU() {
		machine_state_destroy(state_);
	}

	bool ePSCPU::LoadRom(const std::vector<unsigned char>& rom) {
		return machine_state_load_rom_image(state_, rom.data(), rom.size());
	}

	void ePSCPU::Reset() {
		machine_state_reset(state_);
	}

	void ePSCPU::Next() {
		machine_state_advance_cycles(state_, 1, true);
	}

	void ePSCPU::RunFrame() {
		machine_state_run_frame(state_);
	}

	void ePSCPU::KeyDown(uint8_t matrix_index) {
		machine_state_keydown(state_, matrix_index);
	}

	void ePSCPU::KeyUp(uint8_t matrix_index) {
		machine_state_keyup(state_, matrix_index);
	}

	void ePSCPU::OnDown() {
		machine_state_ondown(state_);
	}

	void ePSCPU::OnUp() {
		machine_state_onup(state_);
	}

	size_t ePSCPU::CopyLcd(uint8_t* output, size_t size) const {
		return machine_state_lcd_copy_framebuffer(state_, output, size);
	}

	size_t ePSCPU::LcdRawSize() const {
		return sizeof(state_->lcd.fb);
	}

	uint8_t ePSCPU::ReadByte(uint8_t address) {
		return machine_state_debug_read_byte(state_, address);
	}

	void ePSCPU::WriteByte(uint8_t address, uint8_t value) {
		machine_state_debug_write_byte(state_, address, value);
	}

	uint32_t ePSCPU::PC() const {
		return state_->cpu.pc << 1;
	}

	void ePSCPU::SetPC(uint32_t word_address) {
		state_->cpu.pc = word_address & 0x00ffffffu;
		state_->mmio.regs[REG_PCL] = static_cast<uint8_t>(state_->cpu.pc);
		state_->mmio.regs[REG_PCM] = static_cast<uint8_t>(state_->cpu.pc >> 8);
		state_->mmio.regs[REG_PCH] = static_cast<uint8_t>(state_->cpu.pc >> 16);
	}

	void ePSCPU::SaveState(std::ostream& stream) const {
		size_t size = 0;
		machine_snapshot* snapshot = machine_state_save_snapshot(state_, &size);
		if (!snapshot || size > std::numeric_limits<uint32_t>::max()) {
			machine_snapshot_free(snapshot);
			throw std::runtime_error("Failed to capture EPS6800 snapshot");
		}

		const void* data = machine_snapshot_data(snapshot, &size);
		const uint32_t encoded_size = static_cast<uint32_t>(size);
		stream.write(reinterpret_cast<const char*>(&kSnapshotMagic), sizeof(kSnapshotMagic));
		stream.write(reinterpret_cast<const char*>(&encoded_size), sizeof(encoded_size));
		stream.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
		machine_snapshot_free(snapshot);
		if (!stream)
			throw std::runtime_error("Failed to write EPS6800 snapshot");
	}

	void ePSCPU::LoadState(std::istream& stream) {
		uint32_t magic = 0;
		uint32_t size = 0;
		stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
		stream.read(reinterpret_cast<char*>(&size), sizeof(size));
		if (!stream || magic != kSnapshotMagic || size == 0 || size > kMaximumSnapshotSize)
			throw std::runtime_error("Invalid EPS6800 snapshot header");

		std::vector<uint8_t> data(size);
		stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
		if (!stream)
			throw std::runtime_error("Truncated EPS6800 snapshot");

		machine_snapshot* snapshot = machine_snapshot_from_data(data.data(), data.size());
		if (!snapshot)
			throw std::runtime_error("Invalid EPS6800 snapshot payload");
		machine_state_load_snapshot(state_, snapshot);
		machine_snapshot_free(snapshot);
	}
} // namespace casioemu
