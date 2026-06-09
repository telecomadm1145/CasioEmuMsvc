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
#include "Random.hpp"
#include <vector>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <stdexcept>

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

namespace crypto {
	class SHA256 {
		uint32_t state[8];
		uint64_t bitlen;
		uint8_t buffer[64];
		uint32_t curlen;

		static uint32_t rotr(uint32_t val, uint32_t num) {
			return (val >> num) | (val << (32 - num));
		}

		void transform(const uint8_t* data) {
			uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
			for (int i = 0; i < 16; ++i) {
				m[i] = ((uint32_t)data[i * 4] << 24) |
				       ((uint32_t)data[i * 4 + 1] << 16) |
				       ((uint32_t)data[i * 4 + 2] << 8) |
				       ((uint32_t)data[i * 4 + 3]);
			}
			for (int i = 16; i < 64; ++i) {
				uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
				uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
				m[i] = m[i - 16] + s0 + m[i - 7] + s1;
			}
			a = state[0]; b = state[1]; c = state[2]; d = state[3];
			e = state[4]; f = state[5]; g = state[6]; h = state[7];

			static const uint32_t k[64] = {
				0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
				0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
				0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
				0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
				0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
				0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
				0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
				0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
			};

			for (int i = 0; i < 64; ++i) {
				uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
				uint32_t ch = (e & f) ^ (~e & g);
				t1 = h + s1 + ch + k[i] + m[i];
				uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
				uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
				t2 = s0 + maj;
				h = g;
				g = f;
				f = e;
				e = d + t1;
				d = c;
				c = b;
				b = a;
				a = t1 + t2;
			}
			state[0] += a; state[1] += b; state[2] += c; state[3] += d;
			state[4] += e; state[5] += f; state[6] += g; state[7] += h;
		}

	public:
		SHA256() {
			state[0] = 0x6a09e667; state[1] = 0xbb67ae85; state[2] = 0x3c6ef372; state[3] = 0xa54ff53a;
			state[4] = 0x510e527f; state[5] = 0x9b05688c; state[6] = 0x1f83d9ab; state[7] = 0x5be0cd19;
			bitlen = 0;
			curlen = 0;
		}

		void update(const uint8_t* data, size_t len) {
			for (size_t i = 0; i < len; ++i) {
				buffer[curlen++] = data[i];
				if (curlen == 64) {
					transform(buffer);
					bitlen += 512;
					curlen = 0;
				}
			}
		}

		void final(uint8_t hash[32]) {
			uint64_t i = curlen;
			if (curlen < 56) {
				buffer[i++] = 0x80;
				while (i < 56) buffer[i++] = 0x00;
			}
			else {
				buffer[i++] = 0x80;
				while (i < 64) buffer[i++] = 0x00;
				transform(buffer);
				memset(buffer, 0, 56);
			}
			bitlen += curlen * 8;
			buffer[56] = (bitlen >> 56) & 0xFF;
			buffer[57] = (bitlen >> 48) & 0xFF;
			buffer[58] = (bitlen >> 40) & 0xFF;
			buffer[59] = (bitlen >> 32) & 0xFF;
			buffer[60] = (bitlen >> 24) & 0xFF;
			buffer[61] = (bitlen >> 16) & 0xFF;
			buffer[62] = (bitlen >> 8) & 0xFF;
			buffer[63] = bitlen & 0xFF;
			transform(buffer);

			for (int j = 0; j < 8; ++j) {
				hash[j * 4] = (state[j] >> 24) & 0xFF;
				hash[j * 4 + 1] = (state[j] >> 16) & 0xFF;
				hash[j * 4 + 2] = (state[j] >> 8) & 0xFF;
				hash[j * 4 + 3] = state[j] & 0xFF;
			}
		}

		static std::vector<uint8_t> hash256(const std::vector<uint8_t>& data) {
			SHA256 ctx;
			ctx.update(data.data(), data.size());
			std::vector<uint8_t> out(32);
			ctx.final(out.data());
			return out;
		}

		static std::vector<uint8_t> hash256(const std::string& str) {
			SHA256 ctx;
			ctx.update(reinterpret_cast<const uint8_t*>(str.data()), str.size());
			std::vector<uint8_t> out(32);
			ctx.final(out.data());
			return out;
		}
	};

	class HMAC_SHA256 {
	public:
		static std::vector<uint8_t> hmac(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data) {
			std::vector<uint8_t> k = key;
			if (k.size() > 64) {
				k = SHA256::hash256(k);
			}
			if (k.size() < 64) {
				k.resize(64, 0);
			}
			std::vector<uint8_t> ipad(64), opad(64);
			for (int i = 0; i < 64; ++i) {
				ipad[i] = k[i] ^ 0x36;
				opad[i] = k[i] ^ 0x5C;
			}
			SHA256 inner;
			inner.update(ipad.data(), 64);
			inner.update(data.data(), data.size());
			std::vector<uint8_t> inner_hash(32);
			inner.final(inner_hash.data());

			SHA256 outer;
			outer.update(opad.data(), 64);
			outer.update(inner_hash.data(), 32);
			std::vector<uint8_t> outer_hash(32);
			outer.final(outer_hash.data());
			return outer_hash;
		}
	};

	inline std::vector<uint8_t> pbkdf2_hmac_sha256(const std::string& password, const std::vector<uint8_t>& salt, int iterations) {
		std::vector<uint8_t> p_bytes(password.begin(), password.end());
		std::vector<uint8_t> salt_with_int = salt;
		salt_with_int.push_back(0);
		salt_with_int.push_back(0);
		salt_with_int.push_back(0);
		salt_with_int.push_back(1);

		std::vector<uint8_t> u = HMAC_SHA256::hmac(p_bytes, salt_with_int);
		std::vector<uint8_t> f = u;
		for (int i = 1; i < iterations; ++i) {
			u = HMAC_SHA256::hmac(p_bytes, u);
			for (size_t j = 0; j < 32; ++j) {
				f[j] ^= u[j];
			}
		}
		return f;
	}

	inline uint32_t rotl(uint32_t v, int c) {
		return (v << c) | (v >> (32 - c));
	}

	inline void chacha_quarter_round(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
		a += b; d ^= a; d = rotl(d, 16);
		c += d; b ^= c; b = rotl(b, 12);
		a += b; d ^= a; d = rotl(d, 8);
		c += d; b ^= c; b = rotl(b, 7);
	}

	inline void chacha20_block(uint32_t out[16], const uint32_t key[8], uint32_t counter, const uint32_t nonce[3]) {
		uint32_t state[16] = {
			0x61787065, 0x3320646e, 0x79622d32, 0x6b206574,
			key[0], key[1], key[2], key[3],
			key[4], key[5], key[6], key[7],
			counter, nonce[0], nonce[1], nonce[2]
		};
		uint32_t x[16];
		std::memcpy(x, state, sizeof(state));
		for (int i = 0; i < 10; ++i) {
			// Columns
			chacha_quarter_round(x[0], x[4], x[8], x[12]);
			chacha_quarter_round(x[1], x[5], x[9], x[13]);
			chacha_quarter_round(x[2], x[6], x[10], x[14]);
			chacha_quarter_round(x[3], x[7], x[11], x[15]);
			// Diagonals
			chacha_quarter_round(x[0], x[5], x[10], x[15]);
			chacha_quarter_round(x[1], x[6], x[11], x[12]);
			chacha_quarter_round(x[2], x[7], x[8], x[13]);
			chacha_quarter_round(x[3], x[4], x[9], x[14]);
		}
		for (int i = 0; i < 16; ++i) {
			out[i] = x[i] + state[i];
		}
	}

	inline void chacha20_crypt(const std::vector<uint8_t>& key, const std::vector<uint8_t>& nonce, std::vector<uint8_t>& data) {
		uint32_t key_words[8];
		for (int i = 0; i < 8; ++i) {
			key_words[i] = ((uint32_t)key[i * 4]) |
			               ((uint32_t)key[i * 4 + 1] << 8) |
			               ((uint32_t)key[i * 4 + 2] << 16) |
			               ((uint32_t)key[i * 4 + 3] << 24);
		}
		uint32_t nonce_words[3];
		for (int i = 0; i < 3; ++i) {
			nonce_words[i] = ((uint32_t)nonce[i * 4]) |
			                 ((uint32_t)nonce[i * 4 + 1] << 8) |
			                 ((uint32_t)nonce[i * 4 + 2] << 16) |
			                 ((uint32_t)nonce[i * 4 + 3] << 24);
		}

		uint32_t counter = 0;
		uint32_t block[16];
		uint8_t block_bytes[64];

		size_t i = 0;
		while (i < data.size()) {
			chacha20_block(block, key_words, counter, nonce_words);
			for (int j = 0; j < 16; ++j) {
				block_bytes[j * 4] = block[j] & 0xFF;
				block_bytes[j * 4 + 1] = (block[j] >> 8) & 0xFF;
				block_bytes[j * 4 + 2] = (block[j] >> 16) & 0xFF;
				block_bytes[j * 4 + 3] = (block[j] >> 24) & 0xFF;
			}

			size_t chunk = std::min<size_t>(64, data.size() - i);
			for (size_t j = 0; j < chunk; ++j) {
				data[i + j] ^= block_bytes[j];
			}
			i += chunk;
			counter++;
		}
	}

	inline bool constant_time_compare(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
		if (a.size() != b.size()) return false;
		uint8_t result = 0;
		for (size_t i = 0; i < a.size(); ++i) {
			result |= a[i] ^ b[i];
		}
		return result == 0;
	}
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