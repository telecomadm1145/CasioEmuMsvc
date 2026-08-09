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
#include <vector>

namespace {
	constexpr size_t kRomSize = 0x40000;
	constexpr size_t kLcdSize = 96 * 4;

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
		rom[offset] = static_cast<uint8_t>(word >> 8);
		rom[offset + 1] = static_cast<uint8_t>(word);
	}

	bool KeyboardMatrixSmoke() {
		constexpr uint8_t kPortA = 0x31;
		constexpr uint8_t kDirectionA = 0x33;
		constexpr uint8_t kPortB = 0x37;
		constexpr uint8_t kDirectionB = 0x39;

		const auto Scan = [&](std::initializer_list<uint8_t> keys, uint8_t selected_pb,
			bool on = false) {
			casioemu::ePSCPU machine;
			std::vector<uint8_t> rom(0x20000, 0);
			if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedBigEndian))
				return static_cast<uint8_t>(0);
			machine.Reset();
			machine.WriteByte(kDirectionA, 0xff); // PA inputs with pull-ups.
			machine.WriteByte(kDirectionB, 0x00); // PB scan outputs.
			machine.WriteByte(kPortB, static_cast<uint8_t>(~(1u << selected_pb)));
			for (const auto key : keys)
				machine.KeyDown(key);
			if (on)
				machine.OnDown();
			for (int i = 0; i < 1100; ++i)
				machine.Next();
			return machine.ReadByte(kPortA);
		};

		return
			Scan({0, 1}, 0) == 0xfc &&                 // same PB row
			Scan({0, 8}, 0) == 0xfe &&                 // same PA column, PB0
			Scan({0, 8}, 1) == 0xfe &&                 // same PA column, PB1
			Scan({0, 1, 8}, 1) == 0xfc &&              // 3-key rectangle ghosts PA1
			Scan({0, 1, 8, 9}, 1) == 0xfc &&           // four physical corners
			Scan({0}, 0, true) == 0x7e;                // independent ON plus matrix key
	}

	bool HookAndRamSmoke() {
		std::vector<uint8_t> rom(0x20000, 0);
		SetPackedRomWord(rom, 0, 0x4e5a); // MOV A,#5Ah
		SetPackedRomWord(rom, 1, 0x2180); // MOV 80h,A
		SetPackedRomWord(rom, 2, 0xe004); // SCALL 0004h
		SetPackedRomWord(rom, 3, 0x0000);
		SetPackedRomWord(rom, 4, 0x2bfe); // RET

		casioemu::ePSCPU machine;
		if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedBigEndian))
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

		auto ram = machine.ExportRam();
		if (ram.size() != 8192 || machine.ImportRam(std::vector<uint8_t>(1, 0)))
			return false;
		ram.front() = 0xa5;
		ram.back() = 0x3c;
		return machine.ImportRam(ram) && machine.ReadDebugMemory(0x80) == 0xa5 &&
			machine.ReadDebugMemory(0x207f) == 0x3c;
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
		if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::UnpackedNibbles) ||
			machine.ReadCodeWord(2) != 0xe004)
			return false;
		std::vector<uint8_t> packed_rom(0x20000, 0);
		SetPackedRomWord(packed_rom, 2, 0xe004);
		casioemu::ePSCPU packed_machine;
		if (!packed_machine.LoadRom(packed_rom, casioemu::Eps6800RomFormat::PackedBigEndian) ||
			packed_machine.ReadCodeWord(2) != 0xe004 ||
			packed_machine.LoadRom(rom, casioemu::Eps6800RomFormat::PackedBigEndian))
			return false;

		machine.Reset();
		machine.AddExecutionBreakpoint(1);
		machine.RequestContinue();
		if (!machine.RunFrame())
			return false;
		auto stop = machine.LastDebugStop();
		auto snapshot = machine.DebugSnapshot();
		if (stop.reason != casioemu::Eps6800DebugStopReason::ExecutionBreakpoint ||
			stop.program_counter != 1 || snapshot.program_counter != 1 || snapshot.registers[0x0a] != 0x12)
			return false;
		casioemu::Eps6800ExecutionBreakpoint skipped_breakpoint{};
		skipped_breakpoint.address = 1;
		skipped_breakpoint.skip_count = 1;
		if (!machine.ConfigureExecutionBreakpoint(skipped_breakpoint))
			return false;
		machine.SetPC(0);
		machine.RequestContinue();
		if (machine.RunFrame())
			return false;
		machine.SetPC(0);
		machine.RequestContinue();
		if (!machine.RunFrame() || machine.LastDebugStop().reason != casioemu::Eps6800DebugStopReason::ExecutionBreakpoint ||
			machine.ExecutionBreakpointDetails().front().hit_count != 2)
			return false;

		machine.RequestStepInto();
		if (!machine.RunFrame() || machine.LastDebugStop().reason != casioemu::Eps6800DebugStopReason::Step ||
			machine.ProgramCounter() != 2)
			return false;

		machine.RequestStepOver();
		if (!machine.RunFrame() || machine.LastDebugStop().reason != casioemu::Eps6800DebugStopReason::StepOver ||
			machine.ProgramCounter() != 3 || machine.DebugSnapshot().stack_pointer != 0 ||
			machine.DebugSnapshot().registers[0x0a] != 0x34)
			return false;

		machine.SetPC(2);
		machine.RequestStepInto();
		if (!machine.RunFrame() || machine.ProgramCounter() != 4 || machine.DebugSnapshot().stack_pointer != 1)
			return false;
		if (!machine.RequestStepOut() || !machine.RunFrame() ||
			machine.LastDebugStop().reason != casioemu::Eps6800DebugStopReason::StepOut ||
			machine.ProgramCounter() != 3 || machine.DebugSnapshot().stack_pointer != 0)
			return false;

		if (!machine.WriteDebugMemory(0x0f, 0xa5) || machine.ReadDebugMemory(0x0f) != 0xa5 ||
			!machine.WriteDebugMemory(0x80, 0x5a) || machine.ReadDebugMemory(0x80) != 0x5a ||
			!machine.WriteDebugMemory(0x207f, 0xc3) || machine.ReadDebugMemory(0x207f) != 0xc3 ||
			machine.WriteDebugMemory(0x2080, 0xff))
			return false;

		if (!machine.WriteCodeWord(6, 0x4e56) || machine.ReadCodeWord(6) != 0x4e56)
			return false;
		machine.WriteCodeWord(6, 0x4e5a);
		casioemu::Eps6800MemoryBreakpoint memory_breakpoint{};
		memory_breakpoint.address = 0x80;
		memory_breakpoint.write = true;
		memory_breakpoint.break_when_hit = true;
		memory_breakpoint.compare_data = true;
		memory_breakpoint.data = 0x50;
		memory_breakpoint.mask = 0xf0;
		if (!machine.AddMemoryBreakpoint(memory_breakpoint))
			return false;
		machine.SetPC(6);
		machine.RequestContinue();
		if (!machine.RunFrame())
			return false;
		stop = machine.LastDebugStop();
		const auto memory_hits = machine.MemoryBreakpointHits(0x80, true);
		if (stop.reason != casioemu::Eps6800DebugStopReason::MemoryBreakpoint ||
			stop.program_counter != 8 || stop.memory_address != 0x80 ||
			stop.memory_value != 0x5a || !stop.memory_write || memory_hits.size() != 1 ||
			machine.ReadDebugMemory(0x80) != 0x5a)
			return false;
		machine.SetPC(6);
		machine.RequestContinue(false);
		if (machine.RunFrame() || machine.MemoryBreakpointHits(0x80, true).size() != 2)
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
		return trace.size() == 2 && trace.back().program_counter == 2 &&
			trace.back().next_program_counter == 4;
	}

	bool StatusEquals(const casioemu::Eps6800DisplayFrame& frame,
		std::initializer_list<uint8_t> expected) {
		return expected.size() == frame.status.size() &&
			std::equal(expected.begin(), expected.end(), frame.status.begin());
	}
}

int main(int argc, char** argv) {
	if (argc != 2) {
		std::cerr << "usage: Eps6800AdapterSmoke <rom.bin>\n";
		return 2;
	}
	if (!DebuggerSmoke()) {
		std::cerr << "EPS6800 debugger smoke regression\n";
		return 1;
	}
	if (!KeyboardMatrixSmoke()) {
		std::cerr << "EPS6800 keyboard matrix/ghosting regression\n";
		return 1;
	}
	if (!HookAndRamSmoke()) {
		std::cerr << "EPS6800 hook/RAM persistence regression\n";
		return 1;
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
	if (!machine.LoadRom(rom, casioemu::Eps6800RomFormat::UnpackedNibbles)) {
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
	if (pc != 0x00018c || golden_acc != 0x20 || golden_status != 0xc4 || hash != 0x1b8852c5) {
		std::cerr << std::hex << std::setfill('0')
			<< "golden mismatch: pc=0x" << std::setw(6) << pc
			<< " acc=0x" << std::setw(2) << static_cast<unsigned>(golden_acc)
			<< " status=0x" << std::setw(2) << static_cast<unsigned>(golden_status)
			<< " lcd=0x" << std::setw(8) << hash << "\n";
		return 1;
	}

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
	const auto Scenario = [&](std::initializer_list<uint8_t> keys) {
		casioemu::ePSCPU scenario_machine;
		scenario_machine.LoadRom(rom, casioemu::Eps6800RomFormat::UnpackedNibbles);
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
	if (!status_ok) {
		std::cerr << "HP 300S+ status mapping regression\n";
		return 1;
	}

	std::cout << " snapshot=ok status_map=ok keyboard_matrix=ok debugger=ok hooks=ok ram=ok";

	casioemu::ePSCPU lcd_control_machine;
	lcd_control_machine.LoadRom(rom, casioemu::Eps6800RomFormat::UnpackedNibbles);
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
	contrast_machine.LoadRom(rom, casioemu::Eps6800RomFormat::UnpackedNibbles);
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
	return 0;
}
