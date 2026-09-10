#pragma once

#include "LcdResponse.hpp"
#include "OrdinaryLcdFrame.hpp"
#include "OrdinaryLcdHistory.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace casioemu::lcd_temporal {

inline constexpr size_t kPixelCount = 66 * 192;
inline constexpr size_t kSourceByteCount = 64 * 32;
inline constexpr size_t kSourceBitCount = kSourceByteCount * 8;
// Every active pixel can have a distinct timestamp after interleaved partial
// updates; this bound avoids an unproved collision/recompute assumption.
inline constexpr size_t kGainCacheCapacity = 32768;
static_assert((kGainCacheCapacity & (kGainCacheCapacity - 1)) == 0);
static_assert(kGainCacheCapacity > kPixelCount);

struct ScanState {
	uint8_t raw_rate = 0;
	uint8_t effective_rate = 0;
	uint8_t option1 = 0;
	uint8_t option_enable = 0;
	int flashing_threshold = 20;
	bool fading_enabled = false;
	uint64_t gate_version = 0;
	bool active = false;
	bool gate_initialized = false;
	uint64_t sdl_ms = 0;
};

using SpriteSpec = ordinary_lcd::SpriteSpec;

struct Controls {
	HardwareId hardware_id = HW_CLASSWIZ;
	int n_row = 63;
	int row_size = 32;
	int row_size_display = 24;
	uint8_t mode = 0;
	uint8_t select = 0;
	uint8_t range = 0;
	uint8_t offset = 0;
	uint8_t brightness = 0;
	uint8_t contrast = 0;
	uint8_t power = 0;
	bool enabled = false;
	bool buffer_select = false;
	bool residual_enabled = false;
	float residual_alpha_scale = 1.0f;
	float flashing_brightness_coeff = 1.5f;
};

struct Baseline {
	Controls controls{};
	ScanState scan{};
	std::vector<uint8_t> primary;
	std::vector<uint8_t> secondary;
	std::vector<SpriteSpec> sprites;
	std::array<double, kPixelCount> alpha{};
	ordinary_lcd::TargetLevels status_levels{255, 0};
	std::array<float, 64> scan_alpha{};
	// A complete baseline must describe every alpha at exactly steady_ns.
	// Replay rejects mixed or older per-pixel timestamps rather than guessing
	// the missing target history.
	std::array<uint64_t, kPixelCount> alpha_steady_ns{};
	uint64_t epoch = 0;
	uint64_t seq = 0;
	uint64_t steady_ns = 0;
	uint64_t sdl_ms = 0;
	bool coverage_complete = false;
};

struct Candidate {
	Controls controls{};
	ScanState scan{};
	std::vector<uint8_t> primary;
	std::vector<uint8_t> secondary;
	std::array<double, kPixelCount> alpha{};
	// Preserved across clean/mode-4 frames, updated only by the status visitor.
	ordinary_lcd::TargetLevels status_levels{255, 0};
	std::array<float, 64> scan_alpha{};
	std::array<float, 64> scan_curve{};
	float scan_curve_coefficient = 0;
	bool scan_curve_valid = false;
	uint64_t anchor_ns = 0, anchor_sdl_ms = 0, timeline_ns = 0;
	size_t scan_segments = 0;
	std::array<uint64_t, kPixelCount> alpha_steady_ns{};
	std::array<float, kPixelCount> targets{};
	std::array<uint8_t, kPixelCount> active{};
	std::array<ordinary_lcd::PixelSource, kPixelCount> pixel_sources{};
	// Two slots are kept per source bit because a status icon and a dot can
	// legally be driven by the same byte bit in the ordinary LCD layout.
	std::array<uint16_t, kSourceBitCount * 2> primary_map{};
	std::array<uint16_t, kSourceBitCount * 2> secondary_map{};
	std::vector<SpriteSpec> sprites;
	uint64_t epoch = 0;
	uint64_t seq = 0;
	uint64_t steady_ns = 0;
	uint64_t last_event_ns = 0;
	uint64_t sdl_ms = 0;
	std::array<uint64_t, kGainCacheCapacity> gain_cache_start_ns{};
	std::array<uint32_t, kGainCacheCapacity> gain_cache_generation{};
	std::array<double, kGainCacheCapacity> gain_cache_rise{};
	std::array<double, kGainCacheCapacity> gain_cache_fall{};
	uint32_t gain_cache_current_generation = 0;
	size_t gain_cache_count = 0;
};

// Owns reusable heap storage for a single fixed cutoff. Append never exposes a
// partially replayed frame. Finish requires the entire contiguous sequence;
// Result stays unavailable after any failure. History completeness
// and live publication/version checks remain the caller's responsibility.
class ReplaySession {
public:
	ReplaySession() = default;
	ReplaySession(const ReplaySession&) = delete;
	ReplaySession& operator=(const ReplaySession&) = delete;
	ReplaySession(ReplaySession&&) = delete;
	ReplaySession& operator=(ReplaySession&&) = delete;
	// Handoff from the legacy endpoint sampler: settle its last interval using
	// the captured endpoint target, privately, before starting event replay.
	bool BeginFromLive(const Baseline& baseline, const ordinary_lcd_history::Cutoff& cutoff,
		uint64_t previous_ns);
	// Retain a completed private frame and its mapping for the next cutoff.
	bool Continue(const ordinary_lcd_history::Cutoff& cutoff);
	bool AppendBatch(const ordinary_lcd_history::Batch& batch,
		const ordinary_lcd_history::ConsumeResult& consumed);
	bool Finish();
	// Borrowed until the next mutating session call; no live arrays are aliased.
	const Candidate* Result() const;

private:
	enum class ReplayStatus : uint8_t { Idle, Pending, Ready, Rejected };
	bool Begin(const Baseline& baseline, const ordinary_lcd_history::Cutoff& cutoff);
	bool Append(std::span<const ordinary_lcd_history::Event> events);
	bool Reject();
	std::unique_ptr<Candidate> candidate_;
	ordinary_lcd_history::Cutoff cutoff_{};
	ReplayStatus status_ = ReplayStatus::Idle;
};

} // namespace casioemu::lcd_temporal
