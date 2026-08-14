#include "ePSCpu.h"
#include "Eps6800Display.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
	constexpr size_t kRomSize = 0x20000;
	constexpr size_t kLcdSize = casioemu::EPS6800_LCD_RAW_SIZE;

	uint32_t Fnv1a(const uint8_t* data, size_t size) {
		uint32_t hash = 2166136261u;
		for (size_t i = 0; i < size; ++i) {
			hash ^= data[i];
			hash *= 16777619u;
		}
		return hash;
	}

	std::vector<unsigned char> ReadRom(const char* path) {
		std::ifstream stream(path, std::ios::binary);
		if (!stream)
			return {};
		return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
	}

	void Boot(casioemu::ePSCPU& machine) {
		machine.Reset();
		machine.OnDown();
		for (int i = 0; i < 2; ++i)
			machine.RunFrame();
		machine.OnUp();
		for (int i = 0; i < 100; ++i)
			machine.RunFrame();
	}

	void Tap(casioemu::ePSCPU& machine, uint8_t key, int settle_frames = 4) {
		machine.KeyDown(key);
		for (int i = 0; i < 2; ++i)
			machine.RunFrame();
		machine.KeyUp(key);
		for (int i = 0; i < settle_frames; ++i)
			machine.RunFrame();
	}

	casioemu::Eps6800DisplayFrame Capture(casioemu::ePSCPU& machine) {
		std::array<uint8_t, kLcdSize> lcd{};
		machine.CopyLcd(lcd.data(), lcd.size());
		return casioemu::DecodeEps6800Display(lcd.data(), lcd.size());
	}

	casioemu::Eps6800LcdControl CaptureControl(casioemu::ePSCPU& machine) {
		std::array<uint8_t, kLcdSize> lcd{};
		casioemu::Eps6800LcdControl control{};
		machine.CopyLcd(lcd.data(), lcd.size(), &control);
		return control;
	}

	void SetRomWord(std::vector<uint8_t>& rom, uint32_t address, uint16_t word) {
		const size_t offset = static_cast<size_t>(address) * 4;
		rom[offset] = static_cast<uint8_t>((word >> 12) & 0x0f);
		rom[offset + 1] = static_cast<uint8_t>((word >> 8) & 0x0f);
		rom[offset + 2] = static_cast<uint8_t>((word >> 4) & 0x0f);
		rom[offset + 3] = static_cast<uint8_t>(word & 0x0f);
	}

	void SetPackedRomWord(std::vector<uint8_t>& rom, uint32_t address, uint16_t word) {
		const size_t offset = static_cast<size_t>(address) * 2;
		rom[offset] = static_cast<uint8_t>(word);
		rom[offset + 1] = static_cast<uint8_t>(word >> 8);
	}

	bool Check(bool condition, const char* label, int line) {
		if (!condition)
			std::cerr << "[eps6800][fail] line " << line << ": " << label << "\n";
		return condition;
	}

	bool KeyboardMatrixSmoke() {
		constexpr uint8_t kPortA = 0x31;
		constexpr uint8_t kDirectionA = 0x33;
		constexpr uint8_t kPaInterruptEnable = 0x35;
		constexpr uint8_t kPaInterruptStatus = 0x36;
		constexpr uint8_t kPortB = 0x37;
		constexpr uint8_t kDirectionB = 0x39;
		constexpr uint8_t kCpuControl = 0x20;
		constexpr uint8_t kGlobalInterruptEnable = 0x04;
		constexpr uint8_t kOnMask = 0x80;

		const auto Scan = [&](std::initializer_list<uint8_t> keys, uint8_t selected_pb_mask,
			bool on = false, uint8_t firmware_ram_40 = 0) {
			casioemu::ePSCPU machine;
			std::vector<uint8_t> rom(0x20000, 0);
			if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian))
				return static_cast<uint8_t>(0);
			machine.Reset();
			machine.WriteByte(kDirectionA, 0xff); // PA inputs with pull-ups.
			machine.WriteByte(kDirectionB, 0x00); // PB scan outputs.
			machine.WriteByte(kPortB, static_cast<uint8_t>(~selected_pb_mask));
			machine.WriteByte(0x40, firmware_ram_40);
			for (const auto key : keys)
				machine.KeyDown(key);
			if (on)
				machine.OnDown();
			for (int i = 0; i < 1100; ++i)
				machine.Next();
			return machine.ReadByte(kPortA);
		};

		const auto OnInterrupt = [&]() {
			casioemu::ePSCPU machine;
			std::vector<uint8_t> rom(0x20000, 0);
			if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian))
				return false;
			machine.Reset();
			machine.WriteByte(kDirectionA, 0xff);
			machine.WriteByte(kPaInterruptEnable, kOnMask);
			machine.WriteByte(kCpuControl, kGlobalInterruptEnable);
			machine.OnDown();
			for (int i = 0; i < 1100; ++i)
				machine.Next();
			return (machine.ReadByte(kPaInterruptStatus) & kOnMask) != 0;
		};
		const auto ShortOnPulseIsObservable = [&]() {
			casioemu::ePSCPU machine;
			std::vector<uint8_t> rom(0x20000, 0);
			if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian))
				return false;
			machine.Reset();
			machine.WriteByte(kDirectionA, 0xff);
			machine.OnDown();
			machine.OnUp();
			for (int i = 0; i < 1001; ++i)
				machine.Next();
			const bool pressed = (machine.ReadByte(kPortA) & kOnMask) == 0;
			for (int i = 0; i < 1000; ++i)
				machine.Next();
			return pressed && (machine.ReadByte(kPortA) & kOnMask) != 0;
		};
		const auto OnIgnoresGpioDirection = [&]() {
			casioemu::ePSCPU machine;
			std::vector<uint8_t> rom(0x20000, 0);
			if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian))
				return false;
			machine.Reset();
			machine.WriteByte(kDirectionA, 0x00);
			machine.WriteByte(kPortA, 0xff);
			machine.OnDown();
			for (int i = 0; i < 1100; ++i)
				machine.Next();
			return machine.ReadByte(kPortA) == 0x7f;
		};
		const auto KeyEnableWake = [&]() {
			casioemu::ePSCPU machine;
			std::vector<uint8_t> rom(0x20000, 0);
			SetPackedRomWord(rom, 0, 0x0002); // SLEEP
			SetPackedRomWord(rom, 1, 0x4e5a); // MOV A,#5Ah after wake
			if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian))
				return false;
			machine.Reset();
			machine.WriteByte(0x30, 0x80); // STBCON.KE enables PA0-PA6 key wake.
			machine.WriteByte(kDirectionA, 0xff);
			machine.WriteByte(kDirectionB, 0x80);
			machine.WriteByte(kPortB, 0x00);
			machine.Next();
			if ((machine.PC() >> 1) != 0)
				return false;
			machine.KeyDown(0);
			for (int i = 0; i < 1100; ++i)
				machine.Next();
			return (machine.PC() >> 1) != 0;
		};

		constexpr uint8_t kPb0 = 1u << 0;
		constexpr uint8_t kPb1 = 1u << 1;
		constexpr uint8_t kPb5 = 1u << 5;
		return
			Scan({0, 1}, kPb0) == 0xfc &&                 // same PB row
			Scan({0, 8}, kPb0) == 0xfe &&                 // same PA column, PB0
			Scan({0, 8}, kPb1) == 0xfe &&                 // same PA column, PB1
			Scan({0, 1, 8}, kPb1) == 0xfc &&              // 3-key rectangle ghosts PA1
			Scan({0, 1, 8, 9}, kPb1) == 0xfc &&           // four physical corners
			Scan({0}, kPb0, true) == 0x7e &&              // independent ON plus matrix key
			Scan({}, kPb0, true) == 0x7f &&               // ON is independent of PB0
			Scan({}, kPb5, true) == 0x7f &&               // ON is independent of PB5
			Scan({3, 46}, kPb0 | kPb5, true) == 0x37 &&   // SHIFT+7+ON, paired rows
			Scan({3, 46}, kPb5, true) == 0x3f &&          // SHIFT+ON
			Scan({3, 46}, kPb0, true) == 0x77 &&          // 7+ON
			Scan({0}, kPb0, false, 0x10) == 0xfe &&       // firmware RAM 40h is not scan state
			OnInterrupt() &&
			ShortOnPulseIsObservable() &&
			OnIgnoresGpioDirection() &&
			KeyEnableWake();
	}

	bool TimerSmoke() {
		constexpr uint8_t kInterruptStatus = 0x24;
		constexpr uint8_t kTimer0Control = 0x25;
		constexpr uint8_t kTimer0ReloadLow = 0x26;
		constexpr uint8_t kTimer0ReloadHigh = 0x27;
		constexpr uint8_t kTimer0Enable = 0x08;
		constexpr uint8_t kTimer0Flag = 0x01;

		casioemu::ePSCPU machine;
		std::vector<uint8_t> rom(0x20000, 0);
		if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian)) {
			std::cerr << "timer packed ROM load failed\n";
			return false;
		}
		machine.Reset();
		machine.WriteByte(kTimer0ReloadLow, 0x02);
		machine.WriteByte(kTimer0ReloadHigh, 0x00);
		machine.WriteByte(kTimer0Control, kTimer0Enable);
		for (int i = 0; i < 100; ++i)
			machine.Next();
		if ((machine.ReadByte(kInterruptStatus) & kTimer0Flag) == 0)
			return false;

		casioemu::ePSCPU idle_machine;
		std::vector<uint8_t> idle_rom(0x20000, 0);
		SetPackedRomWord(idle_rom, 0, 0x0002); // SLEP
		SetPackedRomWord(idle_rom, 1, 0x4e5a); // MOV A,#5Ah after Timer1 wake
		if (!idle_machine.LoadRom(idle_rom, casioemu::Eps6800RomFormat::PackedLittleEndian))
			return false;
		idle_machine.Reset();
		idle_machine.WriteByte(0x20, 0x03); // CPUCON.MS1 selects Idle on SLEP.
		idle_machine.WriteByte(kTimer0ReloadLow, 0x02);
		idle_machine.WriteByte(kTimer0ReloadHigh, 0x00);
		idle_machine.WriteByte(kTimer0Control, kTimer0Enable);
		idle_machine.WriteByte(0x2b, 0x40); // Timer1 reload.
		idle_machine.WriteByte(0x2a, 0x88); // T1WKEN | T1EN.
		idle_machine.Next();
		if ((idle_machine.PC() >> 1) != 0)
			return false;
		for (int i = 0; i < 20; ++i)
			idle_machine.Next();
		if ((idle_machine.ReadByte(kInterruptStatus) & kTimer0Flag) != 0)
			return false; // Timer0 is stopped while the CPU is in Idle.
		for (int i = 0; i < 300; ++i)
			idle_machine.Next();
		return (idle_machine.PC() >> 1) != 0;
	}

	bool RepeatInterruptDeferralSmoke() {
		constexpr uint8_t kCpuControl = 0x20;
		constexpr uint8_t kTimer1Control = 0x2a;
		constexpr uint8_t kTimer1Reload = 0x2b;
		constexpr uint8_t kRepeatCount = 0x80;
		constexpr uint8_t kDestination = 0x81;

		casioemu::ePSCPU machine;
		std::vector<uint8_t> rom(0x20000, 0);
		SetPackedRomWord(rom, 0, 0x2780); // RPT 80h
		SetPackedRomWord(rom, 1, 0x1d81); // INC 81h
		SetPackedRomWord(rom, 2, 0xc002); // SJMP 2
		SetPackedRomWord(rom, 8, 0x0001); // RETI
		if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian))
			return false;
		machine.Reset();
		machine.SetTimerCycleDivisor(1);
		machine.WriteByte(kRepeatCount, 10);
		machine.WriteByte(kDestination, 0);
		machine.WriteByte(kCpuControl, 0x04); // CPUCON.GLINT
		machine.WriteByte(kTimer1Reload, 1);
		machine.WriteByte(kTimer1Control, 0x18); // TMR1IE | T1EN
		for (int i = 0; i < 20; ++i)
			machine.Next();
		return machine.ReadByte(kDestination) == 10;
	}

	bool InterruptEntryMasksGlobalInterruptSmoke() {
		constexpr uint8_t kStackPointer = 0x06;
		constexpr uint8_t kCpuControl = 0x20;
		constexpr uint8_t kTimer1Control = 0x2a;
		constexpr uint8_t kTimer1Reload = 0x2b;
		constexpr uint8_t kGlobalInterruptEnable = 0x04;

		casioemu::ePSCPU machine;
		std::vector<uint8_t> rom(0x20000, 0);
		SetPackedRomWord(rom, 0, 0x0000); // NOP
		SetPackedRomWord(rom, 8, 0x0000); // NOP: keep the ISR active for one step.
		SetPackedRomWord(rom, 9, 0x2bff); // RETI
		if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian))
			return false;
		machine.Reset();
		machine.SetTimerCycleDivisor(1);
		machine.WriteByte(kCpuControl, kGlobalInterruptEnable);
		machine.WriteByte(kTimer1Reload, 1);
		machine.WriteByte(kTimer1Control, 0x18); // TMR1IE | T1EN

		for (int i = 0; i < 32 && machine.ReadByte(kStackPointer) == 0; ++i)
			machine.Next();
		if ((machine.ReadByte(kCpuControl) & kGlobalInterruptEnable) != 0 ||
			machine.ReadByte(kStackPointer) != 1 || (machine.PC() >> 1) != 9) {
			std::cerr << "interrupt entry state: CPUCON=" << static_cast<unsigned>(machine.ReadByte(kCpuControl))
				<< " STKPTR=" << static_cast<unsigned>(machine.ReadByte(kStackPointer))
				<< " PC=" << (machine.PC() >> 1) << '\n';
			return false;
		}

		machine.Next(); // Pending timer requests cannot nest; execute RETI instead.
		const bool returned = machine.ReadByte(kStackPointer) == 0 &&
			(machine.ReadByte(kCpuControl) & kGlobalInterruptEnable) != 0 &&
			(machine.PC() >> 1) < 8;
		if (!returned)
			std::cerr << "interrupt return state: CPUCON=" << static_cast<unsigned>(machine.ReadByte(kCpuControl))
				<< " STKPTR=" << static_cast<unsigned>(machine.ReadByte(kStackPointer))
				<< " PC=" << (machine.PC() >> 1) << '\n';
		return returned;
	}

	bool TablePointerSmoke() {
		constexpr uint8_t kAccumulator = 0x0a;
		constexpr uint8_t kTablePointerLow = 0x0b;
		constexpr uint8_t kTablePointerMid = 0x0c;
		constexpr uint8_t kTablePointerHigh = 0x0d;
		constexpr uint8_t kTableReadDestination = 0x80;
		constexpr uint8_t kChecksumHigh = 0x57;
		constexpr uint8_t kChecksumLow = 0x58;

		casioemu::ePSCPU machine;
		std::vector<uint8_t> rom(0x30000, 0);
		SetPackedRomWord(rom, 0, 0x2d80); // TBRD 1,80h: read then increment TABPTR.
		rom[0x2ffff] = 0x5a;
		if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian)) {
			std::cerr << "192 KiB packed ROM load failed\n";
			return false;
		}
		machine.Reset();
		machine.WriteByte(kTablePointerLow, 0xff);
		machine.WriteByte(kTablePointerMid, 0xff);
		machine.WriteByte(kTablePointerHigh, 0x02);
		if (machine.ReadByte(kTablePointerHigh) != 0x02) {
			std::cerr << "TABPTRH did not preserve bit 1\n";
			return false;
		}
		machine.Next();
		if (machine.ReadByte(kTableReadDestination) != 0x5a ||
			machine.ReadByte(kTablePointerLow) != 0 ||
			machine.ReadByte(kTablePointerMid) != 0 ||
			machine.ReadByte(kTablePointerHigh) != 3) {
			std::cerr << "upper table read mismatch: value=0x" << std::hex
				<< static_cast<unsigned>(machine.ReadByte(kTableReadDestination))
				<< " pointer=" << static_cast<unsigned>(machine.ReadByte(kTablePointerHigh))
				<< static_cast<unsigned>(machine.ReadByte(kTablePointerMid))
				<< static_cast<unsigned>(machine.ReadByte(kTablePointerLow)) << std::dec << '\n';
			return false;
		}

		casioemu::ePSCPU arithmetic_machine;
		std::vector<uint8_t> arithmetic_rom(0x20000, 0);
		SetPackedRomWord(arithmetic_rom, 0, 0x1158); // ADD 58h,A
		SetPackedRomWord(arithmetic_rom, 1, 0x240a); // CLR A; carry must be preserved.
		SetPackedRomWord(arithmetic_rom, 2, 0x1357); // ADC 57h,A
		if (!arithmetic_machine.LoadRom(arithmetic_rom, casioemu::Eps6800RomFormat::PackedLittleEndian))
			return false;
		arithmetic_machine.Reset();
		for (unsigned low = 0; low <= 0xff; ++low) {
			for (unsigned byte = 0; byte <= 0xff; ++byte) {
				arithmetic_machine.WriteByte(kChecksumLow, static_cast<uint8_t>(low));
				arithmetic_machine.WriteByte(kChecksumHigh, 0);
				arithmetic_machine.WriteByte(kAccumulator, static_cast<uint8_t>(byte));
				arithmetic_machine.SetPC(0);
				arithmetic_machine.Next();
				arithmetic_machine.Next();
				arithmetic_machine.Next();
				const unsigned expected = low + byte;
				if (arithmetic_machine.ReadByte(kChecksumLow) != (expected & 0xff) ||
					arithmetic_machine.ReadByte(kChecksumHigh) != (expected >> 8))
					return false;
			}
		}
		return true;
	}

	bool ArithmeticFlagsSmoke() {
		constexpr uint8_t kAccumulator = 0x0a;
		constexpr uint8_t kStatus = 0x0f;
		constexpr uint8_t kOperand = 0x80;
		constexpr uint8_t kResetHighBits = 0xc0;
		constexpr uint8_t kCarry = 0x01;
		constexpr uint8_t kDigitCarry = 0x02;
		constexpr uint8_t kZero = 0x04;
		constexpr uint8_t kOverflow = 0x08;
		constexpr uint8_t kSignedLessEqual = 0x10;
		constexpr uint8_t kSignedGreaterEqual = 0x20;

		const auto Run = [&](uint16_t instruction, uint8_t accumulator,
			uint8_t operand, uint8_t status) {
			casioemu::ePSCPU machine;
			std::vector<uint8_t> rom(0x20000, 0);
			SetPackedRomWord(rom, 0, instruction);
			if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian))
				return std::pair<uint8_t, uint8_t>{0xff, 0xff};
			machine.Reset();
			machine.WriteByte(kAccumulator, accumulator);
			machine.WriteByte(kOperand, operand);
			machine.WriteByte(kStatus, status);
			machine.Next();
			return std::pair<uint8_t, uint8_t>{
				machine.ReadByte(kAccumulator), machine.ReadByte(kStatus)};
		};

		const auto add_overflow = Run(0x4a01, 0x7f, 0, kResetHighBits);
		const auto add_zero = Run(0x4a01, 0xff, 0, kResetHighBits);
		const auto subtract_negative = Run(0x4c00, 0x01, 0, kResetHighBits);
		const auto subtract_borrow = Run(0x4d00, 0x01, 0, kResetHighBits);
		const auto increment_overflow = Run(0x1c80, 0, 0x7f, kResetHighBits);
		const auto decrement_negative = Run(0x1e80, 0, 0x00, kResetHighBits);
		const auto bcd_add = Run(0x1480, 0x01, 0x09, kResetHighBits);
		const auto bcd_subtract = Run(0x1a80, 0x01, 0x10, kResetHighBits | kCarry);

		return
			add_overflow == std::pair<uint8_t, uint8_t>{
				0x80, kResetHighBits | kDigitCarry | kOverflow | kSignedGreaterEqual} &&
			add_zero == std::pair<uint8_t, uint8_t>{
				0x00, kResetHighBits | kCarry | kDigitCarry | kZero |
				kSignedLessEqual | kSignedGreaterEqual} &&
			subtract_negative == std::pair<uint8_t, uint8_t>{
				0xff, kResetHighBits | kSignedLessEqual} &&
			subtract_borrow == std::pair<uint8_t, uint8_t>{
				0xfe, kResetHighBits | kSignedLessEqual} &&
			increment_overflow == std::pair<uint8_t, uint8_t>{
				0x80, kResetHighBits | kOverflow} &&
			decrement_negative == std::pair<uint8_t, uint8_t>{
				0xff, kResetHighBits} &&
			bcd_add == std::pair<uint8_t, uint8_t>{0x10, kResetHighBits | kDigitCarry} &&
			bcd_subtract == std::pair<uint8_t, uint8_t>{0x09, kResetHighBits | kCarry};
	}

	// F3: 0x0001 (HALT-like) sets STATUS TO/PD; F4: INC/DEC commit only C/Z/OV
	// and preserve DC/SLE/SGE. Reference behavior from ice.dll (EPS6800).
	bool HaltAndIncDecSmoke() {
		constexpr uint8_t kAccumulator = 0x0a;
		constexpr uint8_t kStatus = 0x0f;
		constexpr uint8_t kOperand = 0x80;
		constexpr uint8_t kCarry = 0x01;
		constexpr uint8_t kDigitCarry = 0x02;
		constexpr uint8_t kZero = 0x04;
		constexpr uint8_t kOverflow = 0x08;
		constexpr uint8_t kSignedLessEqual = 0x10;
		constexpr uint8_t kSignedGreaterEqual = 0x20;
		constexpr uint8_t kPowerDown = 0x40;
		constexpr uint8_t kTimerOverflow = 0x80;

		const auto Run = [&](uint16_t instruction, uint8_t accumulator,
			uint8_t operand, uint8_t status) {
			casioemu::ePSCPU machine;
			std::vector<uint8_t> rom(0x20000, 0);
			SetPackedRomWord(rom, 0, instruction);
			if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian))
				return std::pair<uint8_t, uint8_t>{0xff, 0xff};
			machine.Reset();
			machine.WriteByte(kAccumulator, accumulator);
			machine.WriteByte(kOperand, operand);
			machine.WriteByte(kStatus, status);
			machine.Next();
			return std::pair<uint8_t, uint8_t>{
				machine.ReadByte(kAccumulator), machine.ReadByte(kStatus)};
		};

		// 0x0001: sets TO and PD, leaves other STATUS bits intact, PC advances.
		const auto halt = Run(0x0001, 0, 0, kZero | kCarry);
		if (halt != std::pair<uint8_t, uint8_t>{0, kZero | kCarry | kPowerDown | kTimerOverflow}) {
			std::cerr << "0x0001 did not set STATUS TO/PD\n";
			return false;
		}
		// 0x0001 is not NOP: TO/PD must not be set for NOP.
		const auto nop = Run(0x0000, 0, 0, kZero | kCarry);
		if (nop != std::pair<uint8_t, uint8_t>{0, kZero | kCarry}) {
			std::cerr << "NOP must not set STATUS TO/PD\n";
			return false;
		}

		// INC A: 0x7f + 1 = 0x80: OV=1, C=0, Z=0; DC/SLE/SGE preserved.
		const auto inc_preserve = Run(0x1c80, 0, 0x7f,
			kDigitCarry | kSignedLessEqual | kSignedGreaterEqual);
		if (inc_preserve != std::pair<uint8_t, uint8_t>{
				0x80, kDigitCarry | kSignedLessEqual | kSignedGreaterEqual | kOverflow}) {
			std::cerr << "INC must not disturb DC/SLE/SGE\n";
			return false;
		}
		// INC A: 0xff + 1 = 0x00: C=1, Z=1.
		const auto inc_carry = Run(0x1c80, 0, 0xff, kDigitCarry);
		if (inc_carry != std::pair<uint8_t, uint8_t>{0x00, kDigitCarry | kCarry | kZero}) {
			std::cerr << "INC carry/zero flags mismatch\n";
			return false;
		}

		// DEC A: 0x00 - 1 = 0xff: C=0 (borrow), Z=0, OV=0; DC/SLE/SGE preserved.
		const auto dec_borrow = Run(0x1e80, 0, 0x00,
			kDigitCarry | kSignedLessEqual | kSignedGreaterEqual);
		if (dec_borrow != std::pair<uint8_t, uint8_t>{
				0xff, kDigitCarry | kSignedLessEqual | kSignedGreaterEqual}) {
			std::cerr << "DEC must not disturb DC/SLE/SGE\n";
			return false;
		}
		// DEC A: 0x01 - 1 = 0x00: C=1, Z=1.
		const auto dec_ok = Run(0x1e80, 0, 0x01, kDigitCarry);
		if (dec_ok != std::pair<uint8_t, uint8_t>{0x00, kDigitCarry | kCarry | kZero}) {
			std::cerr << "DEC carry/zero flags mismatch\n";
			return false;
		}
		return true;
	}

	// F5: reset register defaults per the reference ice.dll CIce::Reset
	// (EPS6800 branch). POSTID 0xF0 (incl. FSR2ID), DCRDE 0x33, CPUCON/PAWAKE
	// 0x10 (ROM word 12 bit 9 is clear on all supported models), STATUS 0xC0,
	// FSR1/FSR2 0x80.
	bool ResetValuesSmoke() {
		casioemu::ePSCPU machine;
		std::vector<uint8_t> rom(0x30000, 0);
		if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian)) {
			std::cerr << "reset ROM load failed\n";
			return false;
		}
		machine.Reset();
		const auto Check = [&](uint8_t addr, uint8_t expected, const char* name) {
			const uint8_t actual = machine.ReadByte(addr);
			if (actual != expected) {
				std::cerr << name << " reset mismatch: 0x" << std::hex
					<< static_cast<unsigned>(actual) << " != 0x"
					<< static_cast<unsigned>(expected) << std::dec << "\n";
				return false;
			}
			return true;
		};
		return Check(0x21, 0xf0, "POSTID") &&
			Check(0x3f, 0x33, "DCRDE") &&
			Check(0x20, 0x10, "CPUCON") &&
			Check(0x34, 0x10, "PAWAKE") &&
			Check(0x0f, 0xc0, "STATUS") &&
			Check(0x04, 0x80, "FSR1") &&
			Check(0x11, 0x80, "FSR2");
	}

	void DumpLcdAscii(casioemu::ePSCPU& machine, const char* label) {
		std::array<uint8_t, kLcdSize> lcd{};
		machine.CopyLcd(lcd.data(), lcd.size());
		const auto frame = casioemu::DecodeEps6800Display(lcd.data(), lcd.size());
		std::cout << "--- " << label << " ---\n";
		for (size_t y = 0; y < casioemu::EPS6800_LCD_PIXEL_HEIGHT; ++y) {
			for (size_t x = 0; x < casioemu::EPS6800_LCD_WIDTH; ++x)
				std::cout << (frame.pixels[y * casioemu::EPS6800_LCD_WIDTH + x] ? '#' : ' ');
			std::cout << '\n';
		}
		std::cout << "status:";
		for (size_t i = 0; i < casioemu::EPS6800_STATUS_SIZE; ++i)
			std::cout << ' ' << std::hex << std::setw(2) << std::setfill('0')
			<< static_cast<unsigned>(frame.status[i]);
		std::cout << std::dec << '\n';
	}

	// F-789SGA functional check: boot the real ROM, run 1+2=, and dump the
	// LCD as ASCII art plus hashes. The golden values are locked once verified.
	bool F789SgaFunctionalSmoke(const char* rom_path) {
		const auto rom = ReadRom(rom_path);
		if (rom.size() != 0x30000) {
			std::cerr << "F-789SGA ROM size mismatch: " << rom.size() << "\n";
			return false;
		}
		casioemu::ePSCPU machine;
		if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian)) {
			std::cerr << "F-789SGA ROM load failed\n";
			return false;
		}
		machine.Reset();
		// config.json: port_c_input_mask 0x0F, value 0x00 (model select).
		machine.SetPortCInput(0x0f, 0x00);
		machine.SetIceTimerScheduling(true);
		const auto State = [&](const char* label) {
			std::cout << label << ": pc=0x" << std::hex << machine.ProgramCounter()
				<< " lcdcon=0x" << static_cast<unsigned>(machine.ReadByte(0x2e))
				<< " lcdarl=0x" << static_cast<unsigned>(machine.ReadByte(0x22))
				<< " paintsta=0x" << static_cast<unsigned>(machine.ReadByte(0x36))
				<< " painten=0x" << static_cast<unsigned>(machine.ReadByte(0x35))
				<< " porta=0x" << static_cast<unsigned>(machine.ReadByte(0x31))
				<< " dcra=0x" << static_cast<unsigned>(machine.ReadByte(0x33))
				<< " stbcon=0x" << static_cast<unsigned>(machine.ReadByte(0x30)) << std::dec << '\n';
		};
		machine.OnDown();
		for (int i = 0; i < 30; ++i)
			machine.RunFrame();
		machine.OnUp();
		for (int i = 0; i < 300; ++i)
			machine.RunFrame();
		State("F789SGA boot");
		DumpLcdAscii(machine, "F789SGA boot");

		const auto HashLcd = [&](casioemu::ePSCPU& m) {
			std::array<uint8_t, kLcdSize> lcd{};
			m.CopyLcd(lcd.data(), lcd.size());
			return Fnv1a(lcd.data(), lcd.size());
		};
		const uint32_t boot_hash = HashLcd(machine);

		// Press and hold '1' for a long time, then release.
		machine.KeyDown(0); // '1' (kiko 0: ko=0, ki=0)
		for (int i = 0; i < 10; ++i)
			machine.RunFrame();
		State("while 1 held");
		machine.KeyUp(0);
		for (int i = 0; i < 20; ++i)
			machine.RunFrame();
		State("after 1");

		const auto TapLong = [&](uint8_t key) {
			machine.KeyDown(key);
			for (int i = 0; i < 8; ++i)
				machine.RunFrame();
			machine.KeyUp(key);
			for (int i = 0; i < 20; ++i)
				machine.RunFrame();
		};
		TapLong(11); // '+'
		TapLong(1);  // '2'
		TapLong(6);  // '='
		for (int i = 0; i < 40; ++i)
			machine.RunFrame();
		State("after 1+2=");
		DumpLcdAscii(machine, "F789SGA after 1+2=");
		const uint32_t result_hash = HashLcd(machine);

		std::cout << "F789SGA boot_lcd_hash=0x" << std::hex << boot_hash
			<< " result_lcd_hash=0x" << result_hash << std::dec << '\n';
		/* Golden values locked from the verified run: boot shows the idle
		 * display, 1+2= produces the computed result glyphs. */
		if (boot_hash != 0xdf402aebu || result_hash != 0xd8ca5010u) {
			std::cerr << "F-789SGA 1+2= golden LCD regression\n";
			return false;
		}
		return true;
	}

	bool PortCInputSmoke() {
		constexpr uint8_t kPortC = 0x3a;
		constexpr uint8_t kDirectionC = 0x3c;
		casioemu::ePSCPU machine;
		machine.SetPortCInput(0x0f, 0x0a);
		machine.Reset();
		machine.WriteByte(kPortC, 0xf0);
		machine.WriteByte(kDirectionC, 0x0f);
		if (machine.ReadByte(kPortC) != 0xfa)
			return false;
		machine.WriteByte(kDirectionC, 0x00);
		return machine.ReadByte(kPortC) == 0xf0;
	}

	bool HookAndRamSmoke() {
		std::vector<uint8_t> rom(0x20000, 0);
		SetPackedRomWord(rom, 0, 0x4e5a); // MOV A,#5Ah
		SetPackedRomWord(rom, 1, 0x2180); // MOV 80h,A
		SetPackedRomWord(rom, 2, 0xe004); // SCALL 0004h
		SetPackedRomWord(rom, 3, 0x0000);
		SetPackedRomWord(rom, 4, 0x2bfe); // RET

		casioemu::ePSCPU machine;
		if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian))
			return false;
		int instructions = 0;
		int calls = 0;
		int returns = 0;
		int writes = 0;
		uint8_t maximum_stack_pointer = 0;
		bool saw_backtrace = false;
		machine.SetDebugHooks(
			[&](uint32_t, uint32_t, uint8_t stack_pointer) {
				++instructions;
				maximum_stack_pointer = std::max(maximum_stack_pointer, stack_pointer);
				return stack_pointer == 1;
			},
			[&](uint32_t, uint32_t, bool call, uint32_t accumulator, const std::string& backtrace) {
				(call ? calls : returns)++;
				saw_backtrace |= call && accumulator == 0x5a && !backtrace.empty();
			},
			[&](uint32_t address, uint8_t&, bool write) {
				if (write && address == 0x80) {
					++writes;
					return true; // Address-lock semantics: cancel before the physical write.
				}
				return false;
			},
			{});
		machine.Reset();
		for (int i = 0; i < 5; ++i)
			machine.Next();
		if (instructions != 5 || calls != 1 || returns != 1 || writes != 1 || maximum_stack_pointer != 1 ||
			machine.LastDebugStop().reason != casioemu::Eps6800DebugStopReason::Hook ||
			!saw_backtrace || machine.ReadDebugMemory(0x80) != 0)
			return false;

		machine.WriteDebugMemory(0x20, 0x80); // CPUCON.WBK
		machine.WriteDebugMemory(0x25, 0x5a); // First WBK-backed RAM byte.
		auto ram = machine.ExportRam();
		constexpr size_t kBankRamSize = 8192;
		constexpr size_t kWbkRamSize = 27;
		constexpr size_t kPersistentRamSize = kBankRamSize + kWbkRamSize + 0x0d + 0x40;
		if (ram.size() != kPersistentRamSize || ram[kBankRamSize] != 0x5a ||
			machine.ImportRam(std::vector<uint8_t>(1, 0)))
			return false;
		ram.front() = 0xa5;
		ram[kBankRamSize - 1] = 0x3c;
		ram[kBankRamSize] = 0x5a;
		ram[kBankRamSize + kWbkRamSize - 1] = 0x6b;
		machine.WriteDebugMemory(0x25, 0);
		machine.WriteDebugMemory(0x3f, 0);
		if (!machine.ImportRam(ram) || machine.ReadDebugMemory(0x80) != 0xa5 ||
			machine.ReadDebugMemory(0x207f) != 0x3c || machine.ReadDebugMemory(0x25) != 0x5a ||
			machine.ReadDebugMemory(0x3f) != 0x6b)
			return false;

		// RAM files produced by older builds contain only the 8 KiB banked area.
		ram.resize(kBankRamSize);
		return machine.ImportRam(ram);
	}

	bool DebuggerSmoke() {
		std::vector<uint8_t> rom(0x40000, 0);
		SetRomWord(rom, 0, 0x4e12); // MOV A,#12h
		SetRomWord(rom, 1, 0x0000); // NOP
		SetRomWord(rom, 2, 0xe004); // SCALL 0004h
		SetRomWord(rom, 3, 0x0000); // NOP (return address)
		SetRomWord(rom, 4, 0x4e34); // MOV A,#34h
		SetRomWord(rom, 5, 0x2bfe); // RET
		SetRomWord(rom, 6, 0x4e5a); // MOV A,#5Ah
		SetRomWord(rom, 7, 0x2180); // MOV 80h,A (bank 0 RAM)
		SetRomWord(rom, 8, 0x0000); // NOP

		casioemu::ePSCPU machine;
		if (!Check(machine.LoadRom(rom, casioemu::Eps6800RomFormat::UnpackedNibbles) &&
				machine.ReadCodeWord(2) == 0xe004, "unpacked ROM load/word decode", __LINE__))
			return false;
		std::vector<uint8_t> packed_rom(0x20000, 0);
		SetPackedRomWord(packed_rom, 2, 0xe004);
		casioemu::ePSCPU packed_machine;
		if (!Check(packed_machine.LoadRom(packed_rom, casioemu::Eps6800RomFormat::PackedLittleEndian) &&
				packed_machine.ReadCodeWord(2) == 0xe004 &&
				!packed_machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian),
				"packed ROM load/word decode/reject mismatch", __LINE__))
			return false;

		machine.Reset();
		machine.AddExecutionBreakpoint(1);
		machine.RequestContinue();
		if (!Check(machine.RunFrame(), "execution breakpoint run", __LINE__))
			return false;
		auto stop = machine.LastDebugStop();
		auto snapshot = machine.DebugSnapshot();
		if (!Check(stop.reason == casioemu::Eps6800DebugStopReason::ExecutionBreakpoint &&
				stop.program_counter == 1 && snapshot.program_counter == 1 && snapshot.registers[0x0a] == 0x12,
				"execution breakpoint stop state", __LINE__))
			return false;
		casioemu::Eps6800ExecutionBreakpoint skipped_breakpoint{};
		skipped_breakpoint.address = 1;
		skipped_breakpoint.skip_count = 1;
		if (!Check(machine.ConfigureExecutionBreakpoint(skipped_breakpoint), "configure skip breakpoint", __LINE__))
			return false;
		machine.SetPC(0);
		machine.RequestContinue();
		if (!Check(!machine.RunFrame(), "skip-count run must not stop", __LINE__))
			return false;
		machine.SetPC(0);
		machine.RequestContinue();
		if (!Check(machine.RunFrame() &&
				machine.LastDebugStop().reason == casioemu::Eps6800DebugStopReason::ExecutionBreakpoint &&
				machine.ExecutionBreakpointDetails().front().hit_count == 2,
				"skip-count second hit", __LINE__))
			return false;

		machine.RequestStepInto();
		if (!Check(machine.RunFrame() &&
				machine.LastDebugStop().reason == casioemu::Eps6800DebugStopReason::Step &&
				machine.ProgramCounter() == 2, "step_into stop state", __LINE__))
			return false;

		machine.RequestStepOver();
		if (!Check(machine.RunFrame() &&
				machine.LastDebugStop().reason == casioemu::Eps6800DebugStopReason::StepOver &&
				machine.ProgramCounter() == 3 && machine.DebugSnapshot().stack_pointer == 0 &&
				machine.DebugSnapshot().registers[0x0a] == 0x34, "step_over stop state", __LINE__))
			return false;

		machine.SetPC(2);
		machine.RequestStepInto();
		if (!Check(machine.RunFrame() && machine.ProgramCounter() == 4 &&
				machine.DebugSnapshot().stack_pointer == 1, "SCALL step_into state", __LINE__))
			return false;
		if (!Check(machine.RequestStepOut() && machine.RunFrame() &&
				machine.LastDebugStop().reason == casioemu::Eps6800DebugStopReason::StepOut &&
				machine.ProgramCounter() == 3 && machine.DebugSnapshot().stack_pointer == 0,
				"step_out stop state", __LINE__))
			return false;

		if (!Check(machine.WriteDebugMemory(0x0f, 0xa5) && machine.ReadDebugMemory(0x0f) == 0xa5 &&
				machine.WriteDebugMemory(0x80, 0x5a) && machine.ReadDebugMemory(0x80) == 0x5a &&
				machine.WriteDebugMemory(0x207f, 0xc3) && machine.ReadDebugMemory(0x207f) == 0xc3 &&
				!machine.WriteDebugMemory(0x2080, 0xff), "debug memory bounds", __LINE__))
			return false;

		if (!Check(machine.WriteCodeWord(6, 0x4e56) && machine.ReadCodeWord(6) == 0x4e56,
				"code word write/read", __LINE__))
			return false;
		machine.WriteCodeWord(6, 0x4e5a);
		casioemu::Eps6800MemoryBreakpoint memory_breakpoint{};
		memory_breakpoint.address = 0x80;
		memory_breakpoint.write = true;
		memory_breakpoint.break_when_hit = true;
		memory_breakpoint.compare_data = true;
		memory_breakpoint.data = 0x50;
		memory_breakpoint.mask = 0xf0;
		if (!Check(machine.AddMemoryBreakpoint(memory_breakpoint), "add conditional memory breakpoint", __LINE__))
			return false;
		machine.SetPC(6);
		machine.RequestContinue();
		if (!Check(machine.RunFrame(), "memory breakpoint run", __LINE__))
			return false;
		stop = machine.LastDebugStop();
		const auto memory_hits = machine.MemoryBreakpointHits(0x80, true);
		if (!Check(stop.reason == casioemu::Eps6800DebugStopReason::MemoryBreakpoint &&
				stop.program_counter == 8 && stop.memory_address == 0x80 &&
				stop.memory_value == 0x5a && stop.memory_write && memory_hits.size() == 1 &&
				machine.ReadDebugMemory(0x80) == 0x5a, "conditional memory breakpoint stop", __LINE__))
			return false;
		machine.SetPC(6);
		machine.RequestContinue(false);
		if (!Check(!machine.RunFrame() && machine.MemoryBreakpointHits(0x80, true).size() == 2,
				"free run records memory accesses", __LINE__))
			return false; // Free Run records accesses but ignores break controls.
		machine.ClearMemoryBreakpoints();

		machine.EnableTrace(true);
		machine.SetTraceCapacity(2);
		machine.SetPC(0);
		machine.RequestStepInto();
		machine.RunFrame();
		machine.RequestStepInto();
		machine.RunFrame();
		machine.RequestStepInto();
		machine.RunFrame();
		const auto trace = machine.TraceBuffer();
		return Check(trace.size() == 2 && trace.back().program_counter == 2 &&
				trace.back().next_program_counter == 4, "trace buffer tail", __LINE__);
	}

	bool StatusEquals(const casioemu::Eps6800DisplayFrame& frame,
		std::initializer_list<uint8_t> expected) {
		return expected.size() == frame.status.size() &&
			std::equal(expected.begin(), expected.end(), frame.status.begin());
	}
}

int main(int argc, char** argv) {
	if (argc > 3) {
		std::cerr << "usage: Eps6800AdapterSmoke [<hp300s-rom.bin> [<f789sga-rom.bin>]]\n";
		return 2;
	}
	if (!DebuggerSmoke()) {
		std::cerr << "EPS6800 debugger smoke regression\n";
		return 1;
	}
	if (!ArithmeticFlagsSmoke()) {
		std::cerr << "EPS6800 arithmetic flags regression\n";
		return 1;
	}
	if (!HaltAndIncDecSmoke()) {
		std::cerr << "EPS6800 HALT/INC-DEC flags regression\n";
		return 1;
	}
	if (!ResetValuesSmoke()) {
		std::cerr << "EPS6800 reset values regression\n";
		return 1;
	}
	if (!PortCInputSmoke()) {
		std::cerr << "EPS6800 Port C external input regression\n";
		return 1;
	}
	if (!KeyboardMatrixSmoke()) {
		std::cerr << "EPS6800 keyboard matrix/ghosting regression\n";
		return 1;
	}
	if (!TimerSmoke()) {
		std::cerr << "EPS6800 timer regression\n";
		return 1;
	}
	if (!RepeatInterruptDeferralSmoke()) {
		std::cerr << "EPS6800 repeat/interrupt deferral regression\n";
		return 1;
	}
	if (!InterruptEntryMasksGlobalInterruptSmoke()) {
		std::cerr << "EPS6800 interrupt entry masking regression\n";
		return 1;
	}
	if (!TablePointerSmoke()) {
		std::cerr << "EPS6800 table pointer width regression\n";
		return 1;
	}
	if (!HookAndRamSmoke()) {
		std::cerr << "EPS6800 hook/RAM persistence regression\n";
		return 1;
	}
	if (argc < 2) {
		std::cout << "EPS6800 synthetic checks passed (no golden ROM provided; "
			"skipping golden/status/LCD scenarios).\n";
		return 0;
	}

	auto rom = ReadRom(argv[1]);
	if (rom.size() != kRomSize) {
		std::cerr << "ROM size mismatch: " << rom.size() << "\n";
		return 2;
	}

	std::array<uint8_t, casioemu::EPS6800_LCD_RAW_SIZE> synthetic{};
	synthetic[3 * 96 + 9] = 0x80;
	synthetic[3 * 96 + 0] = 0x40;
	synthetic[0] = 0x01;
	const auto synthetic_frame = casioemu::DecodeEps6800Display(synthetic.data(), synthetic.size());
	if (synthetic_frame.status[1] != 0x02 || synthetic_frame.pixels[0] != 1 ||
		synthetic_frame.pixels[30 * 96] != 1) {
		std::cerr << "LCD/status row mapping mismatch\n";
		return 1;
	}

	float previous_alpha = -1.0f;
	for (uint8_t contrast = 0; contrast <= casioemu::EPS6800_CONTRAST_MAX; ++contrast) {
		const float mapped = casioemu::Eps6800AsEspContrast(contrast);
		const float alpha = casioemu::Eps6800ActiveAlpha(contrast);
		const float expected = std::max(0.0f, -240.0f + mapped * 28.0f - 3.0f * 8.0f);
		if (std::abs(alpha - expected) > 0.001f ||
			(contrast == casioemu::EPS6800_CONTRAST_MAX &&
				std::abs(mapped - 31.0f) > 0.001f) ||
			(contrast != 0 && alpha < previous_alpha)) {
			std::cerr << "LCD contrast curve regression\n";
			return 1;
		}
		previous_alpha = alpha;
	}
	if (!(casioemu::Eps6800InactiveAlpha(8) < casioemu::Eps6800ActiveAlpha(8))) {
		std::cerr << "LCD inactive-pixel contrast regression\n";
		return 1;
	}

	casioemu::ePSCPU register_machine;
	register_machine.Reset();
	std::array<uint8_t, kLcdSize> register_lcd{};
	for (uint8_t contrast = 0; contrast <= casioemu::EPS6800_CONTRAST_MAX; ++contrast) {
		const uint8_t lcdarh = static_cast<uint8_t>((contrast << 4) | 0x03);
		register_machine.WriteByte(0x23, lcdarh);
		register_machine.WriteByte(0x2e, 0x20);
		casioemu::Eps6800LcdControl control{};
		register_machine.CopyLcd(register_lcd.data(), register_lcd.size(), &control);
		if (control.lcdarh != lcdarh || control.lcdcon != 0x20 ||
			control.contrast != contrast || !control.visible()) {
			std::cerr << "LCD control decode mismatch\n";
			return 1;
		}
	}
	register_machine.WriteByte(0x2e, 0x60);
	if (CaptureControl(register_machine).visible()) {
		std::cerr << "LCD BLANK bit did not blank display\n";
		return 1;
	}
	register_machine.WriteByte(0x2e, 0x00);
	if (CaptureControl(register_machine).visible()) {
		std::cerr << "LCDON clear did not disable display\n";
		return 1;
	}

	casioemu::ePSCPU machine;
	if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian)) {
		std::cerr << "ROM load failed\n";
		return 2;
	}
	machine.Reset();
	for (uint32_t i = 0; i < 200000; ++i)
		machine.Next();

	std::array<uint8_t, kLcdSize> lcd{};
	if (machine.CopyLcd(lcd.data(), lcd.size()) != lcd.size()) {
		std::cerr << "LCD copy failed\n";
		return 2;
	}
	const uint32_t hash = Fnv1a(lcd.data(), lcd.size());
	const auto display = casioemu::DecodeEps6800Display(lcd.data(), lcd.size());
	const uint32_t pc = machine.PC() >> 1;
	const auto golden_snapshot = machine.DebugSnapshot();
	const uint8_t golden_acc = golden_snapshot.registers[0x0a];
	const uint8_t golden_status = golden_snapshot.registers[0x0f];
	if (pc != 0x00018c || golden_acc != 0x20 || golden_status != 0xd4 || hash != 0x1b8852c5) {
		std::cerr << std::hex << std::setfill('0')
			<< "golden mismatch: pc=0x" << std::setw(6) << pc
			<< " acc=0x" << std::setw(2) << static_cast<unsigned>(golden_acc)
			<< " status=0x" << std::setw(2) << static_cast<unsigned>(golden_status)
			<< " lcd=0x" << std::setw(8) << hash << "\n";
		return 1;
	}
	DumpLcdAscii(machine, "HP300S+ boot (golden)");

	const auto control_before_snapshot = CaptureControl(machine);
	std::stringstream snapshot(std::ios::in | std::ios::out | std::ios::binary);
	machine.SaveState(snapshot);
	machine.WriteByte(0x23, 0xf3);
	machine.WriteByte(0x2e, 0x60);
	for (uint32_t i = 0; i < 4096; ++i)
		machine.Next();
	snapshot.seekg(0);
	machine.LoadState(snapshot);
	lcd.fill(0);
	machine.CopyLcd(lcd.data(), lcd.size());
	const auto control_after_snapshot = CaptureControl(machine);
	if ((machine.PC() >> 1) != pc || Fnv1a(lcd.data(), lcd.size()) != hash ||
		control_after_snapshot.lcdarh != control_before_snapshot.lcdarh ||
		control_after_snapshot.lcdcon != control_before_snapshot.lcdcon) {
		std::cerr << "snapshot round-trip mismatch\n";
		return 1;
	}

	std::cout << std::hex << std::setfill('0')
		<< "EPS6800 adapter OK: pc=0x" << std::setw(6) << pc
		<< " acc=0x" << std::setw(2) << static_cast<unsigned>(golden_acc)
		<< " status=0x" << std::setw(2) << static_cast<unsigned>(golden_status)
		<< " lcd_fnv1a=0x" << std::setw(8) << hash
		<< " status_bus=";
	for (const auto value : display.status)
		std::cout << std::setw(2) << static_cast<unsigned>(value);

	// The HP indicator row has its own physical bit layout. Exercise independent
	// cold boots so persistent calculator settings cannot leak across scenarios.
	bool rom_load_failed = false;
	const auto Scenario = [&](std::initializer_list<uint8_t> keys) {
		casioemu::ePSCPU scenario_machine;
		if (!scenario_machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian)) {
			rom_load_failed = true;
			return Capture(scenario_machine);
		}
		Boot(scenario_machine);
		for (const auto key : keys)
			Tap(scenario_machine, key);
		return Capture(scenario_machine);
	};
	const bool status_ok =
		StatusEquals(Scenario({}), {0, 0, 0, 0, 0, 0, 0x80, 0, 0, 0, 0x01, 0}) &&
		StatusEquals(Scenario({46}), {0x01, 0, 0, 0, 0, 0, 0x80, 0, 0, 0, 0x01, 0}) &&
		StatusEquals(Scenario({54}), {0x08, 0, 0, 0, 0, 0, 0x80, 0, 0, 0, 0x01, 0}) &&
		StatusEquals(Scenario({50, 13, 5}), {0, 0, 0, 0x02, 0, 0, 0x80, 0, 0, 0, 0, 0}) &&
		StatusEquals(Scenario({46, 50, 4}), {0, 0, 0, 0, 0, 0, 0, 0x04, 0, 0, 0x01, 0}) &&
		StatusEquals(Scenario({46, 50, 12}), {0, 0, 0, 0, 0, 0, 0, 0x20, 0, 0, 0x01, 0}) &&
		StatusEquals(Scenario({46, 50, 20, 13}), {0, 0, 0, 0, 0, 0, 0x80, 0, 0x02, 0, 0x01, 0}) &&
		StatusEquals(Scenario({46, 50, 3, 13}), {0, 0, 0, 0, 0, 0, 0x80, 0, 0x80, 0, 0x01, 0}) &&
		StatusEquals(Scenario({2}), {0, 0, 0x04, 0, 0, 0, 0x80, 0, 0, 0, 0x01, 0}) &&
		StatusEquals(Scenario({46, 2}), {0, 0x10, 0, 0, 0, 0, 0x80, 0, 0, 0, 0x01, 0}) &&
		StatusEquals(Scenario({5, 38}), {0, 0, 0, 0, 0, 0, 0x80, 0, 0, 0, 0x81, 0}) &&
		StatusEquals(Scenario({5, 38, 13, 38, 49}), {0, 0, 0, 0, 0, 0, 0x80, 0, 0, 0, 0x11, 0}) &&
		StatusEquals(Scenario({50, 45}), {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80});
	if (rom_load_failed) {
		std::cerr << "Scenario ROM load failed\n";
		return 2;
	}
	if (!status_ok) {
		std::cerr << "HP 300S+ status mapping regression\n";
		return 1;
	}

	casioemu::ePSCPU diagnostic_machine;
	if (!diagnostic_machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian)) {
		std::cerr << "Diagnostic ROM load failed\n";
		return 2;
	}
	Boot(diagnostic_machine);
	bool diagnostic_armed = false;
	diagnostic_machine.SetDebugHooks(
		[&](uint32_t pc_before, uint32_t, uint8_t) {
			// ROM 0x01D8 stores mode 1 after all three SHIFT+7+ON
			// electrical scans have matched.
			diagnostic_armed |= pc_before == 0x01d8;
			return false;
		},
		{}, {}, {});
	diagnostic_machine.KeyDown(46); // SHIFT: PB5-PA6
	diagnostic_machine.KeyDown(3);  // 7: PB0-PA3
	for (int i = 0; i < 4; ++i)
		diagnostic_machine.RunFrame();
	diagnostic_machine.OnDown();
	for (int i = 0; i < 12; ++i)
		diagnostic_machine.RunFrame();
	diagnostic_machine.OnUp();
	diagnostic_machine.KeyUp(3);
	diagnostic_machine.KeyUp(46);
	for (int i = 0; i < 12; ++i)
		diagnostic_machine.RunFrame();
	if (!diagnostic_armed) {
		std::cerr << "HP 300S+ ROM did not accept SHIFT+7+ON diagnostic entry\n";
		return 1;
	}

	std::cout << " snapshot=ok status_map=ok keyboard_matrix=ok diagnostic=ok debugger=ok hooks=ok ram=ok";

	casioemu::ePSCPU lcd_control_machine;
	if (!lcd_control_machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian)) {
		std::cerr << "ROM load failed\n";
		return 2;
	}
	Boot(lcd_control_machine);
	const auto boot_control = CaptureControl(lcd_control_machine);
	if (boot_control.lcdarh != 0x83 || boot_control.lcdcon != 0xa1 ||
		boot_control.contrast != 8 || !boot_control.visible()) {
		std::cerr << "HP 300S+ boot LCD control mismatch\n";
		return 1;
	}
	Tap(lcd_control_machine, 46);
	Tap(lcd_control_machine, 35, 100);
	const auto off_control = CaptureControl(lcd_control_machine);
	if (off_control.lcdarh != 0x83 || off_control.lcdcon != 0x00 || off_control.visible()) {
		std::cerr << "HP 300S+ SHIFT+AC LCD shutdown mismatch\n";
		return 1;
	}

	casioemu::ePSCPU contrast_machine;
	if (!contrast_machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedLittleEndian)) {
		std::cerr << "ROM load failed\n";
		return 2;
	}
	Boot(contrast_machine);
	Tap(contrast_machine, 46);
	Tap(contrast_machine, 50);
	Tap(contrast_machine, 45);
	Tap(contrast_machine, 20);
	Tap(contrast_machine, 53);
	Tap(contrast_machine, 48);
	Tap(contrast_machine, 48);
	const auto adjusted_control = CaptureControl(contrast_machine);
	if (adjusted_control.contrast <= boot_control.contrast || !adjusted_control.visible()) {
		std::cerr << "HP 300S+ contrast menu did not increase LCD contrast\n";
		return 1;
	}

	std::cout << " lcd_control=ok contrast=0x"
		<< static_cast<unsigned>(adjusted_control.contrast) << "\n";

	if (argc >= 3) {
		if (!F789SgaFunctionalSmoke(argv[2])) {
			std::cerr << "F-789SGA functional regression\n";
			return 1;
		}
	}
	return 0;
}
