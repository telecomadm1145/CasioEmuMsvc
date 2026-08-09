#include "ePSCpu.h"
#include "Eps6800Display.h"

#include <algorithm>
#include <array>
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

	casioemu::ePSCPU machine;
	if (!machine.LoadRom(rom)) {
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
	if (pc != 0x00018c || machine.ACC != 0x20 || machine.STATUS != 0xc4 || hash != 0x1b8852c5) {
		std::cerr << std::hex << std::setfill('0')
			<< "golden mismatch: pc=0x" << std::setw(6) << pc
			<< " acc=0x" << std::setw(2) << static_cast<unsigned>(machine.ACC)
			<< " status=0x" << std::setw(2) << static_cast<unsigned>(machine.STATUS)
			<< " lcd=0x" << std::setw(8) << hash << "\n";
		return 1;
	}

	std::stringstream snapshot(std::ios::in | std::ios::out | std::ios::binary);
	machine.SaveState(snapshot);
	for (uint32_t i = 0; i < 4096; ++i)
		machine.Next();
	snapshot.seekg(0);
	machine.LoadState(snapshot);
	lcd.fill(0);
	machine.CopyLcd(lcd.data(), lcd.size());
	if ((machine.PC() >> 1) != pc || Fnv1a(lcd.data(), lcd.size()) != hash) {
		std::cerr << "snapshot round-trip mismatch\n";
		return 1;
	}

	std::cout << std::hex << std::setfill('0')
		<< "EPS6800 adapter OK: pc=0x" << std::setw(6) << pc
		<< " acc=0x" << std::setw(2) << static_cast<unsigned>(machine.ACC)
		<< " status=0x" << std::setw(2) << static_cast<unsigned>(machine.STATUS)
		<< " lcd_fnv1a=0x" << std::setw(8) << hash
		<< " status_bus=";
	for (const auto value : display.status)
		std::cout << std::setw(2) << static_cast<unsigned>(value);

	// The HP indicator row has its own physical bit layout. Exercise independent
	// cold boots so persistent calculator settings cannot leak across scenarios.
	const auto Scenario = [&](std::initializer_list<uint8_t> keys) {
		casioemu::ePSCPU scenario_machine;
		scenario_machine.LoadRom(rom);
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

	std::cout << " snapshot=ok status_map=ok\n";
	return 0;
}
