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
#include "Crypto.hpp"
#include "ModelConfig.h"
#include "ModelInfo.h"
#include "Random.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

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

	static bool isPathSafe(const std::string& path_str) {
		std::filesystem::path path(path_str);
		if (path.is_absolute()) return false;
		for (const auto& part : path) {
			if (part == "..") {
				return false;
			}
		}
		return true;
	}

public:
	File RomData;
	File FlashData;
	File InterfaceData;
	bool IsEncrypted{};
	uint8_t EncryptionVersion{};
	std::vector<unsigned char> Salt;
	std::vector<unsigned char> Nonce;
	std::vector<unsigned char> Hmac;
	uint32_t Crc32{};
	casioemu::ModelInfo ModelInfo;

	void Read(std::istream& is) {
		Binary::Read(is, RomData);
		Binary::Read(is, FlashData);
		Binary::Read(is, InterfaceData);
		uint8_t encVersion = 0;
		Binary::Read(is, encVersion);
		EncryptionVersion = encVersion;
		IsEncrypted = (encVersion != 0);
		if (EncryptionVersion == 2) {
			Binary::Read(is, Salt);
			Binary::Read(is, Nonce);
			Binary::Read(is, Hmac);
		}
		Binary::Read(is, Crc32);
		Binary::Read(is, ModelInfo);
	}

	void Write(std::ostream& os) const {
		Binary::Write(os, RomData);
		Binary::Write(os, FlashData);
		Binary::Write(os, InterfaceData);
		uint8_t encVersion = EncryptionVersion;
		if (encVersion == 0 && IsEncrypted) {
			encVersion = 2; // fallback
		}
		Binary::Write(os, encVersion);
		if (encVersion == 2) {
			Binary::Write(os, Salt);
			Binary::Write(os, Nonce);
			Binary::Write(os, Hmac);
		}
		Binary::Write(os, Crc32);
		Binary::Write(os, ModelInfo);
	}

	void Encrypt(const std::string& key) {
		if (EncryptionVersion != 0 || IsEncrypted)
			return;

		Salt.resize(16);
		util::Random::fillRandomBytes(Salt.data(), Salt.size());
		Nonce.resize(12);
		util::Random::fillRandomBytes(Nonce.data(), Nonce.size());

		std::vector<uint8_t> K = crypto::pbkdf2_hmac_sha256(key, Salt, 10000);

		std::vector<uint8_t> rom_label = { 'R', 'O', 'M' };
		std::vector<uint8_t> flash_label = { 'F', 'L', 'A', 'S', 'H' };
		std::vector<uint8_t> interface_label = { 'I', 'N', 'T', 'E', 'R', 'F', 'A', 'C', 'E' };
		std::vector<uint8_t> mac_label = { 'M', 'A', 'C' };

		std::vector<uint8_t> K_rom_input = K;
		K_rom_input.insert(K_rom_input.end(), rom_label.begin(), rom_label.end());
		std::vector<uint8_t> K_rom = crypto::SHA256::hash256(K_rom_input);

		std::vector<uint8_t> K_flash_input = K;
		K_flash_input.insert(K_flash_input.end(), flash_label.begin(), flash_label.end());
		std::vector<uint8_t> K_flash = crypto::SHA256::hash256(K_flash_input);

		std::vector<uint8_t> K_interface_input = K;
		K_interface_input.insert(K_interface_input.end(), interface_label.begin(), interface_label.end());
		std::vector<uint8_t> K_interface = crypto::SHA256::hash256(K_interface_input);

		std::vector<uint8_t> K_mac_input = K;
		K_mac_input.insert(K_mac_input.end(), mac_label.begin(), mac_label.end());
		std::vector<uint8_t> K_mac = crypto::SHA256::hash256(K_mac_input);

		crypto::chacha20_crypt(K_rom, Nonce, RomData);
		crypto::chacha20_crypt(K_flash, Nonce, FlashData);
		crypto::chacha20_crypt(K_interface, Nonce, InterfaceData);

		std::vector<uint8_t> auth_data;
		auth_data.insert(auth_data.end(), RomData.begin(), RomData.end());
		auth_data.insert(auth_data.end(), FlashData.begin(), FlashData.end());
		auth_data.insert(auth_data.end(), InterfaceData.begin(), InterfaceData.end());
		Hmac = crypto::HMAC_SHA256::hmac(K_mac, auth_data);

		Crc32 = 0;
		EncryptionVersion = 2;
		IsEncrypted = true;
	}

	void Decrypt(const std::string& key) {
		if (EncryptionVersion == 0 && !IsEncrypted)
			return;

		if (EncryptionVersion == 1 || (EncryptionVersion == 0 && IsEncrypted)) {
			uint32_t keyCrc = crc32(std::vector<unsigned char>(key.begin(), key.end()));
			xorData(RomData, key);
			xorData(FlashData, key);
			xorData(InterfaceData, key);
			if (calculateDataCrc32() != Crc32) {
				// Rollback
				xorData(RomData, key);
				xorData(FlashData, key);
				xorData(InterfaceData, key);
				throw std::runtime_error("Invalid decryption key");
			}
			EncryptionVersion = 0;
			IsEncrypted = false;
			return;
		}

		if (EncryptionVersion == 2) {
			std::vector<uint8_t> K = crypto::pbkdf2_hmac_sha256(key, Salt, 10000);

			std::vector<uint8_t> rom_label = { 'R', 'O', 'M' };
			std::vector<uint8_t> flash_label = { 'F', 'L', 'A', 'S', 'H' };
			std::vector<uint8_t> interface_label = { 'I', 'N', 'T', 'E', 'R', 'F', 'A', 'C', 'E' };
			std::vector<uint8_t> mac_label = { 'M', 'A', 'C' };

			std::vector<uint8_t> K_rom_input = K;
			K_rom_input.insert(K_rom_input.end(), rom_label.begin(), rom_label.end());
			std::vector<uint8_t> K_rom = crypto::SHA256::hash256(K_rom_input);

			std::vector<uint8_t> K_flash_input = K;
			K_flash_input.insert(K_flash_input.end(), flash_label.begin(), flash_label.end());
			std::vector<uint8_t> K_flash = crypto::SHA256::hash256(K_flash_input);

			std::vector<uint8_t> K_interface_input = K;
			K_interface_input.insert(K_interface_input.end(), interface_label.begin(), interface_label.end());
			std::vector<uint8_t> K_interface = crypto::SHA256::hash256(K_interface_input);

			std::vector<uint8_t> K_mac_input = K;
			K_mac_input.insert(K_mac_input.end(), mac_label.begin(), mac_label.end());
			std::vector<uint8_t> K_mac = crypto::SHA256::hash256(K_mac_input);

			std::vector<uint8_t> auth_data;
			auth_data.insert(auth_data.end(), RomData.begin(), RomData.end());
			auth_data.insert(auth_data.end(), FlashData.begin(), FlashData.end());
			auth_data.insert(auth_data.end(), InterfaceData.begin(), InterfaceData.end());
			std::vector<uint8_t> calculated_hmac = crypto::HMAC_SHA256::hmac(K_mac, auth_data);

			if (!crypto::constant_time_compare(calculated_hmac, Hmac)) {
				throw std::runtime_error("Invalid decryption key");
			}

			crypto::chacha20_crypt(K_rom, Nonce, RomData);
			crypto::chacha20_crypt(K_flash, Nonce, FlashData);
			crypto::chacha20_crypt(K_interface, Nonce, InterfaceData);

			Salt.clear();
			Nonce.clear();
			Hmac.clear();
			EncryptionVersion = 0;
			IsEncrypted = false;
			return;
		}

		throw std::runtime_error("Unsupported encryption version");
	}

	void ExtractTo(std::filesystem::path pth) {
		if (IsEncrypted)
			throw std::runtime_error("Please decrypt first.");

		if (!isPathSafe(ModelInfo.rom_path)) throw std::runtime_error("Path traversal detected");
		if (!ModelInfo.flash_path.empty() && !isPathSafe(ModelInfo.flash_path)) throw std::runtime_error("Path traversal detected");
		if (!isPathSafe(ModelInfo.interface_path)) throw std::runtime_error("Path traversal detected");

		std::filesystem::create_directory(pth);
		WriteFile(pth / ModelInfo.rom_path, RomData);
		if (!ModelInfo.flash_path.empty())
			WriteFile(pth / ModelInfo.flash_path, FlashData);
		WriteFile(pth / ModelInfo.interface_path, InterfaceData);
		SaveModelInfoJson(pth, ModelInfo);
	}

	void Load(std::filesystem::path pth) {
		std::string error;
		if (!LoadModelInfoFromFolder(pth, ModelInfo, nullptr, &error))
			throw std::runtime_error(error);
		ReadFile(pth / ModelInfo.rom_path, RomData);
		if (!ModelInfo.flash_path.empty())
			ReadFile(pth / ModelInfo.flash_path, FlashData);
		ReadFile(pth / ModelInfo.interface_path, InterfaceData);
	}
};
