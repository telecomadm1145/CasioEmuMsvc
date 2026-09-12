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

struct EpsScreenTemporalState::Impl {
	bool valid = false;
	HardwareId hardware_id = HW_EPS6800;
	uint64_t epoch = 0;
	uint64_t seq = 0;
	uint64_t cutoff_ns = 0;
	bool residual_enabled = false;
	float residual_alpha_scale = 1.0f;
	std::vector<uint8_t> raw;
	Eps6800LcdControl control{};
	std::array<float, 66 * 192> targets{};
	std::array<uint64_t, 66 * 192> alpha_ns{};
	std::array<uint8_t, 66 * 192> active{};
};

EpsScreenTemporalState::EpsScreenTemporalState()
	: impl_(std::make_unique<Impl>()) {}

EpsScreenTemporalState::~EpsScreenTemporalState() = default;

void EpsScreenTemporalState::Reset() {
	impl_->valid = false;
}

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

struct EpsTargetLevels {
	float on = 0.0f;
	float off = 0.0f;
};

EpsTargetLevels TargetLevels(
	HardwareId hardware_id,
	const Eps6800LcdControl& control,
	bool residual_enabled,
	float residual_alpha_scale) {
	EpsTargetLevels levels;
	if (hardware_id == HW_EPS6009) {
		levels = {230.0f, 8.0f};
	}
	else if (hardware_id == HW_EPS6800_W192) {
		levels = {Eps6800W192ActiveAlpha(control.contrast),
			Eps6800W192InactiveAlpha(control.contrast)};
	}
	else {
		levels = {Eps6800ActiveAlpha(control.contrast),
			Eps6800InactiveAlpha(control.contrast)};
	}
	levels.off = residual_enabled ? levels.off * residual_alpha_scale : 0.0f;
	if (!control.visible())
		levels = {};
	return levels;
}

template <typename Visitor>
void VisitStatusSource(
	size_t byte_offset,
	uint8_t bit,
	bool on,
	const std::vector<StatusIndicatorInfo>& indicators,
	Visitor&& visitor) {
	for (size_t i = 0; i < indicators.size(); ++i) {
		if (indicators[i].byte_offset == byte_offset && indicators[i].bit == bit)
			visitor(i, on);
	}
}

template <typename Visitor>
bool VisitRawByteTargets(
	HardwareId hardware_id,
	size_t offset,
	uint8_t value,
	uint8_t changed_mask,
	const std::vector<StatusIndicatorInfo>& indicators,
	Visitor&& visitor) {
	if (hardware_id == HW_EPS6009) {
		for (uint8_t bit = 0; bit < 8; ++bit) {
			if (changed_mask & (1u << bit))
				VisitStatusSource(offset, bit, (value & (1u << bit)) != 0, indicators, visitor);
		}
		return true;
	}

	for (uint8_t bit = 0; bit < 8; ++bit) {
		if (!(changed_mask & (1u << bit)))
			continue;
		const bool on = (value & (1u << bit)) != 0;
		if (hardware_id == HW_EPS6800) {
			if (offset >= EPS6800_LCD_RAW_SIZE)
				return false;
			const size_t page = offset / EPS6800_LCD_WIDTH;
			const size_t x = offset % EPS6800_LCD_WIDTH;
			const size_t logical_y = 31 - (page * 8 + bit);
			if (logical_y == 0)
				VisitStatusSource(x >> 3, static_cast<uint8_t>(x & 7), on, indicators, visitor);
			else
				visitor(logical_y * 192 + x, on);
		}
		else if (hardware_id == HW_EPS6800_W192) {
			if (offset >= EPS6800_W192_LCD_RAW_SIZE)
				return false;
			const size_t page = offset / EPS6800_W192_LCD_WIDTH;
			const size_t x = offset % EPS6800_W192_LCD_WIDTH;
			const size_t serial_y = page * 8 + bit;
			if (serial_y == 0)
				VisitStatusSource(x >> 3, static_cast<uint8_t>(x & 7), on, indicators, visitor);
			else
				visitor(serial_y * 192 + x, on);
		}
		else if (hardware_id == HW_EPS9500) {
			if (offset >= EPS9500_LCD_RAW_SIZE)
				return false;
			const size_t page = offset / EPS9500_LCD_DEVICE_COUNT;
			const size_t device = offset % EPS9500_LCD_DEVICE_COUNT;
			if (device == 0)
				VisitStatusSource(page, bit, on, indicators, visitor);
			else if (device >= EPS9500_LCD_VISIBLE_DEVICE_FIRST &&
				device < EPS9500_LCD_VISIBLE_DEVICE_FIRST + EPS9500_LCD_WIDTH) {
				const size_t x = device - EPS9500_LCD_VISIBLE_DEVICE_FIRST;
				const size_t y = EPS9500_LCD_HEIGHT - 1 - (page * 8 + bit);
				visitor((y + 1) * 192 + x, on);
			}
		}
		else {
			return false;
		}
	}
	return true;
}

template <typename State>
void RebuildTargets(
	State& state,
	const std::vector<StatusIndicatorInfo>& indicators) {
	state.targets.fill(0.0f);
	state.active.fill(0);
	const auto levels = TargetLevels(state.hardware_id, state.control,
		state.residual_enabled, state.residual_alpha_scale);
	for (size_t offset = 0; offset < state.raw.size(); ++offset) {
		const uint8_t visible_value = state.control.all_pixels_on ? 0xff : state.raw[offset];
		VisitRawByteTargets(state.hardware_id, offset, visible_value, 0xff, indicators,
			[&](size_t index, bool on) {
				if (index >= state.targets.size())
					return;
				state.active[index] = 1;
				state.targets[index] = on ? levels.on : levels.off;
			});
	}
}

template <typename State>
void SettleAlpha(
	State& state,
	std::array<float, 66 * 192>& alpha,
	size_t index,
	uint64_t time_ns) {
	if (index >= alpha.size() || !state.active[index] || time_ns <= state.alpha_ns[index])
		return;
	const double elapsed_ms = static_cast<double>(time_ns - state.alpha_ns[index]) / 1000000.0;
	const auto config = lcd_response::ForHardware(state.hardware_id);
	const double rise = lcd_response::GainForElapsed(elapsed_ms, config.rise_half_life_ms);
	const double fall = lcd_response::GainForElapsed(elapsed_ms, config.fall_half_life_ms);
	alpha[index] = static_cast<float>(lcd_response::BlendWithGains(
		alpha[index], state.targets[index], rise, fall));
	state.alpha_ns[index] = time_ns;
}

template <typename State>
void SettleAll(
	State& state,
	std::array<float, 66 * 192>& alpha,
	uint64_t time_ns) {
	for (size_t i = 0; i < alpha.size(); ++i) {
		if (state.active[i])
			SettleAlpha(state, alpha, i, time_ns);
	}
}

template <typename State>
bool ReplayHistory(
	EpsScreenContext& context,
	const EpsLcdHistoryBatch& batch,
	State& state) {
	if (!batch.complete || batch.baseline.raw.empty() ||
		batch.baseline.epoch != batch.cutoff.epoch ||
		batch.baseline.steady_ns > batch.cutoff.steady_ns)
		return false;

	if (!state.valid) {
		state.valid = true;
		state.hardware_id = context.hardware_id;
		state.epoch = batch.baseline.epoch;
		state.seq = batch.baseline.seq;
		state.cutoff_ns = batch.baseline.steady_ns;
		state.residual_enabled = context.residual_enabled;
		state.residual_alpha_scale = context.residual_alpha_scale;
		state.raw = batch.baseline.raw;
		state.control = batch.baseline.control;
		RebuildTargets(state, context.status_indicators);
		for (size_t i = 0; i < state.alpha_ns.size(); ++i)
			state.alpha_ns[i] = state.active[i] ? state.cutoff_ns : 0;
	}
	else if (state.hardware_id != context.hardware_id || state.epoch != batch.baseline.epoch ||
		state.seq != batch.baseline.seq || state.cutoff_ns != batch.baseline.steady_ns ||
		state.raw != batch.baseline.raw || !(state.control == batch.baseline.control) ||
		state.residual_enabled != context.residual_enabled ||
		state.residual_alpha_scale != context.residual_alpha_scale) {
		state.valid = false;
		return false;
	}

	uint64_t expected_seq = state.seq;
	uint64_t timeline_ns = state.cutoff_ns;
	for (const auto& event : batch.events) {
		if (event.seq != ++expected_seq || event.steady_ns < timeline_ns ||
			event.steady_ns > batch.cutoff.steady_ns) {
			state.valid = false;
			return false;
		}
		timeline_ns = event.steady_ns;
		if (!(event.control == state.control)) {
			SettleAll(state, context.eps_screen_ink_alpha, timeline_ns);
			state.control = event.control;
			RebuildTargets(state, context.status_indicators);
		}
		if (event.offset != EpsLcdHistoryEvent::kNoByte) {
			if (event.offset >= state.raw.size() || state.raw[event.offset] != event.old_value) {
				state.valid = false;
				return false;
			}
			const uint8_t changed = event.old_value ^ event.new_value;
			if (!state.control.all_pixels_on &&
				!VisitRawByteTargets(state.hardware_id, event.offset, event.new_value, changed,
				context.status_indicators, [&](size_t index, bool on) {
					if (index >= state.targets.size())
						return;
					SettleAlpha(state, context.eps_screen_ink_alpha, index, timeline_ns);
					const auto levels = TargetLevels(state.hardware_id, state.control,
						state.residual_enabled, state.residual_alpha_scale);
					state.active[index] = 1;
					state.targets[index] = on ? levels.on : levels.off;
				})) {
				state.valid = false;
				return false;
			}
			state.raw[event.offset] = event.new_value;
		}
	}
	if (expected_seq != batch.cutoff.seq || state.raw != batch.cutoff.raw ||
		!(state.control == batch.cutoff.control)) {
		state.valid = false;
		return false;
	}

	SettleAll(state, context.eps_screen_ink_alpha, batch.cutoff.steady_ns);
	state.seq = batch.cutoff.seq;
	state.cutoff_ns = batch.cutoff.steady_ns;
	return true;
}

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
	context.eps_cpu->SetLcdHistoryEnabled(context.response.enabled);
	if (context.response.enabled) {
		EpsLcdHistoryBatch batch;
		if (context.eps_cpu->ConsumeLcdHistory(batch) && batch.complete) {
			std::lock_guard<std::mutex> lock(context.eps_screen_alpha_mutex);
			if (ReplayHistory(context, batch, *context.temporal_state.impl_))
				return;
		}
		context.temporal_state.Reset();
	}
	else {
		context.temporal_state.Reset();
	}

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
			alpha = BlendAlpha(alpha, on ? ink_alpha_on : ink_alpha_off,
				transition_ratio, context.response);
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
