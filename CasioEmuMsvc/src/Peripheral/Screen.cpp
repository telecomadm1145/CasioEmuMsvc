/*

		Screen peripheral implement.
		Copyright (C) 2024 telecomadm1145/Xyzst/user202729/LBPHacker/hieuxyz

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
#include "Screen.hpp"
#include "ScreenOutput.hpp"
#include "ScreenRenderSupport.hpp"
#include "SolarIIScreen.hpp"
#include "EpsScreen.hpp"
#include "LcdResponse.hpp"
#include "OrdinaryLcdHistory.hpp"
#include "OrdinaryLcdTarget.hpp"
#include "OrdinaryLcdFrame.hpp"
#include "ScreenScan.hpp"
#include "ScreenScanVisual.hpp"
#include "LcdTemporalReplay.hpp"
#include "Chipset/Chipset.hpp"
#include "Chipset/ePSCpu.h"
#include "Chipset/MMU.hpp"
#include "Chipset/MMURegion.hpp"
#include "Emulator.hpp"
#include "Ext/Random.hpp"
#include "Gui/HwController.h"
#include "Logger.hpp"
#include "ML620Ports.h"
#include "ModelInfo.h"
#include "Models.h"
#include "Ui.hpp"
#include <algorithm> // for std::min, std::max
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

extern bool low_perf_ext;

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include "Theme.h"
#endif

inline constexpr uint8_t reverse_bits(uint8_t n) {
	uint8_t reversed = 0;
	for (int i = 0; i < 8; ++i) {
		reversed |= ((n >> i) & 1) << (7 - i);
	}
	return reversed;
}

// constexpr 生成查找表
inline constexpr std::array<uint8_t, 256> generate_lookup_table() {
	std::array<uint8_t, 256> table = {};
	for (int i = 0; i < 256; ++i) {
		table[i] = reverse_bits(static_cast<uint8_t>(i));
	}
	return table;
}

// 定义查找表
constexpr auto bit_lookup_table = generate_lookup_table();

inline void fillRandomData(unsigned char* buf, size_t size) {
	util::Random::fillRandomBytes(reinterpret_cast<std::uint8_t*>(buf), size);
}

#pragma warning(disable : 4244)

namespace casioemu {

	using SpriteBitmap = ordinary_lcd::SpriteSpec;
	inline int update_screen_scan_alpha(float* alpha, std::array<float, 64>& curve,
		float& coefficient, bool& valid, Uint64 time, int rate, int threshold) {
		return screen_scan::UpdateScanAlpha(alpha, curve, coefficient, valid,
			time, rate, threshold, screen_flashing_brightness_coeff);
	}
	template <HardwareId hardware_id>
	class Screen : public Peripheral, public IScreenFrameProvider {
		static constexpr bool kCaptureLcdHistory = ordinary_lcd_history::kEnabled &&
			(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II ||
			 hardware_id == HW_ES_PLUS || hardware_id == HW_FX_5800P);
		using LcdHistory = std::conditional_t<kCaptureLcdHistory,
			ordinary_lcd_history::History, ordinary_lcd_history::DisabledHistory>;
		using LcdHistoryWorker = std::conditional_t<kCaptureLcdHistory,
			ordinary_lcd_history::WorkerState, ordinary_lcd_history::DisabledWorkerState>;

		static int const N_ROW,
			ROW_SIZE,
			OFFSET,
			ROW_SIZE_DISP,
			SPR_MAX;

		static size_t RowBufferSize() {
			return (static_cast<size_t>(N_ROW) + 1) * static_cast<size_t>(ROW_SIZE);
		}

		MMURegion region_buffer{}, region_buffer1{}, region_contrast{}, region_brightness{}, region_scan_report_op1{}, region_mode{}, region_range{}, region_select{}, region_offset{}, region_refresh_rate{}, region_scan_report{};
		uint8_t* screen_buffer{}, * screen_buffer1{}, screen_contrast{}, screen_brightness{}, screen_scan_report_op1{}, screen_mode{}, screen_range{}, screen_select{}, screen_offset{}, screen_refresh_rate{}, screen_scan_report{};

		MMURegion region_power{}, region_scan_report_en{};
		uint8_t screen_power{}, screen_scan_report_en{};

		MMURegion region_unk1{}, region_unk2{};

		uint8_t unk_f034{};

		// TI things

		MMURegion ti_port7_data{}, ti_port5_data{};
		int ti_contrast{}, ti_port_status{};
		bool ti_enabled = 0;
		bool ti_a0 = 0;
		bool ti_rw = 0;
		int ti_col = 0;
		int ti_page = 0;

		int ti_port7{};
		int ti_port5{};

		float screen_scan_alpha[64]{};
		std::array<float, 64> screen_scan_curve{};
		float screen_scan_curve_coeff = 0.0f;
		bool screen_scan_curve_valid = false;
		std::array<double, 66 * 192> lcd_response_alpha{};
		bool lcd_response_active = false;
		std::chrono::steady_clock::time_point lcd_response_last_tick{};
		std::atomic_bool lcd_response_reset_requested{false};
		screen_scan::State scan_report_state;
		LcdHistory lcd_history;
		LcdHistoryWorker lcd_history_worker;
		bool scan_history_bound = false;
		std::unique_ptr<lcd_temporal::Baseline> temporal_baseline;
		std::unique_ptr<lcd_temporal::ReplaySession> temporal_replay;
		bool temporal_valid = false;
		std::chrono::steady_clock::time_point temporal_retry_after{};
		static constexpr std::chrono::milliseconds kTemporalInterval{2};
		static constexpr std::chrono::milliseconds kTemporalRecoveryInterval{50};
		float position = 0;
		SDL_Renderer* renderer{};
		SDL_Texture* interface_texture{};
#ifndef CASIOEMU_CORE_WEB
		SDL_Texture* pixel_screen_texture{};
		int pixel_screen_texture_width = 0;
		int pixel_screen_texture_height = 0;
		std::vector<uint8_t> pixel_screen_pixels;
#endif
		float screen_ink_alpha[66 * 192]{};
		std::array<float, 66 * 192> eps_screen_ink_alpha{};
		std::mutex eps_screen_alpha_mutex;
		EpsScreenTemporalState eps_temporal_state;
		bool eps_lcd_response_active = false;
		std::chrono::steady_clock::time_point eps_lcd_response_last_tick{};
		std::atomic_bool screen_thread_running{false};
		std::thread screen_thread;
		mutable std::mutex screen_state_mutex;
		mutable std::condition_variable screen_state_ready;
		mutable std::atomic_uint screen_state_waiters{0};
#if !defined(__EMSCRIPTEN__) && !defined(CASIOEMU_CORE_WEB)
		std::mutex output_resource_mutex;
		ScreenOutput* output = nullptr;
#endif
		static const SpriteBitmap sprite_bitmap[];
		std::vector<SpriteInfo> sprite_info;
		std::vector<SvgSpriteTextureCache> sprite_svg_textures;
		std::vector<uint8_t> sprite_available;
		ColourInfo ink_colour{};

		bool inited = 0;
		bool enabled_2 = 0;
		int status_ink_alpha_on = 255;
		int status_ink_alpha_off = 0;

		std::unique_lock<std::recursive_mutex> LockLcdMutation() const {
			if constexpr (kCaptureLcdHistory)
				return lcd_history.Lock();
			return {};
		}

		template <ordinary_lcd_history::Kind kind, uint8_t mask, uint8_t Screen::*member>
		void SetupLcdControl(MMURegion& region, size_t address, const char* name) {
			if constexpr (!kCaptureLcdHistory) {
				region.Setup(address, 1, name, &(this->*member),
					MMURegion::DefaultRead<uint8_t, mask>, MMURegion::DefaultWrite<uint8_t, mask>, emulator);
			}
			else {
				region.Setup(address, 1, name, this,
					[](MMURegion* region, size_t) {
						return static_cast<uint8_t>((static_cast<Screen*>(region->userdata)->*member) & mask);
					},
					[](MMURegion* region, size_t, uint8_t data) {
						auto* screen = static_cast<Screen*>(region->userdata);
						auto history_lock = screen->LockLcdMutation();
						struct WriteContext {
							Screen* screen;
							uint8_t data;
						} context{screen, static_cast<uint8_t>(data & mask)};
						const auto write = [](void* raw) noexcept {
							auto* context = static_cast<WriteContext*>(raw);
							context->screen->*member = context->data;
						};
						const auto prepare = [](void* raw, ordinary_lcd_history::Event& event) noexcept {
							auto* context = static_cast<WriteContext*>(raw);
							event.kind = kind;
							event.old_value = context->screen->*member;
							event.new_value = context->data;
							return event.Changes() ? ordinary_lcd_history::PrepareResult::Commit
								: ordinary_lcd_history::PrepareResult::NoChange;
						};
						if (screen->lcd_history.TryRecord(prepare, write, &context) !=
							ordinary_lcd_history::TransactionResult::Committed)
							write(&context);
					}, emulator);
			}
		}

		// Announce readers/lifecycle operations before waiting for the state lock.
		// The unthrottled LCD worker must let them through before its next tick.
		std::unique_lock<std::mutex> LockScreenState() const {
			screen_state_waiters.fetch_add(1);
			std::unique_lock<std::mutex> lock(screen_state_mutex, std::defer_lock);
			try {
				lock.lock();
			}
			catch (...) {
				screen_state_waiters.fetch_sub(1);
				screen_state_ready.notify_one();
				throw;
			}
			screen_state_waiters.fetch_sub(1);
			screen_state_ready.notify_one();
			return lock;
		}

		struct LcdResponseTick {
			bool enabled = false;
			double rise_gain = 0.0;
			double fall_gain = 0.0;
		};

		bool LcdResponseEligible() const {
			if constexpr (!(hardware_id == HW_FX_5800P || hardware_id == HW_ES_PLUS ||
				hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II)) {
				return false;
			}
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
			return false;
#else
			return !ThemeManager::Instance().Settings().lowPerformanceMode && !low_perf_ext;
#endif
		}

		LcdResponseTick BeginLcdResponseTick() {
			if (!LcdResponseEligible()) {
				lcd_response_active = false;
				return {};
			}

			const auto now = std::chrono::steady_clock::now();
			double elapsed_ms = 0.0;
			if (lcd_response_reset_requested.exchange(false, std::memory_order_acq_rel) ||
				!lcd_response_active) {
				for (size_t i = 0; i < lcd_response_alpha.size(); ++i)
					lcd_response_alpha[i] = static_cast<double>(screen_ink_alpha[i]);
				lcd_response_active = true;
				lcd_response_last_tick = now;
			}
			else {
				elapsed_ms = std::chrono::duration<double, std::milli>(now - lcd_response_last_tick).count();
				lcd_response_last_tick = now;
				if (!(elapsed_ms > 0.0))
					elapsed_ms = 0.0;
			}

			const auto config = lcd_response::ForHardware(hardware_id);
			return {true,
				lcd_response::GainForElapsed(elapsed_ms, config.rise_half_life_ms),
				lcd_response::GainForElapsed(elapsed_ms, config.fall_half_life_ms)};
		}

		EpsLcdResponseTick BeginEpsLcdResponseTick() {
			if constexpr (!IsEpsFamily(hardware_id)) {
				return {};
			}
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
			eps_lcd_response_active = false;
			return {};
#else
			if (ThemeManager::Instance().Settings().lowPerformanceMode || low_perf_ext) {
				eps_lcd_response_active = false;
				return {};
			}

			const auto now = std::chrono::steady_clock::now();
			double elapsed_ms = 0.0;
			if (lcd_response_reset_requested.exchange(false, std::memory_order_acq_rel) ||
				!eps_lcd_response_active) {
				eps_lcd_response_active = true;
			}
			else {
				elapsed_ms = std::chrono::duration<double, std::milli>(
					now - eps_lcd_response_last_tick).count();
				if (!(elapsed_ms > 0.0))
					elapsed_ms = 0.0;
			}
			eps_lcd_response_last_tick = now;

			const auto config = lcd_response::ForHardware(hardware_id);
			return {true,
				lcd_response::GainForElapsed(elapsed_ms, config.rise_half_life_ms),
				lcd_response::GainForElapsed(elapsed_ms, config.fall_half_life_ms)};
#endif
		}

		void ApplyLcdAlpha(
			size_t index,
			float target,
			float ratio,
			const LcdResponseTick& response) {
			if (!response.enabled) {
				screen_ink_alpha[index] = screen_ink_alpha[index] * ratio + target * (1 - ratio);
				return;
			}

			double& alpha = lcd_response_alpha[index];
			alpha = lcd_response::BlendWithGains(
				alpha, static_cast<double>(target), response.rise_gain, response.fall_gain);
			screen_ink_alpha[index] = static_cast<float>(alpha);
		}

		void ApplyLcdAlphaDecay(
			size_t first,
			size_t last,
			float ratio,
			const LcdResponseTick& response) {
			if (!response.enabled) {
				for (size_t i = first; i < last; ++i)
					screen_ink_alpha[i] *= ratio;
				return;
			}
			for (size_t i = first; i < last; ++i)
				ApplyLcdAlpha(i, 0.0f, ratio, response);
		}

		void SetupIndependentScanRegions() {
			if constexpr (screen_scan::kEnableIndependentScanReport &&
				(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II)) {
				if (!region_scan_report_op1.setup_done)
					region_scan_report_op1.Setup(
						0xF035, 1, "Screen/ScanReportOption1", &scan_report_state,
						screen_scan::ReadOption1, screen_scan::WriteOption1, emulator);
				if (!region_scan_report_en.setup_done)
					region_scan_report_en.Setup(
						0xF036, 1, "Screen/ScanReportOptionEnable", &scan_report_state,
						screen_scan::ReadOptionEnable, screen_scan::WriteOptionEnable, emulator);
				if (!region_scan_report.setup_done)
					region_scan_report.Setup(
						0xF03B, 1, "Screen/ScanReport", &scan_report_state,
						screen_scan::ReadReport, MMURegion::IgnoreWrite, emulator);
				if (!region_refresh_rate.setup_done)
					region_refresh_rate.Setup(
						0xF034, 1, "Screen/RefreshRate", &scan_report_state,
						screen_scan::ReadRefreshRate, screen_scan::WriteRefreshRate, emulator);
			}
		}

		// Called with Screen held. CPU producers only take History -> Scan;
		// neither target evaluation nor replay holds their mutation lock.
		// -1: legacy fallback, 0: pending cutoff, 1: complete publication.
		int TemporalTick() {
			if constexpr (!kCaptureLcdHistory) {
				return -1;
			}
			else {
				const auto now = std::chrono::steady_clock::now();
				{
					auto settings_lock = ordinary_lcd_history::UntrackedChange::LockSettings();
					if (!LcdResponseEligible() || screen_buffer_select != 0) {
						if (temporal_valid) lcd_history.InvalidateEpoch();
						temporal_valid = false;
						lcd_history_worker.cutoff_pending = false;
						return -1;
					}
				}
				if (now < temporal_retry_after) return -1;
				const auto fail = [&]() {
					// Legacy tick does not consume history. Stop recording until
					// recovery establishes a new baseline and resets the queue.
					lcd_history.InvalidateEpoch();
					temporal_valid = false;
					lcd_history_worker.cutoff_pending = false;
					temporal_retry_after = std::chrono::steady_clock::now() + kTemporalRecoveryInterval;
					return -1;
				};
				if (lcd_history.Incomplete() || lcd_response_reset_requested.load(std::memory_order_acquire))
					temporal_valid = false;
				if (!temporal_replay) temporal_replay = std::make_unique<lcd_temporal::ReplaySession>();
				if (!temporal_valid) {
					if (!temporal_baseline) {
						temporal_baseline = std::make_unique<lcd_temporal::Baseline>();
						temporal_baseline->primary.resize(RowBufferSize());
						if constexpr (hardware_id == HW_CLASSWIZ_II)
							temporal_baseline->secondary.resize(RowBufferSize());
						temporal_baseline->sprites.assign(sprite_bitmap + 1, sprite_bitmap + SPR_MAX);
					}
					auto& b = *temporal_baseline;
					bool cold = !lcd_response_active || lcd_response_reset_requested.load(std::memory_order_acquire);
					for (size_t i = 0; i != b.alpha.size(); ++i)
						b.alpha[i] = cold ? static_cast<double>(screen_ink_alpha[i]) : lcd_response_alpha[i];
					b.status_levels = {status_ink_alpha_on, status_ink_alpha_off};
					std::copy_n(screen_scan_alpha, 64, b.scan_alpha.begin());
					{
						auto settings_lock = ordinary_lcd_history::UntrackedChange::LockSettings();
						auto history_lock = LockLcdMutation();
						if (!LcdResponseEligible() || screen_buffer_select != 0 || !screen_buffer) return fail();
						if (lcd_response_reset_requested.load(std::memory_order_acquire)) {
							cold = true;
							std::copy_n(screen_ink_alpha, b.alpha.size(), b.alpha.begin());
						}
						ordinary_lcd_history::ScanSnapshot scan;
						if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
							scan_report_state.Advance(SDL_GetTicks64(), screen_scan::CurrentGate());
							scan = scan_report_state.CaptureSnapshot();
						}
						else {
							const auto gate = screen_scan::CurrentGate();
							// ES/5800 F034 is unrelated to the visual rate. Only
							// lifecycle/load can change this legacy visual field.
							screen_refresh_rate = std::max<uint8_t>(screen_refresh_rate, 6);
							scan = {screen_refresh_rate, screen_refresh_rate,
								screen_scan_report_op1, screen_scan_report_en,
								gate.flashing_threshold, gate.fading_enabled, gate.version, true, true};
						}
						b.scan = {scan.raw_rate, scan.effective_rate, scan.option1, scan.option_enable,
							scan.flashing_threshold, scan.fading_enabled, scan.gate_version,
							scan.active, scan.gate_initialized, 0};
						b.controls = {hardware_id, N_ROW, ROW_SIZE, ROW_SIZE_DISP,
							screen_mode, screen_select, screen_range, screen_offset, screen_brightness,
							screen_contrast, screen_power, enabled_2, false, screen_residual_enabled,
							screen_residual_alpha_scale, screen_flashing_brightness_coeff};
						std::copy_n(screen_buffer, b.primary.size(), b.primary.begin());
						if constexpr (hardware_id == HW_CLASSWIZ_II)
							std::copy_n(screen_buffer1, b.secondary.size(), b.secondary.begin());
						// All producers, including invalidating/fallback writes, are
						// excluded through the same recursive mutation lock.
						lcd_history.Reset();
						lcd_history_worker.cutoff = lcd_history.CaptureCutoff();
						b.coverage_complete = !lcd_history.Incomplete();
					}
					const auto& cutoff = lcd_history_worker.cutoff;
					b.epoch = cutoff.epoch; b.seq = cutoff.end_seq;
					b.steady_ns = cutoff.steady_ns; b.sdl_ms = cutoff.sdl_ms;
					b.scan.sdl_ms = b.sdl_ms;
					b.alpha_steady_ns.fill(b.steady_ns);
					const uint64_t previous_ns = cold ? b.steady_ns : static_cast<uint64_t>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(lcd_response_last_tick.time_since_epoch()).count());
					if (!temporal_replay->BeginFromLive(b, cutoff, previous_ns) || !temporal_replay->Finish()) return fail();
					temporal_valid = true;
					lcd_history_worker.cutoff_pending = false;
				}
				else {
					if (!lcd_history_worker.cutoff_pending) {
						if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II)
							scan_report_state.Advance(SDL_GetTicks64(), screen_scan::CurrentGate());
						lcd_history_worker.cutoff = lcd_history.CaptureCutoff();
						if (!temporal_replay->Continue(lcd_history_worker.cutoff)) return fail();
						lcd_history_worker.cutoff_pending = true;
					}
					const auto consumed = lcd_history.ConsumeUntil(lcd_history_worker.cutoff, lcd_history_worker.batch);
					if (!temporal_replay->AppendBatch(lcd_history_worker.batch, consumed)) return fail();
					if (!consumed.cutoff_complete) return 0;
					lcd_history_worker.cutoff_pending = false;
				}
				const auto* result = temporal_replay->Result();
				if (!result) return fail();
				{
					auto settings_lock = ordinary_lcd_history::UntrackedChange::LockSettings();
					auto history_lock = LockLcdMutation();
					if (!lcd_history.Covers(lcd_history_worker.cutoff) ||
						!LcdResponseEligible() || screen_buffer_select != 0) return fail();
					lcd_response_reset_requested.store(false, std::memory_order_release);
				} // Publication commits here; subsequent mutations belong to the next observation.
				{
					// Publish only the complete private frame. Newer queued writes
					// belong to the next cutoff, not this observation time.
					lcd_response_alpha = result->alpha;
					for (size_t i = 0; i != result->alpha.size(); ++i)
						screen_ink_alpha[i] = static_cast<float>(result->alpha[i]);
					std::copy(result->scan_alpha.begin(), result->scan_alpha.end(), screen_scan_alpha);
					status_ink_alpha_on = result->status_levels.ink_alpha_on;
					status_ink_alpha_off = result->status_levels.ink_alpha_off;
					lcd_response_last_tick = std::chrono::steady_clock::time_point(
						std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::nanoseconds(result->steady_ns)));
					lcd_response_active = true;
				}
				return 1;
			}
		}
		void StartUpdateThread();
		void StopUpdateThread();

		int SpriteCount() const {
			if constexpr (IsEpsFamily(hardware_id))
				return static_cast<int>(emulator.ModelDefinition.status_indicators.size()) + 1;
			return SPR_MAX;
		}

		bool StatusEnabled() const {
			if (!(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) && hardware_id != HW_FX_5800P && hardware_id != HW_ES_PLUS && !IsEpsFamily(hardware_id)) {
				return true;
			}
			if (!enabled_2 || (screen_range & 0b100000)) {
				return false;
			}
			const auto mode = screen_mode & 7;
			return mode == 5 || mode == 6;
		}

		uint8_t LogicalAlpha(float value) const {
			return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(value * 255.0f)), 0, 255));
		}

		static constexpr float kClassWizIILowerPlaneWeight = 1.0f / 3.0f;
		static constexpr float kClassWizIIUpperPlaneWeight = 2.0f / 3.0f;

		uint8_t ClassWizIIStatusAlpha(uint8_t offset, uint8_t mask) const {
			if (!StatusEnabled() || !screen_buffer || !screen_buffer1) return 0;
			const auto gate = screen_gate::Get();
			const auto status_offset = (offset + screen_offset * ROW_SIZE) % RowBufferSize();
			const bool lower = (screen_buffer[status_offset] & mask) != 0;
			const bool upper = (screen_buffer1[status_offset] & mask) != 0;
			if (!screen_residual_enabled) {
				return LogicalAlpha((lower ? kClassWizIILowerPlaneWeight : 0.0f) + (upper ? kClassWizIIUpperPlaneWeight : 0.0f));
			}
			float alpha = static_cast<float>(status_ink_alpha_off);
			alpha += (static_cast<float>(status_ink_alpha_on - status_ink_alpha_off)) * (lower ? kClassWizIILowerPlaneWeight : 0.0f);
			alpha += (static_cast<float>(status_ink_alpha_on - status_ink_alpha_off)) * (upper ? kClassWizIIUpperPlaneWeight : 0.0f);
			if (screen_refresh_rate >= gate.flashing_threshold) {
				alpha *= screen_scan_alpha[0];
			}
			return static_cast<uint8_t>(std::clamp(static_cast<int>(alpha), 0, 255));
		}

#ifndef CASIOEMU_CORE_WEB
		void ResetPixelScreenTexture() {
			if (pixel_screen_texture) {
				SDL_DestroyTexture(pixel_screen_texture);
				pixel_screen_texture = nullptr;
			}
			pixel_screen_texture_width = 0;
			pixel_screen_texture_height = 0;
			pixel_screen_pixels.clear();
		}

		bool EnsurePixelScreenTexture(int width, int height) {
			if (!renderer || width <= 0 || height <= 0)
				return false;
			if (pixel_screen_texture && pixel_screen_texture_width == width && pixel_screen_texture_height == height)
				return true;

			ResetPixelScreenTexture();
			pixel_screen_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height);
			if (!pixel_screen_texture) {
				SDL_Log("[Screen][Warn] SDL_CreateTexture failed for pixel screen: %s", SDL_GetError());
				return false;
			}
			SDL_SetTextureBlendMode(pixel_screen_texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
			SDL_SetTextureScaleMode(pixel_screen_texture, SDL_ScaleModeNearest);
#endif
			pixel_screen_texture_width = width;
			pixel_screen_texture_height = height;
			pixel_screen_pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
			return true;
		}

		void WritePixelScreenTexture(int logical_width, int logical_height, const float* alpha_buffer) {
			if (!alpha_buffer)
				return;
			if (pixel_screen_pixels.size() < static_cast<size_t>(logical_width) * static_cast<size_t>(logical_height) * 4)
				return;
			for (int y = 0; y != logical_height; ++y) {
				const int source_y = y + 1;
				for (int x = 0; x != logical_width; ++x) {
					const float alpha_value = alpha_buffer[x + source_y * 192];
					const SDL_Color colour = ScreenPixelColour(ink_colour, alpha_value);

					const size_t pixel_offset = (static_cast<size_t>(y) * static_cast<size_t>(logical_width) + static_cast<size_t>(x)) * 4;
					pixel_screen_pixels[pixel_offset + 0] = colour.r;
					pixel_screen_pixels[pixel_offset + 1] = colour.g;
					pixel_screen_pixels[pixel_offset + 2] = colour.b;
					pixel_screen_pixels[pixel_offset + 3] = colour.a;
				}
			}
		}

		bool RenderPixelScreenTexture(const SDL_Rect& lcd_dest, int logical_width, int logical_height, const float* alpha_buffer) {
			if (!EnsurePixelScreenTexture(logical_width, logical_height))
				return false;
			WritePixelScreenTexture(logical_width, logical_height, alpha_buffer);
			if (SDL_UpdateTexture(pixel_screen_texture, nullptr, pixel_screen_pixels.data(), logical_width * 4) != 0) {
				SDL_Log("[Screen][Warn] SDL_UpdateTexture failed for pixel screen: %s", SDL_GetError());
				return false;
			}
			if (SDL_RenderCopy(renderer, pixel_screen_texture, nullptr, &lcd_dest) != 0) {
				SDL_Log("[Screen][Warn] SDL_RenderCopy failed for pixel screen: %s", SDL_GetError());
				return false;
			}
			return true;
		}
#endif

	public:
		Screen(Emulator& emu)
			: Peripheral(emu) {
		}
		~Screen() {
			StopUpdateThread();
#if !defined(__EMSCRIPTEN__) && !defined(CASIOEMU_CORE_WEB)
			{
				std::lock_guard<std::mutex> output_lock(output_resource_mutex);
				if (output) {
					output->Stop();
					delete output;
					output = nullptr;
				}
			}
#endif
			for (auto& texture : sprite_svg_textures)
				texture.Reset();
#ifndef CASIOEMU_CORE_WEB
			ResetPixelScreenTexture();
#endif
			if (screen_buffer)
				delete[] screen_buffer;
			if (screen_buffer1)
				delete[] screen_buffer1;
		}
		void Initialise() override;
		void Uninitialise() override;
		void Frame() override;
		void Reset() override;
		void* QueryInterface(const char* name) override {
			if (strcmp(name, typeid(IScreenFrameProvider).name()) == 0) {
				return static_cast<IScreenFrameProvider*>(this);
			}
			return Peripheral::QueryInterface(name);
		}
		void UpdateFrameAlpha() override {
#ifdef __EMSCRIPTEN__
			auto state_lock = LockScreenState();
			tick();
			if constexpr (IsEpsFamily(hardware_id)) {
				std::lock_guard<std::mutex> lock(eps_screen_alpha_mutex);
				std::copy(eps_screen_ink_alpha.begin(), eps_screen_ink_alpha.end(), screen_ink_alpha);
			}
#endif
		}
		int GetFrameWidth() const override {
			if constexpr (IsEpsFamily(hardware_id)) {
				return GetEpsScreenSpec(
					hardware_id,
					emulator.ModelDefinition.screen_width,
					emulator.ModelDefinition.screen_height).export_width;
			}
			return hardware_id == HW_EPS6800 || hardware_id == HW_EPS9500 ? 96 : 192;
		}
		int GetFrameHeight() const override {
			if constexpr (IsEpsFamily(hardware_id)) {
				return GetEpsScreenSpec(
					hardware_id,
					emulator.ModelDefinition.screen_width,
					emulator.ModelDefinition.screen_height).export_height;
			}
			return hardware_id == HW_FX_5800P || hardware_id == HW_ES_PLUS ||
				hardware_id == HW_EPS6800 ? 32 : 64;
		}
		void WriteFrameRgba(uint8_t* out, int r, int g, int b) const override {
			if (!out) return;
			std::array<float, 66 * 192> alpha_snapshot{};
			{
				auto state_lock = LockScreenState();
				std::copy(std::begin(screen_ink_alpha), std::end(screen_ink_alpha), alpha_snapshot.begin());
			}
			const int width = GetFrameWidth();
			const int height = GetFrameHeight();
			if constexpr (hardware_id == HW_EPS6009) {
				std::fill(out, out + static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
				return;
			}
			for (int y = 0; y < height; ++y) {
				for (int x = 0; x < width; ++x) {
					const float alpha = alpha_snapshot[y * 192 + x];
					const int idx = (y * width + x) * 4;
					out[idx + 0] = static_cast<uint8_t>(std::clamp(r, 0, 255));
					out[idx + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
					out[idx + 2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
					out[idx + 3] = static_cast<uint8_t>(std::clamp(static_cast<int>(alpha), 0, 255));
				}
			}
		}
		int GetStatusAlphaCount() const override {
			return std::max(0, SpriteCount() - 1);
		}
		void WriteStatusAlpha(uint8_t* out, int max_len) const override {
			if (!out || max_len <= 0) return;
			const int count = std::min(max_len, GetStatusAlphaCount());
			for (int i = 0; i < count; ++i) out[i] = 0;
			std::array<float, 66 * 192> alpha_snapshot{};
			{
				auto state_lock = LockScreenState();
				std::copy(std::begin(screen_ink_alpha), std::end(screen_ink_alpha), alpha_snapshot.begin());
			}
			for (int i = 0; i < count; ++i) {
				out[i] = static_cast<uint8_t>(std::clamp(static_cast<int>(alpha_snapshot[i]), 0, 255));
			}
		}
		void SaveState(std::ostream& os) override {
			size_t bufSize = (hardware_id == HW_TI) ? (192 * 9) : RowBufferSize();
			if (screen_buffer)
				os.write(reinterpret_cast<const char*>(screen_buffer), bufSize);
			uint8_t hasBuf1 = (screen_buffer1 != nullptr) ? 1 : 0;
			os.write(reinterpret_cast<const char*>(&hasBuf1), 1);
			if (screen_buffer1)
				os.write(reinterpret_cast<const char*>(screen_buffer1), bufSize);
			os.write(reinterpret_cast<const char*>(&screen_contrast), 1);
			os.write(reinterpret_cast<const char*>(&screen_brightness), 1);
			os.write(reinterpret_cast<const char*>(&screen_mode), 1);
			os.write(reinterpret_cast<const char*>(&screen_range), 1);
			os.write(reinterpret_cast<const char*>(&screen_offset), 1);
			if constexpr (screen_scan::kEnableIndependentScanReport &&
				(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II)) {
				const uint8_t refresh_rate = scan_report_state.GetRateForState();
				os.write(reinterpret_cast<const char*>(&refresh_rate), 1);
			}
			else {
				os.write(reinterpret_cast<const char*>(&screen_refresh_rate), 1);
			}
			os.write(reinterpret_cast<const char*>(&screen_power), 1);
		}
		void LoadState(std::istream& is) override {
			auto history_lock = LockLcdMutation();
			if constexpr (kCaptureLcdHistory)
				lcd_history.InvalidateEpoch();
			size_t bufSize = (hardware_id == HW_TI) ? (192 * 9) : RowBufferSize();
			if (screen_buffer)
				is.read(reinterpret_cast<char*>(screen_buffer), bufSize);
			uint8_t hasBuf1 = 0;
			is.read(reinterpret_cast<char*>(&hasBuf1), 1);
			if (hasBuf1 && screen_buffer1)
				is.read(reinterpret_cast<char*>(screen_buffer1), bufSize);
			is.read(reinterpret_cast<char*>(&screen_contrast), 1);
			is.read(reinterpret_cast<char*>(&screen_brightness), 1);
			is.read(reinterpret_cast<char*>(&screen_mode), 1);
			is.read(reinterpret_cast<char*>(&screen_range), 1);
			is.read(reinterpret_cast<char*>(&screen_offset), 1);
			if constexpr (screen_scan::kEnableIndependentScanReport &&
				(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II)) {
				uint8_t refresh_rate = scan_report_state.GetRateForState();
				if (is.read(reinterpret_cast<char*>(&refresh_rate), 1))
					scan_report_state.LoadRate(refresh_rate);
			}
			else {
				is.read(reinterpret_cast<char*>(&screen_refresh_rate), 1);
			}
			is.read(reinterpret_cast<char*>(&screen_power), 1);
			lcd_response_reset_requested.store(true, std::memory_order_release);
		}
		void tick() {
			float ratio = 0;
			if constexpr (hardware_id == HW_FX_5800P || hardware_id == HW_ES_PLUS)
				ratio = 1 - 1e-4;
			else
				ratio = 1 - 5e-4;
#ifdef __EMSCRIPTEN__
			ratio = 0.0f;
#elif defined(__ANDROID__)
			ratio = 0.80f;
#else
			if (ThemeManager::Instance().Settings().lowPerformanceMode || low_perf_ext) {
				ratio = 0.80f;
			}
#endif
			const auto lcd_response = BeginLcdResponseTick();
			if constexpr (hardware_id == HW_TI) {
				ratio = 1 - 1e-4;
#ifdef __EMSCRIPTEN__
				ratio = 0.0f;
#elif defined(__ANDROID__)
				ratio = 0.80f;
#else
				if (ThemeManager::Instance().Settings().lowPerformanceMode || low_perf_ext) {
					ratio = 0.80f;
				}
#endif
				if (!ti_enabled) {
					for (size_t i = 0; i < 65 * 192; i++) {
						screen_ink_alpha[i] *= ratio;
					}
					return;
				}
				if (!n_ram_buffer) //  || !emulator.chipset.ti_status_buf) //  || !emulator.chipset.ti_screen_buf
					return;
				float ink_alpha_on = (ti_contrast - 100) * 20.0;
				float ink_alpha_off = std::clamp(ink_alpha_on * 0.1, 0.0, 255.0);
				ink_alpha_off = screen_residual_enabled ? ink_alpha_off * screen_residual_alpha_scale : 0.0f;
				ink_alpha_on = std::clamp(ink_alpha_on, 0.0f, 255.0f);
				if (!screen_residual_enabled) {
					ink_alpha_on = 255.0f;
				}
				uint8_t* screen_buffer = (uint8_t*)n_ram_buffer - casioemu::GetRamBaseAddr(hardware_id) + 0xE708;
				if (emulator.ModelDefinition.real_hardware) {
					screen_buffer = this->screen_buffer;
				}
				for (int ix = 0; ix < 192; ++ix) {
					for (int iy = 0; iy < 64; ++iy) {
						uint32_t i = (ix << 6) | iy;
						int bIndx = (i >> 3);
						int subIndx = (i & 7);
						int mask = (1 << subIndx);
						bool on = (screen_buffer[bIndx] & mask) != 0;
						auto& data = screen_ink_alpha[(iy * 192 + 192) + ix];
						data = data * ratio + (on ? ink_alpha_on : ink_alpha_off) * (1 - ratio);
					}
				}
				screen_buffer = (uint8_t*)n_ram_buffer - casioemu::GetRamBaseAddr(hardware_id) + 0xe5d4;
				if (emulator.ModelDefinition.real_hardware) {
					screen_buffer = this->screen_buffer + 8 * 192;
				}
				int x = 0;
				for (int ix = 1; ix != SPR_MAX; ++ix) {
					auto off = sprite_bitmap[ix].offset;
					auto& data = screen_ink_alpha[x];
					data = data * ratio + ((screen_buffer[off] & sprite_bitmap[ix].mask) ? ink_alpha_on : ink_alpha_off) * (1 - ratio);
					x++;
				}

				return;
			}
			else if constexpr (IsEpsFamily(hardware_id)) {
			#ifndef __EMSCRIPTEN__
				ratio = 0.80f;
			#endif
				bool eps_residual_enabled;
				float eps_residual_alpha_scale;
				EpsLcdResponseTick eps_response;
				{
					auto settings_lock = ordinary_lcd_history::UntrackedChange::LockSettings();
					eps_residual_enabled = screen_residual_enabled;
					eps_residual_alpha_scale = screen_residual_alpha_scale;
					eps_response = BeginEpsLcdResponseTick();
				}
				EpsScreenContext eps_context{
					hardware_id,
					emulator.chipset.epscpu,
					emulator.ModelDefinition.status_indicators,
					eps_screen_ink_alpha,
					eps_screen_alpha_mutex,
					eps_temporal_state,
					eps_residual_enabled,
					eps_residual_alpha_scale,
					ratio,
					eps_response};
				UpdateEpsScreen(eps_context);
				return;
			}

			int report_refresh_rate = 0;
			int visual_refresh_rate = 0;
			Uint64 scan_now = 0;
			const auto gate = screen_gate::Get();
			screen_scan::Gate visual_gate = gate;
			if constexpr (screen_scan::kEnableIndependentScanReport &&
				(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II)) {
				scan_now = SDL_GetTicks64();
				const auto scan_result = scan_report_state.Advance(scan_now, gate);
				// Advance returns the gate it actually accepted. This may be newer
				// than the snapshot captured before waiting for scan state, so all
				// ON-path visual decisions must use this returned snapshot.
				visual_gate = scan_result.gate;
				scan_now = scan_result.effective_now_ms;
				report_refresh_rate = scan_result.phase_rate;
				visual_refresh_rate = scan_result.effective_rate;
			}
			else {
				report_refresh_rate = screen_refresh_rate;
			}
			const int visual_flashing_threshold = visual_gate.flashing_threshold;
			const bool scan_frozen = [&]() {
				return report_refresh_rate < visual_gate.flashing_threshold && !visual_gate.fading_enabled;
			}();
			if (!scan_frozen) {
				int n = 0;
				if constexpr (screen_scan::kEnableIndependentScanReport &&
					(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II)) {
					n = update_screen_scan_alpha(
						screen_scan_alpha,
						screen_scan_curve,
						screen_scan_curve_coeff,
						screen_scan_curve_valid,
						scan_now,
						report_refresh_rate,
						visual_gate.flashing_threshold);
				}
				else {
					n = update_screen_scan_alpha(
						screen_scan_alpha,
						screen_scan_curve,
						screen_scan_curve_coeff,
						screen_scan_curve_valid,
						SDL_GetTicks64(),
						screen_refresh_rate,
						visual_gate.flashing_threshold);
				}
				if constexpr (!(screen_scan::kEnableIndependentScanReport &&
					(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II))) {
					screen_scan_report = screen_scan::CalculateScanReportFromPhase(n, screen_scan_report_op1, screen_scan_report_en);
				}
			}
			if constexpr (!(screen_scan::kEnableIndependentScanReport &&
				(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II))) {
				if (screen_refresh_rate < 6)
					screen_refresh_rate = 6;
				visual_refresh_rate = screen_refresh_rate;
			}
			const auto target_levels = ordinary_lcd::CalculateTargetLevels<hardware_id == HW_CLASSWIZ_II>(
				screen_brightness,
				static_cast<int>(screen_contrast),
				screen_residual_enabled,
				screen_residual_alpha_scale);

			auto screen_buffer = this->screen_buffer;
			uint8_t* screen_buffer1 = nullptr;
			size_t row_size = ROW_SIZE;
			if constexpr (hardware_id == HW_CLASSWIZ_II) {
				screen_buffer1 = this->screen_buffer1;
			}
			if (screen_buffer_select != 0) {
				screen_buffer = (uint8_t*)n_ram_buffer - casioemu::GetRamBaseAddr(hardware_id) + casioemu::GetScreenBufferOffset(emulator.hardware_id, screen_buffer_select);
				if (hardware_id == HW_CLASSWIZ_II) {
					screen_buffer1 = screen_buffer + 0x600;
				}
				row_size = ROW_SIZE_DISP;
			}

			const ordinary_lcd::FrameControls frame_controls{
				N_ROW, static_cast<int>(row_size), ROW_SIZE_DISP,
				screen_mode, screen_range, screen_offset, enabled_2,
				hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II,
				target_levels};
			ordinary_lcd::VisitFrame<hardware_id == HW_CLASSWIZ_II>(frame_controls,
				std::span<const ordinary_lcd::SpriteSpec>(sprite_bitmap + 1, SPR_MAX - 1),
				[&](size_t index, const ordinary_lcd::PixelSource& source) {
					const float target = ordinary_lcd::EvaluatePixel<hardware_id == HW_CLASSWIZ_II>(
						source, screen_buffer, screen_buffer1, screen_scan_alpha,
						visual_refresh_rate >= visual_flashing_threshold);
					ApplyLcdAlpha(index, target, ratio, lcd_response);
				},
				[&](size_t begin, size_t end) {
					ApplyLcdAlphaDecay(begin, end, ratio, lcd_response);
				},
				[&](ordinary_lcd::TargetLevels levels) {
					status_ink_alpha_on = levels.ink_alpha_on;
					status_ink_alpha_off = levels.ink_alpha_off;
				});
		}
	};

	template <>
	const int Screen<HW_TI>::N_ROW = 64;
	template <>
	const int Screen<HW_TI>::ROW_SIZE = 32;
	template <>
	const int Screen<HW_TI>::OFFSET = 32;
	template <>
	const int Screen<HW_TI>::ROW_SIZE_DISP = 24;
	template <>
	const int Screen<HW_TI>::SPR_MAX = 14;
	template <>
	const SpriteBitmap Screen<HW_TI>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		{"rsd_2nd", 1, 17},
		{"rsd_fix", 0, 0x00},
		{"rsd_hbo", 0, 0x00},
		{"rsd_sci", 0, 0x01},
		{"rsd_eng", 0, 0x01},
		{"rsd_deg", 0, 0x01},
		{"rsd_rad", 0, 0x01},
		{"rsd_bat", 0, 0x02},
		{"rsd_wait", 1, 164},
		{"rsd_left", 0, 0x02},
		{"rsd_up", 0, 0x02},
		{"rsd_down", 0, 0x02},
		{"rsd_right", 0, 0x02},
	};

	template <>
	const int Screen<HW_CLASSWIZ_II>::N_ROW = 63;
	template <>
	const int Screen<HW_CLASSWIZ_II>::ROW_SIZE = 32;
	template <>
	const int Screen<HW_CLASSWIZ_II>::OFFSET = 32;
	template <>
	const int Screen<HW_CLASSWIZ_II>::ROW_SIZE_DISP = 24;
	template <>
	const int Screen<HW_CLASSWIZ_II>::SPR_MAX = 21;

	template <>
	const int Screen<HW_CLASSWIZ>::N_ROW = 63;
	template <>
	const int Screen<HW_CLASSWIZ>::ROW_SIZE = 32;
	template <>
	const int Screen<HW_CLASSWIZ>::OFFSET = 32;
	template <>
	const int Screen<HW_CLASSWIZ>::ROW_SIZE_DISP = 24;
	template <>
	const int Screen<HW_CLASSWIZ>::SPR_MAX = 21;

	template <>
	const int Screen<HW_ES_PLUS>::N_ROW = 31;
	template <>
	const int Screen<HW_ES_PLUS>::ROW_SIZE = 16;
	template <>
	const int Screen<HW_ES_PLUS>::OFFSET = 16;
	template <>
	const int Screen<HW_ES_PLUS>::ROW_SIZE_DISP = 12;
	template <>
	const int Screen<HW_ES_PLUS>::SPR_MAX = 19;

	template <>
	const int Screen<HW_FX_5800P>::N_ROW = 31;
	template <>
	const int Screen<HW_FX_5800P>::ROW_SIZE = 16;
	template <>
	const int Screen<HW_FX_5800P>::OFFSET = 16;
	template <>
	const int Screen<HW_FX_5800P>::ROW_SIZE_DISP = 12;
	template <>
	const int Screen<HW_FX_5800P>::SPR_MAX = 20;

	// that's meaningless, just make compiler happy xd
	template <>
	const int Screen<HW_EPS6800>::N_ROW = 31;
	template <>
	const int Screen<HW_EPS6800>::ROW_SIZE = 16;
	template <>
	const int Screen<HW_EPS6800>::OFFSET = 16;
	template <>
	const int Screen<HW_EPS6800>::ROW_SIZE_DISP = 12;
	template <>
	const int Screen<HW_EPS6800>::SPR_MAX = 1;

	template <>
	const int Screen<HW_EPS6800_W192>::N_ROW = 63;
	template <>
	const int Screen<HW_EPS6800_W192>::ROW_SIZE = 24;
	template <>
	const int Screen<HW_EPS6800_W192>::OFFSET = 0;
	template <>
	const int Screen<HW_EPS6800_W192>::ROW_SIZE_DISP = 24;
	template <>
	const int Screen<HW_EPS6800_W192>::SPR_MAX = 1;

	template <>
	const int Screen<HW_EPS6009>::N_ROW = 0;
	template <>
	const int Screen<HW_EPS6009>::ROW_SIZE = 1;
	template <>
	const int Screen<HW_EPS6009>::OFFSET = 0;
	template <>
	const int Screen<HW_EPS6009>::ROW_SIZE_DISP = 1;
	template <>
	const int Screen<HW_EPS6009>::SPR_MAX = 1;

	template <>
	const int Screen<HW_EPS9500>::N_ROW = 32;
	template <>
	const int Screen<HW_EPS9500>::ROW_SIZE = 16;
	template <>
	const int Screen<HW_EPS9500>::OFFSET = 16;
	template <>
	const int Screen<HW_EPS9500>::ROW_SIZE_DISP = 12;
	template <>
	const int Screen<HW_EPS9500>::SPR_MAX = 1;

	template <>
	const SpriteBitmap Screen<HW_CLASSWIZ_II>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		{"rsd_s", 0x01, 0x01},
		{"rsd_math", 0x01, 0x03},
		{"rsd_d", 0x01, 0x04},
		{"rsd_r", 0x01, 0x05},
		{"rsd_g", 0x01, 0x06},
		{"rsd_fix", 0x01, 0x07},
		{"rsd_sci", 0x01, 0x08},
		{"rsd_fx", 0x01, 0x09},
		{"rsd_e", 0x01, 0x0A},
		{"rsd_cmplx", 0x01, 0x0B},
		{"rsd_angle", 0x01, 0x0C},
		{"rsd_wdown", 0x01, 0x0D},
		{"rsd_verify", 0x01, 0x0E},
		{"rsd_gx", 0x01, 0x0F},
		{"rsd_left", 0x01, 0x10},
		{"rsd_down", 0x01, 0x11},
		{"rsd_up", 0x01, 0x12},
		{"rsd_right", 0x01, 0x13},
		{"rsd_pause", 0x01, 0x15},
		{"rsd_sun", 0x01, 0x16} };

	template <>
	const SpriteBitmap Screen<HW_CLASSWIZ>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		{"rsd_s", 0x01, 0x00},
		{"rsd_a", 0x01, 0x01},
		{"rsd_m", 0x01, 0x02},
		{"rsd_sto", 0x01, 0x03},
		{"rsd_math", 0x01, 0x05},
		{"rsd_d", 0x01, 0x06},
		{"rsd_r", 0x01, 0x07},
		{"rsd_g", 0x01, 0x08},
		{"rsd_fix", 0x01, 0x09},
		{"rsd_sci", 0x01, 0x0A},
		{"rsd_e", 0x01, 0x0B},
		{"rsd_cmplx", 0x01, 0x0C},
		{"rsd_angle", 0x01, 0x0D},
		{"rsd_wdown", 0x01, 0x0F},
		{"rsd_left", 0x01, 0x10},
		{"rsd_down", 0x01, 0x11},
		{"rsd_up", 0x01, 0x12},
		{"rsd_right", 0x01, 0x13},
		{"rsd_pause", 0x01, 0x15},
		{"rsd_sun", 0x01, 0x16} };

	template <>
	const SpriteBitmap Screen<HW_ES_PLUS>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		{"rsd_s", 0x10, 0x00},
		{"rsd_a", 0x04, 0x00},
		{"rsd_m", 0x10, 0x01},
		{"rsd_sto", 0x02, 0x01},
		{"rsd_rcl", 0x40, 0x02},
		{"rsd_stat", 0x40, 0x03},
		{"rsd_cmplx", 0x80, 0x04},
		{"rsd_mat", 0x40, 0x05},
		{"rsd_vct", 0x02, 0x05},
		{"rsd_d", 0x20, 0x07},
		{"rsd_r", 0x02, 0x07},
		{"rsd_g", 0x10, 0x08},
		{"rsd_fix", 0x01, 0x08},
		{"rsd_sci", 0x20, 0x09},
		{"rsd_math", 0x40, 0x0A},
		{"rsd_down", 0x08, 0x0A},
		{"rsd_up", 0x80, 0x0B},
		{"rsd_disp", 0x10, 0x0B} };

	template <>
	const SpriteBitmap Screen<HW_FX_5800P>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		{"rsd_s", 0x10, 0x00},
		{"rsd_a", 0x04, 0x00},
		{"rsd_m", 0x10, 0x01},
		{"rsd_sto", 0x02, 0x01},
		{"rsd_rcl", 0x40, 0x02},
		{"rsd_sd", 0x40, 0x03},
		{"rsd_reg", 0x80, 0x04},
		{"rsd_fmla", 0x40, 0x05},
		{"rsd_prgm", 0x10, 0x05},
		{"rsd_eng", 0x02, 0x05},
		{"rsd_d", 0x20, 0x07},
		{"rsd_r", 0x02, 0x07},
		{"rsd_g", 0x10, 0x08},
		{"rsd_fix", 0x01, 0x08},
		{"rsd_sci", 0x20, 0x09},
		{"rsd_math", 0x40, 0x0A},
		{"rsd_down", 0x08, 0x0A},
		{"rsd_up", 0x80, 0x0B},
		{"rsd_disp", 0x10, 0x0B} };

	template <>
	const SpriteBitmap Screen<HW_EPS6800>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0} };

	template <>
	const SpriteBitmap Screen<HW_EPS6800_W192>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0} };

	template <>
	const SpriteBitmap Screen<HW_EPS6009>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0} };

	template <>
	const SpriteBitmap Screen<HW_EPS9500>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0} };

	template <HardwareId hardware_id>
	void Screen<hardware_id>::StartUpdateThread() {
#if !defined(TEST_BUILD) && !defined(__EMSCRIPTEN__)
		if (screen_thread.joinable()) {
			if (screen_thread_running.load(std::memory_order_acquire))
				return;
			if (screen_thread.get_id() == std::this_thread::get_id())
				std::terminate();
			screen_thread.join();
		}
		screen_thread_running.store(true, std::memory_order_release);
		screen_thread = std::thread([this]() {
			while (screen_thread_running.load(std::memory_order_acquire)) {
				int temporal_status = -1;
				{
					std::unique_lock<std::mutex> state_lock(screen_state_mutex);
					screen_state_ready.wait(state_lock, [this]() {
						return screen_state_waiters.load() == 0 || !screen_thread_running.load();
					});
					if (!screen_thread_running.load())
						break;
					temporal_status = TemporalTick();
					if (temporal_status < 0) {
						// Settings writers use UntrackedChange. Keep the legacy
						// renderer on the same synchronization contract.
						auto settings_lock = ordinary_lcd_history::UntrackedChange::LockSettings();
						tick();
					}
				}
				if constexpr (kCaptureLcdHistory) {
					if (temporal_status >= 0) {
						if (temporal_status == 1) lcd_history.WaitForWork(kTemporalInterval, screen_thread_running);
						continue;
					}
				}
			if constexpr (IsEpsFamily(hardware_id)) {
				SDL_Delay(10);
				}
#ifdef __ANDROID__
				else {
					SDL_Delay(10);
				}
#elif !defined(__EMSCRIPTEN__)
				else {
					bool low_performance;
					{
						auto settings_lock = ordinary_lcd_history::UntrackedChange::LockSettings();
						low_performance = ThemeManager::Instance().Settings().lowPerformanceMode || low_perf_ext;
					}
					if (low_performance)
						SDL_Delay(10);
				}
#endif
			}
		});
#endif
	}

	template <HardwareId hardware_id>
	void Screen<hardware_id>::StopUpdateThread() {
#if !defined(TEST_BUILD) && !defined(__EMSCRIPTEN__)
		{
			auto state_lock = LockScreenState();
			screen_thread_running.store(false, std::memory_order_release);
		}
		screen_state_ready.notify_one();
		if constexpr (kCaptureLcdHistory) lcd_history.Wake();
		if (screen_thread.joinable()) {
			// Screen destruction is owned by the Emulator/Chipset thread. The
			// update worker never destroys its owning Screen instance.
			if (screen_thread.get_id() == std::this_thread::get_id())
				std::terminate();
			screen_thread.join();
		}
		if constexpr (IsEpsFamily(hardware_id)) {
			if (emulator.chipset.epscpu)
				emulator.chipset.epscpu->SetLcdHistoryEnabled(false);
		}
#endif
	}

	template <HardwareId hardware_id>
	void Screen<hardware_id>::Initialise() {
		auto state_lock = LockScreenState();
		auto history_lock = LockLcdMutation();
		if (!inited) {
			if constexpr (kCaptureLcdHistory) lcd_history.InvalidateEpoch();
			renderer = emulator.GetRenderer();
			interface_texture = emulator.GetInterfaceTexture();
			const int sprite_count = SpriteCount();
			sprite_info.resize(sprite_count);
			for (auto& texture : sprite_svg_textures)
				texture.Reset();
			sprite_svg_textures.clear();
			sprite_svg_textures.resize(sprite_count);
			sprite_available.assign(sprite_count, 0);
			for (int ix = 0; ix != sprite_count; ++ix) {
				const char* static_name = nullptr;
				std::string dynamic_name;
				if constexpr (IsEpsFamily(hardware_id)) {
					dynamic_name = ix == 0 ? "rsd_pixel" : emulator.ModelDefinition.status_indicators[static_cast<size_t>(ix - 1)].sprite_name;
				}
				else {
					static_name = sprite_bitmap[ix].name;
				}
				auto sprite = emulator.ModelDefinition.sprites.find(static_name ? static_name : dynamic_name);
				if (sprite == emulator.ModelDefinition.sprites.end())
					continue;
				sprite_info[ix] = sprite->second;
				sprite_available[ix] = 1;
			}

			ink_colour = emulator.ModelDefinition.ink_color;
			if constexpr (hardware_id == HW_TI) {
				screen_buffer = new uint8_t[192 * 9];
				// TODO: remove this
				memset(screen_buffer, 0, 192 * 9);
				// fillRandomData(screen_buffer, 192*9);
			}
			else {
				screen_buffer = new uint8_t[RowBufferSize()];
				fillRandomData(screen_buffer, RowBufferSize());
			}
			if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
				region_power.Setup(
					0xF03D, 1, "Screen/Power", this,
					[](MMURegion* region, size_t offset) {
						return ((Screen*)region->userdata)->screen_power;
					},
					[](MMURegion* region, size_t offset, uint8_t data) {
						auto* screen = static_cast<Screen*>(region->userdata);
						bool a;
						{
							auto history_lock = screen->LockLcdMutation();
							a = (screen->screen_power & 1) ^ (data & 1);
							if constexpr (kCaptureLcdHistory) {
								if (screen->screen_power != (data & 0xf))
									screen->lcd_history.InvalidateEpoch();
							}
							screen->screen_power = data & 0xf;
						} // Release History before Initialise/Uninitialise takes Screen.
						if (a && ((data & 1) == 0)) { // 关闭屏幕
							((Screen*)region->userdata)->Uninitialise();
						}
						else {
							((Screen*)region->userdata)->Initialise();
						}
					},
					emulator);
			}
			if constexpr (hardware_id == HW_CLASSWIZ_II) {
				screen_buffer1 = new uint8_t[RowBufferSize()];
				fillRandomData(screen_buffer1, RowBufferSize());
			}
			inited = true;
		}
		if constexpr (IsEpsFamily(hardware_id)) {
			// CPU-visible LCD registers and RAM are owned by EPS6800Core. This
			// peripheral is only the CasioEmuMsvc presentation/resource layer.
			StartUpdateThread();
			return;
		}
		if constexpr (hardware_id == HW_TI) {
			auto pp = emulator.chipset.QueryInterface<IPortProvider>();
			pp->SetPortOutputCallback(7, [&](uint8_t data) {
				ti_port7 = data;
				});
			pp->SetPortOutputCallback(5, [&](uint8_t data) {
				// ti_port5 = data;
				if (ti_a0 && !(data & 0x40)) {
					if ((data & 0x10)) {
						auto bit_off = ti_col;
						auto off = bit_off + ti_page * 192;
						if (off > 192 * 9) {
							return;
						}
						if (off > 192 * 8) {
							std::cout << std::dec << off - 192 * 8 << " <- 0x" << std::hex << ti_port7 << "\n";
						}
						screen_buffer[off] = ti_port7;
						ti_col++;
						if (ti_col >= 192) {
							ti_col = 0;
							ti_page++;
						}
					}
					else {
						auto data = ti_port7;
						switch (ti_port_status) {
						case 0: {
							auto dh = data >> 4;
							if (dh == 0) {
								ti_col = (ti_col & 0xf0) | (data & 0xf);
							}
							else if (dh == 1) {
								ti_col = (ti_col & 0xf) | ((data & 0xf) << 4);
							}
							else if ((dh & 0b1100) == 0b0100) {
								// std::cout << "Set Scroll line " << (data & 0x3f) << "\n";
							}
							else if (dh == 0b1011) {
								// std::cout << "Set page  " << (data & 0xf) << "\n";
								ti_page = (data & 0xf);
							}
							else if ((data >> 3) == 17) {
								// std::cout << "Set addressing mode\n";
							}
							else if ((data >> 2) == 58) {
								// std::cout << "Set bias\n";
							}
							else if ((data >> 2) == 40) {
								// std::cout << "Set frame rate\n";
							}
							else if ((data >> 1) == 82) {
								// std::cout << "Clear all display segments\n";
							}
							else if ((data >> 1) == 83) {
								// std::cout << "Set inverse display\n";
							}
							else if ((data & 0xf9) == 0xc0) {
								// std::cout << "Set Com Seg Scan Direction\n";
							}
							else if (data == 0xe3) {
								// std::cout << "Nop\n";
							}
							else if (data == 0xe2) {
								// std::cout << "Software reset\n";
							}
							else if (data == 0xaf) {
								// std::cout << "Enabled screen!\n";
								ti_enabled = 1;
							}
							else if (data == 0x81) {
								ti_port_status = 1;
							}
							else if (data == 0xae) {
								// std::cout << "Disabled screen!\n";
								ti_enabled = 0;
							}
							else {
								std::cout << "[Screen][Warn] Unknown ST7525 command: 0x" << std::hex << (int)data << "\n";
							}
							break;
						}
						case 1:
							// std::cout << "Set contrast!\n";
							ti_contrast = data;
							ti_port_status = 0;
							break;
						}
					}
				}
				ti_a0 = (data & 0x40);
				});
			StartUpdateThread();
			return;
		}
		if (!(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) || (!enabled_2 && (screen_power & 1))) {
			if constexpr (kCaptureLcdHistory) {
				// Repeated power writes while already enabled leave history intact.
				lcd_history.InvalidateEpoch();
				if (!scan_history_bound) {
					scan_report_state.SetHistory(&lcd_history);
					scan_history_bound = true;
				}
			}
			if constexpr (hardware_id != HW_CLASSWIZ_II) {
				region_buffer.Setup(
					0xF800, RowBufferSize(), "Screen/Buffer", this, [](MMURegion* region, size_t offset) {
						offset -= region->base;
						if (offset % ROW_SIZE >= ROW_SIZE_DISP)
							return (uint8_t)0;
						return ((Screen*)region->userdata)->screen_buffer[offset]; },
					[](MMURegion* region, size_t offset, uint8_t data) {
						offset -= region->base;
						if (offset % ROW_SIZE >= ROW_SIZE_DISP)
							return;

						auto this_obj = (Screen*)region->userdata;
						auto history_lock = this_obj->LockLcdMutation();
						if constexpr (kCaptureLcdHistory) {
							struct WriteContext {
								Screen* screen;
								size_t offset;
								uint8_t data;
							};
							const auto write = [](void* raw) noexcept {
								auto* context = static_cast<WriteContext*>(raw);
								context->screen->screen_buffer[context->offset] = context->data;
							};
							const auto prepare = [](void* raw, ordinary_lcd_history::Event& event) noexcept {
								auto* context = static_cast<WriteContext*>(raw);
								event.buffer = ordinary_lcd_history::BufferId::F800;
								event.offset = static_cast<uint32_t>(context->offset);
								event.old_primary = context->screen->screen_buffer[context->offset];
								event.new_primary = context->data;
								event.write_plane_mask = ordinary_lcd_history::kPrimaryPlane;
								return event.Changes()
									? ordinary_lcd_history::PrepareResult::Commit
									: ordinary_lcd_history::PrepareResult::NoChange;
							};
							WriteContext context{this_obj, offset, data};
							if (this_obj->lcd_history.TryRecord(prepare, write, &context) ==
								ordinary_lcd_history::TransactionResult::Committed)
								return;
						}
						this_obj->screen_buffer[offset] = data; },
					emulator);
			}
			else {
				region_buffer.Setup(
					0xF800, RowBufferSize(), "Screen/Buffer", this,
					[](MMURegion* region, size_t offset) {
						offset -= region->base;
						if (offset % ROW_SIZE >= ROW_SIZE_DISP)
							return (uint8_t)0;
						if (((Screen*)region->userdata)->screen_select & 0x04) {
							return ((Screen*)region->userdata)->screen_buffer1[offset];
						}
						else {
							return ((Screen*)region->userdata)->screen_buffer[offset];
						}
					},
					[](MMURegion* region, size_t offset, uint8_t data) {
						offset -= region->base;
						if (offset % ROW_SIZE >= ROW_SIZE_DISP)
							return;

						auto this_obj = (Screen*)region->userdata;
						auto history_lock = this_obj->LockLcdMutation();
						if constexpr (kCaptureLcdHistory) {
							struct WriteContext {
								Screen* screen;
								size_t offset;
								uint8_t data;
								uint8_t plane_mask;
							};
							const auto write = [](void* raw) noexcept {
								auto* context = static_cast<WriteContext*>(raw);
								if (context->plane_mask == (ordinary_lcd_history::kPrimaryPlane | ordinary_lcd_history::kSecondaryPlane)) {
									context->screen->screen_buffer1[context->offset] = context->screen->screen_buffer[context->offset] = context->data;
								}
								else if (context->plane_mask & ordinary_lcd_history::kSecondaryPlane) {
									context->screen->screen_buffer1[context->offset] = context->data;
								}
								else {
									context->screen->screen_buffer[context->offset] = context->data;
								}
							};
							const auto prepare = [](void* raw, ordinary_lcd_history::Event& event) noexcept {
								auto* context = static_cast<WriteContext*>(raw);
								context->plane_mask = !(context->screen->screen_mode & 0x40)
									? (ordinary_lcd_history::kPrimaryPlane | ordinary_lcd_history::kSecondaryPlane)
									: ((context->screen->screen_select & 0x04)
										? ordinary_lcd_history::kSecondaryPlane
										: ordinary_lcd_history::kPrimaryPlane);
								event.buffer = ordinary_lcd_history::BufferId::F800;
								event.offset = static_cast<uint32_t>(context->offset);
								event.write_plane_mask = context->plane_mask;
								if (context->plane_mask & ordinary_lcd_history::kPrimaryPlane)
									event.old_primary = context->screen->screen_buffer[context->offset];
								if (context->plane_mask & ordinary_lcd_history::kSecondaryPlane)
									event.old_secondary = context->screen->screen_buffer1[context->offset];
								event.new_primary = context->data;
								event.new_secondary = context->data;
								return event.Changes()
									? ordinary_lcd_history::PrepareResult::Commit
									: ordinary_lcd_history::PrepareResult::NoChange;
							};
							WriteContext context{this_obj, offset, data, 0};
							if (this_obj->lcd_history.TryRecord(prepare, write, &context) ==
								ordinary_lcd_history::TransactionResult::Committed)
								return;
						}
						if (!(this_obj->screen_mode & 0x40)) {
							this_obj->screen_buffer1[offset] = this_obj->screen_buffer[offset] = data;
							return;
						}
						if (this_obj->screen_select & 0x04)
							this_obj->screen_buffer1[offset] = data;
						else
							this_obj->screen_buffer[offset] = data;
					},
					emulator);
				if (!emulator.ModelDefinition.real_hardware) {
					// region_buffer.Setup(
					//	0xF800, (N_ROW + 1) * ROW_SIZE, "Screen/Buffer", this,
					//	[](MMURegion* region, size_t offset) {
					//		offset -= region->base;
					//		if (offset % ROW_SIZE >= ROW_SIZE_DISP)
					//			return (uint8_t)0;
					//		return ((Screen*)region->userdata)->screen_buffer[offset];
					//	},
					//	[](MMURegion* region, size_t offset, uint8_t data) {
					//		offset -= region->base;
					//		if (offset % ROW_SIZE >= ROW_SIZE_DISP)
					//			return;

					//                auto this_obj = (Screen*)region->userdata;
					//                this_obj->screen_buffer[offset] = data;
					//        },
					//        emulator);
					region_buffer1.Setup(
						0x89000, RowBufferSize(), "Screen/Buffer1", this,
						[](MMURegion* region, size_t offset) {
							offset -= region->base;
							if (offset % ROW_SIZE >= ROW_SIZE_DISP)
								return (uint8_t)0;
							return ((Screen*)region->userdata)->screen_buffer1[offset];
						},
						[](MMURegion* region, size_t offset, uint8_t data) {
							offset -= region->base;
							if (offset % ROW_SIZE >= ROW_SIZE_DISP)
								return;

							auto this_obj = (Screen*)region->userdata;
						auto history_lock = this_obj->LockLcdMutation();
							if constexpr (kCaptureLcdHistory) {
								struct WriteContext {
									Screen* screen;
									size_t offset;
									uint8_t data;
								};
								const auto write = [](void* raw) noexcept {
									auto* context = static_cast<WriteContext*>(raw);
									context->screen->screen_buffer1[context->offset] = context->data;
								};
								const auto prepare = [](void* raw, ordinary_lcd_history::Event& event) noexcept {
									auto* context = static_cast<WriteContext*>(raw);
									event.buffer = ordinary_lcd_history::BufferId::F800Secondary;
									event.offset = static_cast<uint32_t>(context->offset);
									event.old_secondary = context->screen->screen_buffer1[context->offset];
									event.new_secondary = context->data;
									event.write_plane_mask = ordinary_lcd_history::kSecondaryPlane;
									return event.Changes()
										? ordinary_lcd_history::PrepareResult::Commit
										: ordinary_lcd_history::PrepareResult::NoChange;
								};
								WriteContext context{this_obj, offset, data};
								if (this_obj->lcd_history.TryRecord(prepare, write, &context) ==
									ordinary_lcd_history::TransactionResult::Committed)
									return;
							}
							this_obj->screen_buffer1[offset] = data;
						},
						emulator);
				}
			}
			if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
				SetupLcdControl<ordinary_lcd_history::Kind::Range, 0x2F, &Screen::screen_range>(
					region_range, 0xF030, "Screen/Range");
			}
			else {
				SetupLcdControl<ordinary_lcd_history::Kind::Range, 0x07, &Screen::screen_range>(
					region_range, 0xF030, "Screen/Range");
			}

			if constexpr (hardware_id == HW_CLASSWIZ_II) {
				region_mode.Setup(
					0xF031, 1, "Screen/Mode", this,
					[](MMURegion* region, size_t offset) {
						auto screen = ((Screen*)region->userdata);
						auto history_lock = screen->LockLcdMutation();
						return screen->screen_mode;
					},
						[](MMURegion* region, size_t offset, uint8_t data) {
							auto screen = ((Screen*)region->userdata);
						auto history_lock = screen->LockLcdMutation();
							if constexpr (kCaptureLcdHistory) {
								struct WriteContext {
									Screen* screen;
									uint8_t data;
								};
								const auto write = [](void* raw) noexcept {
									auto* context = static_cast<WriteContext*>(raw);
									context->screen->screen_mode = context->data & 127;
								};
								const auto prepare = [](void* raw, ordinary_lcd_history::Event& event) noexcept {
									auto* context = static_cast<WriteContext*>(raw);
									const uint8_t old_value = context->screen->screen_mode & 127;
									const uint8_t new_value = context->data & 127;
									if ((old_value ^ new_value) & 0b1000)
										return ordinary_lcd_history::PrepareResult::Reject;
									event.kind = ordinary_lcd_history::Kind::Mode;
									event.old_value = old_value;
									event.new_value = new_value;
									return old_value == new_value
										? ordinary_lcd_history::PrepareResult::NoChange
										: ordinary_lcd_history::PrepareResult::Commit;
								};
								WriteContext context{screen, data};
								const auto result = screen->lcd_history.TryRecord(prepare, write, &context);
								if (result == ordinary_lcd_history::TransactionResult::Committed)
									return;
							}
							auto old = screen->screen_mode & 0b1000;
						auto new_ = data & 0b1000;
						if (old ^ new_) {
							if constexpr (kCaptureLcdHistory)
								screen->lcd_history.InvalidateEpoch();
							auto sb = screen->screen_buffer;
							for (int iy = 0; iy != (N_ROW + 1); ++iy) {
								for (int ix = 0; ix != ROW_SIZE_DISP; ++ix) {
									sb[ix + iy * ROW_SIZE] = bit_lookup_table[sb[(ix)+iy * ROW_SIZE]];
								}
							}
							for (int iy = 0; iy != (N_ROW + 1); ++iy) {
								for (int ix = 0; ix != (ROW_SIZE_DISP / 2); ++ix) {
									std::swap(sb[ix + iy * ROW_SIZE], sb[(ROW_SIZE_DISP - 1 - ix) + iy * ROW_SIZE]);
								}
							}
							if constexpr (hardware_id == HW_CLASSWIZ_II) {
								sb = screen->screen_buffer1;
								for (int iy = 0; iy != (N_ROW + 1); ++iy) {
									for (int ix = 0; ix != ROW_SIZE_DISP; ++ix) {
										sb[ix + iy * ROW_SIZE] = bit_lookup_table[sb[(ix)+iy * ROW_SIZE]];
									}
								}
								for (int iy = 0; iy != (N_ROW + 1); ++iy) {
									for (int ix = 0; ix != (ROW_SIZE_DISP / 2); ++ix) {
										std::swap(sb[ix + iy * ROW_SIZE], sb[(ROW_SIZE_DISP - 1 - ix) + iy * ROW_SIZE]);
									}
								}
							}
						}
						screen->screen_mode = data & 127;
					},
					emulator);
			}
			else if constexpr (hardware_id == HW_CLASSWIZ) {
				if constexpr (kCaptureLcdHistory) {
					region_mode.Setup(
						0xF031, 1, "Screen/Mode", this,
						[](MMURegion* region, size_t) {
							return static_cast<uint8_t>(static_cast<Screen*>(region->userdata)->screen_mode & 63);
						},
						[](MMURegion* region, size_t, uint8_t data) {
							auto screen = static_cast<Screen*>(region->userdata);
							auto history_lock = screen->LockLcdMutation();
							struct WriteContext {
								Screen* screen;
								uint8_t data;
							};
							const auto write = [](void* raw) noexcept {
								auto* context = static_cast<WriteContext*>(raw);
								context->screen->screen_mode = context->data;
							};
							const auto prepare = [](void* raw, ordinary_lcd_history::Event& event) noexcept {
								auto* context = static_cast<WriteContext*>(raw);
								const uint8_t old_value = context->screen->screen_mode & 63;
								const uint8_t new_value = context->data & 63;
								event.kind = ordinary_lcd_history::Kind::Mode;
								event.old_value = old_value;
								event.new_value = new_value;
								return old_value == new_value
									? ordinary_lcd_history::PrepareResult::NoChange
									: ordinary_lcd_history::PrepareResult::Commit;
							};
							WriteContext context{screen, static_cast<uint8_t>(data & 63)};
							if (screen->lcd_history.TryRecord(prepare, write, &context) ==
								ordinary_lcd_history::TransactionResult::Committed)
								return;
							screen->screen_mode = data & 63;
						},
						emulator);
				}
				else {
					region_mode.Setup(0xF031, 1, "Screen/Mode", &screen_mode, MMURegion::DefaultRead<uint8_t, 63>,
						MMURegion::DefaultWrite<uint8_t, 63>, emulator);
				}
			}
			else {
				SetupLcdControl<ordinary_lcd_history::Kind::Mode, 0x07, &Screen::screen_mode>(
					region_mode, 0xF031, "Screen/Mode");
			}
			if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
				SetupLcdControl<ordinary_lcd_history::Kind::Contrast, 0x3F, &Screen::screen_contrast>(
					region_contrast, 0xF032, "Screen/Contrast");
				region_unk1.Setup(
					0xF03E, 1, "Screen/Unk1", this,
					[](MMURegion* region, size_t offset) {
						return (uint8_t)0;
					},
					[](MMURegion* region, size_t offset, uint8_t data) {
						((Screen*)region->userdata)->emulator.chipset.mmu.WriteData(0xF817, data);
					},
					emulator);
				region_unk2.Setup(
					0xF03F, 1, "Screen/Unk2", this,
					[](MMURegion* region, size_t offset) {
						return (uint8_t)0;
					},
					[](MMURegion* region, size_t offset, uint8_t data) {
						((Screen*)region->userdata)->emulator.chipset.mmu.WriteData(0xF817, data);
					},
					emulator);
			}
			else {
				SetupLcdControl<ordinary_lcd_history::Kind::Contrast, 0x1f, &Screen::screen_contrast>(
					region_contrast, 0xF032, "Screen/Contrast");
			}

			if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
				if constexpr (kCaptureLcdHistory) {
					region_select.Setup(
						0xF037, 1, "Screen/Select", this,
						[](MMURegion* region, size_t) {
							return static_cast<uint8_t>(static_cast<Screen*>(region->userdata)->screen_select & (0x04 | 1));
						},
						[](MMURegion* region, size_t, uint8_t data) {
							auto screen = static_cast<Screen*>(region->userdata);
							auto history_lock = screen->LockLcdMutation();
							struct WriteContext {
								Screen* screen;
								uint8_t data;
							};
							const auto write = [](void* raw) noexcept {
								auto* context = static_cast<WriteContext*>(raw);
								context->screen->screen_select = context->data & (0x04 | 1);
							};
							const auto prepare = [](void* raw, ordinary_lcd_history::Event& event) noexcept {
								auto* context = static_cast<WriteContext*>(raw);
								const uint8_t old_value = context->screen->screen_select & (0x04 | 1);
								const uint8_t new_value = context->data & (0x04 | 1);
								event.kind = ordinary_lcd_history::Kind::Select;
								event.old_value = old_value;
								event.new_value = new_value;
								return old_value == new_value
									? ordinary_lcd_history::PrepareResult::NoChange
									: ordinary_lcd_history::PrepareResult::Commit;
							};
							WriteContext context{screen, data};
							if (screen->lcd_history.TryRecord(prepare, write, &context) ==
								ordinary_lcd_history::TransactionResult::Committed)
								return;
							screen->screen_select = data & (0x04 | 1);
						},
						emulator);
				}
				else {
					region_select.Setup(0xF037, 1, "Screen/Select", &screen_select, MMURegion::DefaultRead < uint8_t, 0x04 | 1 >,
						MMURegion::DefaultWrite < uint8_t, 0x04 | 1 >, emulator);
				}

				SetupLcdControl<ordinary_lcd_history::Kind::Brightness, 0x07, &Screen::screen_brightness>(
					region_brightness, 0xF033, "Screen/Brightness");

				/*
cwx中F03B的值应该是由屏幕扫描和F035/F036决定的
1.每行扫描的时间大概是( [0xF034] * 25 ) us
2.F03B的mask是3，屏幕每扫描( [0xF036] == 0 ? 64 : [0xF035] )行后F03B的基础值会在0和3之间切换，如果F036是0的话这个循环的半周期和屏幕扫描应该是对齐的，也就是F03B的基础值切换后对应屏幕的第0行扫描（注：F035.0始终为1）
3.扫描屏幕的第0行 (对应bit0?) 及第32行 (对应bit1?) 时，F03B对应的bit会反转

n为行扫描计数，[0xF03B] = ( ( n / ( [0xF036] == 0 ? 64 : [0xF035] ) ) % 2 ? 3 : 0 ) ^ ( n % 64 == 0 ? 1 : ( n % 64 == 32 ? 2 : 0)  )
				*/

				if constexpr (screen_scan::kEnableIndependentScanReport) {
					SetupIndependentScanRegions();
				}
				else {
					region_scan_report_op1.Setup(0xF035, 1, "Screen/ScanReportOption1", &screen_scan_report_op1, MMURegion::DefaultRead<uint8_t, 0x1E>,
						MMURegion::DefaultWrite<uint8_t, 0x1E>, emulator);

					region_scan_report_en.Setup(0xF036, 1, "Screen/ScanReportOptionEnable", &screen_scan_report_en, MMURegion::DefaultRead<uint8_t, 0b1001>,
						MMURegion::DefaultWrite<uint8_t, 0b1001>, emulator);

					region_scan_report.Setup(0xF03B, 1, "Screen/ScanReport", &screen_scan_report, MMURegion::DefaultRead<uint8_t, 0x3>,
						MMURegion::IgnoreWrite, emulator);
				}
			}
			else {
				screen_scan_report_op1 = 0x17;
				screen_scan_report_en = 1;
			}

			if constexpr (screen_scan::kEnableIndependentScanReport &&
				(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II)) {
				SetupLcdControl<ordinary_lcd_history::Kind::Offset, 0x3F, &Screen::screen_offset>(
					region_offset, 0xF039, "Screen/DSPOFST");
			}
			else if constexpr (hardware_id == HardwareId::HW_FX_5800P || hardware_id == HardwareId::HW_ES_PLUS) {
				region_refresh_rate.Setup(0xF034, 1, "Screen/Unknown_F034", &unk_f034, MMURegion::DefaultRead<uint8_t, 0b11>,
					MMURegion::DefaultWrite<uint8_t, 0b11>, emulator);
			}
			else {
				region_offset.Setup(0xF039, 1, "Screen/DSPOFST", &screen_offset, MMURegion::DefaultRead<uint8_t, 0x3F>,
					MMURegion::DefaultWrite<uint8_t, 0x3F>, emulator);

				// 25us
				region_refresh_rate.Setup(0xF034, 1, "Screen/RefreshRate", &screen_refresh_rate, MMURegion::DefaultRead<uint8_t, 0x7F>,
					MMURegion::DefaultWrite<uint8_t, 0x7F>, emulator);
			}
			enabled_2 = true;
			if constexpr (screen_scan::kEnableIndependentScanReport &&
				(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II))
				scan_report_state.Activate();
		}
		StartUpdateThread();
	}

	template <HardwareId hardware_id>
	void Screen<hardware_id>::Uninitialise() {
		auto state_lock = LockScreenState();
		auto history_lock = LockLcdMutation();
		if (!enabled_2)
			return;
		if constexpr (kCaptureLcdHistory)
			lcd_history.InvalidateEpoch();
		fillRandomData(screen_buffer, RowBufferSize());
		if constexpr (hardware_id == HW_CLASSWIZ_II) {
			fillRandomData(screen_buffer1, RowBufferSize());
		}
		if constexpr (hardware_id != HW_CLASSWIZ_II) {
			region_buffer.Kill();
		}
		else {
			if (!emulator.ModelDefinition.real_hardware) {
				region_buffer.Kill();
				region_buffer1.Kill();
			}
			else {
				region_buffer.Kill();
			}
		}
		screen_range = 0;
		region_range.Kill();
		screen_mode = 0;
		region_mode.Kill();
		screen_contrast = 0;
		region_contrast.Kill();
		if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
			screen_select = 0;
			region_select.Kill();
			if constexpr (screen_scan::kEnableIndependentScanReport) {
				scan_report_state.Deactivate();
				region_scan_report_op1.Kill();
				region_scan_report_en.Kill();
				region_scan_report.Kill();
			}
			else {
				screen_scan_report_op1 = 0;
				region_scan_report_op1.Kill();
				screen_scan_report_en = 0;
				region_scan_report_en.Kill();
				screen_scan_report = 0;
				region_scan_report.Kill();
			}
			region_unk1.Kill();
			region_unk2.Kill();
			screen_brightness = 0;
			region_brightness.Kill();
		}
		if constexpr (!screen_scan::kEnableIndependentScanReport ||
			!(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II))
			screen_refresh_rate = 0;
		region_refresh_rate.Kill();
		if constexpr (hardware_id != HardwareId::HW_FX_5800P && hardware_id != HardwareId::HW_ES_PLUS) {
			screen_offset = 0;
			region_offset.Kill();
		}
		enabled_2 = false;
	}

	// Function to collect all sprite and pixel rectangles
	template <HardwareId hardware_id>
	void Screen<hardware_id>::Frame() {
		std::array<float, 66 * 192> frame_screen_ink_alpha{};
		{
			auto state_lock = LockScreenState();
#ifdef __EMSCRIPTEN__
			tick();
#elif defined(TEST_BUILD)
			if constexpr (IsEpsFamily(hardware_id))
				tick();
#endif
			if constexpr (IsEpsFamily(hardware_id)) {
				std::lock_guard<std::mutex> lock(eps_screen_alpha_mutex);
				std::copy(eps_screen_ink_alpha.begin(), eps_screen_ink_alpha.end(), screen_ink_alpha);
			}
			std::copy(std::begin(screen_ink_alpha), std::end(screen_ink_alpha), frame_screen_ink_alpha.begin());
		}
		int screenWidth = 0, screenHeight = 0;

		// Get the renderer output size if not already available
		SDL_GetRendererOutputSize(renderer, &screenWidth, &screenHeight);

		if (!emulator.ModelDefinition.enable_new_screen) {
			SDL_SetTextureColorMod(interface_texture, ink_colour.r, ink_colour.g, ink_colour.b);
		}

		for (int ix = 1; ix != SpriteCount(); ++ix) {
			if (ix >= static_cast<int>(sprite_available.size()) || !sprite_available[ix])
				continue;
			const int alpha_index = ix - 1;
			const uint8_t alpha = Uint8(std::clamp((int)frame_screen_ink_alpha[alpha_index], 0, 255));
			if (alpha == 0)
				continue;
			RenderModelSprite(renderer, interface_texture,
				ix < static_cast<int>(sprite_svg_textures.size()) ? &sprite_svg_textures[ix] : nullptr,
				sprite_info[ix], ink_colour, alpha);
		}

		static constexpr auto SPR_PIXEL = 0;
		SDL_Rect dest = Screen<hardware_id>::sprite_info[SPR_PIXEL].dest;
		const bool board_screen_slot = !emulator.ModelDefinition.board_path.empty();
		const bool segment_lcd = IsEpsSegmentLcd(hardware_id);
		int logical_width = 0;
		int logical_height = 0;
		if constexpr (IsEpsFamily(hardware_id)) {
			const auto eps_spec = GetEpsScreenSpec(
				hardware_id,
				emulator.ModelDefinition.screen_width,
				emulator.ModelDefinition.screen_height);
			logical_width = eps_spec.logical_width;
			logical_height = eps_spec.logical_height;
		}
		else if (segment_lcd) {
			logical_width = std::max(1, emulator.ModelDefinition.screen_width);
			logical_height = std::max(1, emulator.ModelDefinition.screen_height);
		}
		else {
			logical_width = ROW_SIZE_DISP * 8;
			logical_height = N_ROW;
		}
		SDL_Rect lcd_dest = dest;
		if (!board_screen_slot) {
			lcd_dest.w = std::max(1, (logical_width - 1) * sprite_info[SPR_PIXEL].src.w + sprite_info[SPR_PIXEL].dest.w);
			lcd_dest.h = std::max(1, (logical_height - 1) * sprite_info[SPR_PIXEL].src.h + sprite_info[SPR_PIXEL].dest.h);
		}

#ifndef CASIOEMU_CORE_WEB
		if (!segment_lcd)
			RenderPixelScreenTexture(lcd_dest, logical_width, logical_height, frame_screen_ink_alpha.data());
#endif

#if !defined(__EMSCRIPTEN__) && !defined(CASIOEMU_CORE_WEB)
		ScreenOutputFrame output_frame{
			renderer, interface_texture, emulator.interface_surface, &sprite_info,
			&sprite_available, &ink_colour, frame_screen_ink_alpha.data(),
			logical_width, logical_height, lcd_dest, !segment_lcd};
		std::lock_guard<std::mutex> output_lock(output_resource_mutex);
		if (!output)
			output = new ScreenOutput(emulator);
		output->Render(output_frame);
#endif
	}

	template <HardwareId hardware_id>
	void Screen<hardware_id>::Reset() {
	}

	Peripheral* CreateScreen(Emulator& emulator) {
		switch (emulator.hardware_id) {
		case HW_FX_5800P:
			return new Screen<HW_FX_5800P>(emulator);
		case HW_ES_PLUS:
			return new Screen<HW_ES_PLUS>(emulator);

		case HW_CLASSWIZ:
			return new Screen<HW_CLASSWIZ>(emulator);

		case HW_CLASSWIZ_II:
			return new Screen<HW_CLASSWIZ_II>(emulator);

		case HW_SOLARII:
			return CreateSolarIIScreen(emulator);

		case HW_TI:
			return new Screen<HW_TI>(emulator);
		case HW_EPS6800:
			return new Screen<HW_EPS6800>(emulator);
		case HW_EPS6800_W192:
			return new Screen<HW_EPS6800_W192>(emulator);
		case HW_EPS6009:
			return new Screen<HW_EPS6009>(emulator);
		case HW_EPS9500:
			return new Screen<HW_EPS9500>(emulator);
		default:
			PANIC("Unknown hardware id\n");
		}
		std::abort();
	}
} // namespace casioemu
