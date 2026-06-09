#pragma once
#include "Binary.h"
#include <iostream>
#include <string>
// 表示一个 SFR
class SpecialFunctionRegister {
public:
	std::string peripheral;
	std::string name;
	uint16_t offset{};
	void Write(std::ostream& os) const {
		Binary::Write(os, offset);
		Binary::Write(os, peripheral);
		Binary::Write(os, name);
	}
	void Read(std::istream& is) {
		Binary::Read(is, offset);
		Binary::Read(is, peripheral);
		Binary::Read(is, name);
	}
};
class Port {
public:
	std::string name;
	std::map<uint16_t, std::string> peripherals; // Mode mapping
	uint8_t mask;
	void Write(std::ostream& os) const {
		Binary::Write(os, name);
		Binary::Write(os, peripherals);
		Binary::Write(os, mask);
	}
	void Read(std::istream& is) {
		Binary::Read(is, name);
		Binary::Read(is, peripherals);
		Binary::Read(is, mask);
	}
};
// 表示芯片组的相关信息
class ChipsetInfo {
public:
	std::string name;
	std::vector<std::string> peripherals;
	std::vector<Port> ports;
	std::vector<SpecialFunctionRegister> sfrs;

	void Write(std::ostream& os) const {
		Binary::Write(os, name);
		Binary::Write(os, peripherals);
		Binary::Write(os, ports);
		Binary::Write(os, sfrs);
	}

	void Read(std::istream& is) {
		Binary::Read(is, name);
		Binary::Read(is, peripherals);
		Binary::Read(is, ports);
		Binary::Read(is, sfrs);
	}
};
