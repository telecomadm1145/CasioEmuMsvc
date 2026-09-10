#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace casioemu::screen_scan {
	inline int UpdateScanAlpha(
		float* screen_scan_alpha,
		std::array<float, 64>& screen_scan_curve,
		float& screen_scan_curve_coeff,
		bool& screen_scan_curve_valid,
		uint64_t t,
		int screen_refresh_rate,
		int flashing_threshold, float brightness_coeff) {
		int n = (static_cast<uint64_t>((t * screen_refresh_rate) / 250)) % 64;

		if (screen_refresh_rate < flashing_threshold) {
			for (size_t i = 0; i < 64; i++) {
				screen_scan_alpha[i] = 1.0f;
			}
			return n;
		}

		if (!screen_scan_curve_valid || screen_scan_curve_coeff != brightness_coeff) {
			// 计算归一化所需的归一化因子
			float normalization_factor = 0.0f;
			std::array<float, 64> exp_values{};

			for (size_t i = 0; i < 64; i++) {
				exp_values[i] = std::exp(-brightness_coeff * i / 64.0f);
				normalization_factor += exp_values[i];
			}

			// 归一化
			for (size_t i = 0; i < 64; i++) {
				screen_scan_curve[i] = std::pow(exp_values[i] / normalization_factor * 80., 0.2);
			}
			screen_scan_curve_coeff = brightness_coeff;
			screen_scan_curve_valid = true;
		}

		for (size_t i = 0; i < 64; i++) {
			screen_scan_alpha[(i + n) % 64] = screen_scan_curve[i];
		}

		return n;
	}

}
