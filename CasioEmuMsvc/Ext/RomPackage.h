#pragma once
/*
	The ROM package implement for the emulator
	Copyright (C) 2024 telecomadm1145

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "Binary.h"
#include "ModelInfo.h"
#include <vector>
#include <filesystem>
#include <fstream>
inline void WriteFile(std::filesystem::path pth, const std::vector<unsigned char>& f) {
	std::ofstream fs(pth, std::ios::binary | std::ios::out);
	if (!fs)
		throw std::runtime_error("Cannot open file.");
	fs.write((char*)f.data(), f.size());
	fs.close();
}
inline void ReadFile(std::filesystem::path pth, std::vector<unsigned char>& f) {
	std::ifstream fs(pth, std::ios::binary | std::ios::in);
	if (!fs)
		throw std::runtime_error("Cannot open file.");
	fs.seekg(0, std::ios::end);
	f.resize((size_t)fs.tellg());
	fs.seekg(0);
	fs.read((char*)f.data(), f.size());
	fs.close();
}
inline void WriteFile(std::filesystem::path pth, const auto& f) {
	std::ofstream fs(pth, std::ios::binary | std::ios::out);
	if (!fs)
		throw std::runtime_error("Cannot open file.");
	Binary::Write(fs, f);
	fs.close();
}
inline void ReadFile(std::filesystem::path pth, auto& f) {
	std::ifstream fs(pth, std::ios::binary | std::ios::in);
	if (!fs)
		throw std::runtime_error("Cannot open file.");
	Binary::Read(fs, f);
	fs.close();
}
class RomPackage {
	using File = std::vector<unsigned char>;

	static uint32_t crc32(const auto& data) {
		uint32_t crc = 0xFFFFFFFF;
		for (unsigned char c : data) {
			crc ^= c;
			for (int i = 0; i < 8; ++i) {
				crc = (crc >> 1) ^ (0xEDB88320 * (crc & 1));
			}
		}
		return ~crc;
	}

	uint32_t calculateDataCrc32() const {
		return crc32(RomData) ^ crc32(FlashData) ^ crc32(InterfaceData);
	}

	static void xorData(std::vector<unsigned char>& data, const std::string& key) {
		uint32_t seed = crc32(key);
		for (size_t i = 0; i < data.size(); ++i) {
			seed = (seed * 86028121 + 611953) & 0xFFFFFFFF;
			uint8_t mask = (seed ^ (seed >> 8) ^ (seed >> 16) ^ (seed >> 24)) & 0xFF;
			data[i] ^= (key[i % key.length()] ^ mask);
		}
	}

public:
	File RomData;
	File FlashData;
	File InterfaceData;
	bool IsEncrypted{};
	uint32_t Crc32{};
	casioemu::ModelInfo ModelInfo;
	void Read(std::istream& is) {
		Binary::Read(is, RomData);
		Binary::Read(is, FlashData);
		Binary::Read(is, InterfaceData);
		Binary::Read(is, IsEncrypted);
		Binary::Read(is, Crc32);
		Binary::Read(is, ModelInfo);
	}
	void Write(std::ostream& os) const {
		Binary::Write(os, RomData);
		Binary::Write(os, FlashData);
		Binary::Write(os, InterfaceData);
		Binary::Write(os, IsEncrypted);
		Binary::Write(os, Crc32);
		Binary::Write(os, ModelInfo);
	}
	void Encrypt(const std::string& key) {
		if (IsEncrypted)
			return;
		Crc32 = calculateDataCrc32();
		uint32_t keyCrc = crc32(std::vector<unsigned char>(key.begin(), key.end()));
		xorData(RomData, key);
		xorData(FlashData, key);
		xorData(InterfaceData, key);
		IsEncrypted = true;
	}
	void Decrypt(const std::string& key) {
		if (!IsEncrypted)
			return;
		uint32_t keyCrc = crc32(std::vector<unsigned char>(key.begin(), key.end()));
		xorData(RomData, key);
		xorData(FlashData, key);
		xorData(InterfaceData, key);
		if (calculateDataCrc32() != Crc32) {
			throw std::runtime_error("Invalid decryption key");
		}
		IsEncrypted = false;
	}
	void ExtractTo(std::filesystem::path pth) {
		if (IsEncrypted)
			throw std::runtime_error("Please decrypt first.");
		std::filesystem::create_directory(pth);
		WriteFile(pth / ModelInfo.rom_path, RomData);
		if (!ModelInfo.flash_path.empty())
			WriteFile(pth / ModelInfo.flash_path, FlashData);
		WriteFile(pth / ModelInfo.interface_path, InterfaceData);
		WriteFile(pth / "config.bin", ModelInfo);
	}
	void Load(std::filesystem::path pth) {
		ReadFile(pth / "config.bin", ModelInfo);
		ReadFile(pth / ModelInfo.rom_path, RomData);
		if (!ModelInfo.flash_path.empty())
			ReadFile(pth / ModelInfo.flash_path, FlashData);
		ReadFile(pth / ModelInfo.interface_path, InterfaceData);
	}
};