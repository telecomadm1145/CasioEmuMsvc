#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <stdexcept>
#include <vector>

namespace crypto {
	class SHA256 {
		std::uint32_t state[8];
		std::uint64_t bitlen;
		std::uint8_t buffer[64];
		std::uint32_t curlen;

		static std::uint32_t rotr(std::uint32_t val, std::uint32_t num) {
			return (val >> num) | (val << (32 - num));
		}

		void transform(const std::uint8_t* data) {
			std::uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
			for (int i = 0; i < 16; ++i) {
				m[i] = (static_cast<std::uint32_t>(data[i * 4]) << 24) |
					(static_cast<std::uint32_t>(data[i * 4 + 1]) << 16) |
					(static_cast<std::uint32_t>(data[i * 4 + 2]) << 8) |
					(static_cast<std::uint32_t>(data[i * 4 + 3]));
			}
			for (int i = 16; i < 64; ++i) {
				std::uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
				std::uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
				m[i] = m[i - 16] + s0 + m[i - 7] + s1;
			}
			a = state[0]; b = state[1]; c = state[2]; d = state[3];
			e = state[4]; f = state[5]; g = state[6]; h = state[7];

			static const std::uint32_t k[64] = {
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
				std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
				std::uint32_t ch = (e & f) ^ (~e & g);
				t1 = h + s1 + ch + k[i] + m[i];
				std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
				std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
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

		void update(const std::uint8_t* data, std::size_t len) {
			for (std::size_t i = 0; i < len; ++i) {
				buffer[curlen++] = data[i];
				if (curlen == 64) {
					transform(buffer);
					bitlen += 512;
					curlen = 0;
				}
			}
		}

		void final(std::uint8_t hash[32]) {
			std::uint64_t i = curlen;
			if (curlen < 56) {
				buffer[i++] = 0x80;
				while (i < 56) buffer[i++] = 0x00;
			}
			else {
				buffer[i++] = 0x80;
				while (i < 64) buffer[i++] = 0x00;
				transform(buffer);
				std::memset(buffer, 0, 56);
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

		static std::vector<std::uint8_t> hash256(const std::vector<std::uint8_t>& data) {
			SHA256 ctx;
			ctx.update(data.data(), data.size());
			std::vector<std::uint8_t> out(32);
			ctx.final(out.data());
			return out;
		}

		static std::vector<std::uint8_t> hash256(const std::string& str) {
			SHA256 ctx;
			ctx.update(reinterpret_cast<const std::uint8_t*>(str.data()), str.size());
			std::vector<std::uint8_t> out(32);
			ctx.final(out.data());
			return out;
		}
	};

	class HMAC_SHA256 {
	public:
		static std::vector<std::uint8_t> hmac(const std::vector<std::uint8_t>& key, const std::vector<std::uint8_t>& data) {
			std::vector<std::uint8_t> k = key;
			if (k.size() > 64) {
				k = SHA256::hash256(k);
			}
			if (k.size() < 64) {
				k.resize(64, 0);
			}
			std::vector<std::uint8_t> ipad(64), opad(64);
			for (int i = 0; i < 64; ++i) {
				ipad[i] = k[i] ^ 0x36;
				opad[i] = k[i] ^ 0x5C;
			}
			SHA256 inner;
			inner.update(ipad.data(), 64);
			inner.update(data.data(), data.size());
			std::vector<std::uint8_t> inner_hash(32);
			inner.final(inner_hash.data());

			SHA256 outer;
			outer.update(opad.data(), 64);
			outer.update(inner_hash.data(), 32);
			std::vector<std::uint8_t> outer_hash(32);
			outer.final(outer_hash.data());
			return outer_hash;
		}
	};

	inline std::vector<std::uint8_t> pbkdf2_hmac_sha256(const std::string& password, const std::vector<std::uint8_t>& salt, int iterations) {
		std::vector<std::uint8_t> p_bytes(password.begin(), password.end());
		std::vector<std::uint8_t> salt_with_int = salt;
		salt_with_int.push_back(0);
		salt_with_int.push_back(0);
		salt_with_int.push_back(0);
		salt_with_int.push_back(1);

		std::vector<std::uint8_t> u = HMAC_SHA256::hmac(p_bytes, salt_with_int);
		std::vector<std::uint8_t> f = u;
		for (int i = 1; i < iterations; ++i) {
			u = HMAC_SHA256::hmac(p_bytes, u);
			for (std::size_t j = 0; j < 32; ++j) {
				f[j] ^= u[j];
			}
		}
		return f;
	}

	inline std::uint32_t rotl(std::uint32_t v, int c) {
		return (v << c) | (v >> (32 - c));
	}

	inline void chacha_quarter_round(std::uint32_t& a, std::uint32_t& b, std::uint32_t& c, std::uint32_t& d) {
		a += b; d ^= a; d = rotl(d, 16);
		c += d; b ^= c; b = rotl(b, 12);
		a += b; d ^= a; d = rotl(d, 8);
		c += d; b ^= c; b = rotl(b, 7);
	}

	inline void chacha20_block(std::uint32_t out[16], const std::uint32_t key[8], std::uint32_t counter, const std::uint32_t nonce[3]) {
		std::uint32_t state[16] = {
			0x61787065, 0x3320646e, 0x79622d32, 0x6b206574,
			key[0], key[1], key[2], key[3],
			key[4], key[5], key[6], key[7],
			counter, nonce[0], nonce[1], nonce[2]
		};
		std::uint32_t x[16];
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

	inline void chacha20_crypt(const std::vector<std::uint8_t>& key, const std::vector<std::uint8_t>& nonce, std::vector<std::uint8_t>& data) {
		if (key.size() < 32 || nonce.size() < 12) {
			throw std::invalid_argument("ChaCha20 requires a 32-byte key and a 12-byte nonce");
		}
		std::uint32_t key_words[8];
		for (int i = 0; i < 8; ++i) {
			key_words[i] = (static_cast<std::uint32_t>(key[i * 4])) |
				(static_cast<std::uint32_t>(key[i * 4 + 1]) << 8) |
				(static_cast<std::uint32_t>(key[i * 4 + 2]) << 16) |
				(static_cast<std::uint32_t>(key[i * 4 + 3]) << 24);
		}
		std::uint32_t nonce_words[3];
		for (int i = 0; i < 3; ++i) {
			nonce_words[i] = (static_cast<std::uint32_t>(nonce[i * 4])) |
				(static_cast<std::uint32_t>(nonce[i * 4 + 1]) << 8) |
				(static_cast<std::uint32_t>(nonce[i * 4 + 2]) << 16) |
				(static_cast<std::uint32_t>(nonce[i * 4 + 3]) << 24);
		}

		std::uint32_t counter = 0;
		std::uint32_t block[16];
		std::uint8_t block_bytes[64];

		std::size_t i = 0;
		while (i < data.size()) {
			chacha20_block(block, key_words, counter, nonce_words);
			for (int j = 0; j < 16; ++j) {
				block_bytes[j * 4] = block[j] & 0xFF;
				block_bytes[j * 4 + 1] = (block[j] >> 8) & 0xFF;
				block_bytes[j * 4 + 2] = (block[j] >> 16) & 0xFF;
				block_bytes[j * 4 + 3] = (block[j] >> 24) & 0xFF;
			}

			std::size_t chunk = std::min<std::size_t>(64, data.size() - i);
			for (std::size_t j = 0; j < chunk; ++j) {
				data[i + j] ^= block_bytes[j];
			}
			i += chunk;
			counter++;
		}
	}

	inline bool constant_time_compare(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
		if (a.size() != b.size()) return false;
		std::uint8_t result = 0;
		for (std::size_t i = 0; i < a.size(); ++i) {
			result |= a[i] ^ b[i];
		}
		return result == 0;
	}
}
