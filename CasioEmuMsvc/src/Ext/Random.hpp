// Random utilities for safe, reusable randomness across the project
#pragma once

#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <thread>

namespace util {
	class Random {
	public:
		static inline std::mt19937_64& engine() {
			thread_local std::mt19937_64 rng;
			thread_local bool initialized = false;
			if (!initialized) {
				std::uint64_t seed = 0;
				try {
					std::random_device rd;
					seed ^= static_cast<std::uint64_t>(rd());
					seed ^= (static_cast<std::uint64_t>(rd()) << 32);
				}
				catch (...) {
					// Fallback: combine time and address entropy
				}
				seed ^= static_cast<std::uint64_t>(
					std::chrono::high_resolution_clock::now().time_since_epoch().count());
				seed ^= reinterpret_cast<std::uintptr_t>(&seed);
				seed ^= static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
				rng.seed(seed);
				initialized = true;
			}
			return rng;
		}

		static inline void fillRandomBytes(std::uint8_t* buffer, std::size_t size) {
			std::uniform_int_distribution<int> dist(0, 255);
			auto& rng = engine();
			for (std::size_t i = 0; i < size; ++i) {
				buffer[i] = static_cast<std::uint8_t>(dist(rng));
			}
		}
		template <class T>
		static inline T getRandomObject() {
			T d;
			fillRandomBytes((std::uint8_t*)&d, sizeof(d));
			return d;
		}

		static inline std::uint32_t uniform_uint32(std::uint32_t min_inclusive, std::uint32_t max_inclusive) {
			std::uniform_int_distribution<std::uint32_t> dist(min_inclusive, max_inclusive);
			return dist(engine());
		}

		static inline std::string random_string(std::size_t length, std::string_view charset = "0123456789abcdefghijklmnopqrstuvwxyz") {
			if (charset.empty() || length == 0)
				return std::string();
			std::uniform_int_distribution<std::size_t> dist(0, charset.size() - 1);
			std::string out;
			out.resize(length);
			auto& rng = engine();
			for (std::size_t i = 0; i < length; ++i) {
				out[i] = charset[dist(rng)];
			}
			return out;
		}
	};
} // namespace util
