#include "EpsScreen.hpp"

#include "Chipset/Eps6800Display.h"
#include "Chipset/ePSCpu.h"
#include "LcdResponse.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace casioemu {

EpsScreenSpec GetEpsScreenSpec(
	HardwareId hardware_id,
	int model_screen_width,
	int model_screen_height) {
	switch (hardware_id) {
	case HW_EPS6800:
		return {96, 31, 96, 32, 96, 31, 192, 32, 16};
	case HW_EPS6800_W192:
		return {192, 63, 192, 64, 192, 63, 192, 64, 24};
	case HW_EPS9500:
		return {96, 32, 96, 33, 96, 32, 192, 33, 16};
	case HW_EPS6009:
		return {
			std::max(1, model_screen_width),
			std::max(1, model_screen_height),
			std::max(1, model_screen_width),
			std::max(1, model_screen_height),
			std::max(1, model_screen_width),
			std::max(1, model_screen_height),
			192,
			1,
			1};
	default:
		return {};
	}
}

namespace {

float BlendAlpha(
	float alpha,
	float target,
	float transition_ratio,
	const EpsLcdResponseTick& response) {
	if (!response.enabled)
		return alpha * transition_ratio + target * (1 - transition_ratio);
	return static_cast<float>(lcd_response::BlendWithGains(
		static_cast<double>(alpha), static_cast<double>(target),
		response.rise_gain, response.fall_gain));
}

template <typename DecodedFrame>
void UpdateStatusAlpha(
	const DecodedFrame& decoded,
	const std::vector<StatusIndicatorInfo>& status_indicators,
	std::array<float, 66 * 192>& alpha_buffer,
	float transition_ratio,
	float ink_alpha_on,
	float ink_alpha_off,
	const EpsLcdResponseTick& response) {
	for (size_t ix = 0; ix < status_indicators.size(); ++ix) {
		const auto& indicator = status_indicators[ix];
		const bool on = indicator.byte_offset < decoded.status.size() &&
			(decoded.status[indicator.byte_offset] & (1u << indicator.bit)) != 0;
		auto& alpha = alpha_buffer[ix];
		alpha = BlendAlpha(alpha, on ? ink_alpha_on : ink_alpha_off,
			transition_ratio, response);
	}
}

void UpdateDotMatrixAlpha(
	const uint8_t* pixels,
	int width,
	int height,
	std::array<float, 66 * 192>& alpha_buffer,
	float transition_ratio,
	float ink_alpha_on,
	float ink_alpha_off,
	const EpsLcdResponseTick& response) {
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const bool on = pixels[y * width + x] != 0;
			auto& alpha = alpha_buffer[(y + 1) * 192 + x];
			alpha = BlendAlpha(alpha, on ? ink_alpha_on : ink_alpha_off,
				transition_ratio, response);
		}
	}
}

} // namespace

void UpdateEpsScreen(EpsScreenContext& context) {
	if (!context.eps_cpu)
		return;

	const float transition_ratio = context.residual_enabled ? context.transition_ratio : 0.0f;
	if (context.hardware_id == HW_EPS6009) {
		std::array<uint8_t, 0x88> lcd{};
		Eps6800LcdControl control{};
		if (context.eps_cpu->CopyLcd(lcd.data(), lcd.size(), &control) != lcd.size())
			return;
		float ink_alpha_on = 230.0f;
		float ink_alpha_off = 8.0f;
		ink_alpha_off = context.residual_enabled ?
			ink_alpha_off * context.residual_alpha_scale : 0.0f;
		if (!control.visible()) {
			ink_alpha_on = 0.0f;
			ink_alpha_off = 0.0f;
		}
		std::lock_guard<std::mutex> lock(context.eps_screen_alpha_mutex);
		for (size_t ix = 0; ix < context.status_indicators.size(); ++ix) {
			const auto& indicator = context.status_indicators[ix];
			const bool on = indicator.byte_offset < lcd.size() &&
				(lcd[indicator.byte_offset] & (1u << indicator.bit)) != 0;
			auto& alpha = context.eps_screen_ink_alpha[ix];
			alpha = alpha * transition_ratio +
				(on ? ink_alpha_on : ink_alpha_off) * (1 - transition_ratio);
		}
		return;
	}

	if (context.hardware_id == HW_EPS6800) {
		std::array<uint8_t, EPS6800_LCD_RAW_SIZE> lcd{};
		Eps6800LcdControl control{};
		if (context.eps_cpu->CopyLcd(lcd.data(), lcd.size(), &control) != lcd.size())
			return;
		float ink_alpha_on = Eps6800ActiveAlpha(control.contrast);
		float ink_alpha_off = Eps6800InactiveAlpha(control.contrast);
		ink_alpha_off = context.residual_enabled ?
			ink_alpha_off * context.residual_alpha_scale : 0.0f;
		if (!control.visible()) {
			ink_alpha_on = 0.0f;
			ink_alpha_off = 0.0f;
		}
		const auto decoded = DecodeEps6800Display(lcd.data(), lcd.size());
		std::lock_guard<std::mutex> lock(context.eps_screen_alpha_mutex);
		UpdateDotMatrixAlpha(
			decoded.pixels.data(),
			static_cast<int>(EPS6800_LCD_WIDTH),
			static_cast<int>(EPS6800_LCD_PIXEL_HEIGHT),
			context.eps_screen_ink_alpha,
			transition_ratio,
			ink_alpha_on,
			ink_alpha_off,
			context.response);
		UpdateStatusAlpha(
			decoded,
			context.status_indicators,
			context.eps_screen_ink_alpha,
			transition_ratio,
			ink_alpha_on,
			ink_alpha_off,
			context.response);
		return;
	}

	if (context.hardware_id == HW_EPS6800_W192) {
		std::array<uint8_t, EPS6800_W192_LCD_RAW_SIZE> lcd{};
		Eps6800LcdControl control{};
		if (context.eps_cpu->CopyLcd(lcd.data(), lcd.size(), &control) != lcd.size())
			return;
		float ink_alpha_on = Eps6800W192ActiveAlpha(control.contrast);
		float ink_alpha_off = Eps6800W192InactiveAlpha(control.contrast);
		ink_alpha_off = context.residual_enabled ?
			ink_alpha_off * context.residual_alpha_scale : 0.0f;
		if (!control.visible()) {
			ink_alpha_on = 0.0f;
			ink_alpha_off = 0.0f;
		}
		const auto decoded = DecodeEps6800W192Display(lcd.data(), lcd.size());
		std::lock_guard<std::mutex> lock(context.eps_screen_alpha_mutex);
		UpdateDotMatrixAlpha(
			decoded.pixels.data(),
			static_cast<int>(EPS6800_W192_LCD_WIDTH),
			static_cast<int>(EPS6800_W192_LCD_HEIGHT),
			context.eps_screen_ink_alpha,
			transition_ratio,
			ink_alpha_on,
			ink_alpha_off,
			context.response);
		UpdateStatusAlpha(
			decoded,
			context.status_indicators,
			context.eps_screen_ink_alpha,
			transition_ratio,
			ink_alpha_on,
			ink_alpha_off,
			context.response);
		return;
	}

	if (context.hardware_id == HW_EPS9500) {
		std::array<uint8_t, EPS9500_LCD_RAW_SIZE> lcd{};
		Eps6800LcdControl control{};
		if (context.eps_cpu->CopyLcd(lcd.data(), lcd.size(), &control) != lcd.size())
			return;
		float ink_alpha_on = Eps6800ActiveAlpha(control.contrast);
		float ink_alpha_off = Eps6800InactiveAlpha(control.contrast);
		ink_alpha_off = context.residual_enabled ?
			ink_alpha_off * context.residual_alpha_scale : 0.0f;
		if (!control.visible()) {
			ink_alpha_on = 0.0f;
			ink_alpha_off = 0.0f;
		}
		const auto decoded = DecodeEps9500Display(lcd.data(), lcd.size());
		std::lock_guard<std::mutex> lock(context.eps_screen_alpha_mutex);
		UpdateDotMatrixAlpha(
			decoded.pixels.data(),
			static_cast<int>(EPS9500_LCD_WIDTH),
			static_cast<int>(EPS9500_LCD_HEIGHT),
			context.eps_screen_ink_alpha,
			transition_ratio,
			ink_alpha_on,
			ink_alpha_off,
			context.response);
		UpdateStatusAlpha(
			decoded,
			context.status_indicators,
			context.eps_screen_ink_alpha,
			transition_ratio,
			ink_alpha_on,
			ink_alpha_off,
			context.response);
	}
}

} // namespace casioemu
