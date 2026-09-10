#include "LcdTemporalCandidate.hpp"

#include "OrdinaryLcdTarget.hpp"
#include "ScreenScanVisual.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <span>

namespace casioemu::lcd_temporal {

namespace {

constexpr bool IsSupported(HardwareId id) {
	return id == HW_CLASSWIZ || id == HW_CLASSWIZ_II || id == HW_ES_PLUS || id == HW_FX_5800P;
}

bool IsFrozen(const ScanState& scan) {
	return scan.raw_rate < scan.flashing_threshold &&
		!scan.fading_enabled;
}

ScanState ToScanState(const ordinary_lcd_history::ScanSnapshot& snapshot, uint64_t sdl_ms) {
	return {
		snapshot.raw_rate, snapshot.effective_rate, snapshot.option1, snapshot.option_enable,
		snapshot.flashing_threshold, snapshot.fading_enabled, snapshot.gate_version,
		snapshot.active, snapshot.gate_initialized, sdl_ms};
}

bool SameScan(const ScanState& state, const ordinary_lcd_history::ScanSnapshot& snapshot) {
	return state.raw_rate == snapshot.raw_rate && state.effective_rate == snapshot.effective_rate &&
		state.option1 == snapshot.option1 && state.option_enable == snapshot.option_enable &&
		state.flashing_threshold == snapshot.flashing_threshold &&
		state.fading_enabled == snapshot.fading_enabled && state.gate_version == snapshot.gate_version &&
		state.active == snapshot.active && state.gate_initialized == snapshot.gate_initialized;
}

constexpr uint16_t kInvalidPixel = std::numeric_limits<uint16_t>::max();

bool AddSourceMapping(std::array<uint16_t, kSourceBitCount * 2>& map,
	size_t source_bit, size_t pixel) {
	if (source_bit >= kSourceBitCount || pixel >= kPixelCount)
		return false;
	const size_t base = source_bit * 2;
	if (map[base] == pixel || map[base + 1] == pixel)
		return true;
	if (map[base] == kInvalidPixel)
		map[base] = static_cast<uint16_t>(pixel);
	else if (map[base + 1] == kInvalidPixel)
		map[base + 1] = static_cast<uint16_t>(pixel);
	else
		return false;
	return true;
}

template <typename Callback>
void ForMappedPixels(const std::array<uint16_t, kSourceBitCount * 2>& map,
	size_t source_byte, uint8_t changed_mask, Callback&& callback) {
	if (source_byte >= kSourceByteCount)
		return;
	for (int bit = 0; bit != 8; ++bit) {
		if ((changed_mask & (0x80u >> bit)) == 0)
			continue;
		const size_t base = (source_byte * 8 + static_cast<size_t>(bit)) * 2;
		for (size_t slot = 0; slot != 2; ++slot) {
			const uint16_t pixel = map[base + slot];
			if (pixel != kInvalidPixel)
				callback(static_cast<size_t>(pixel));
		}
	}
}

void BeginGainCache(Candidate& candidate) {
	if (++candidate.gain_cache_current_generation == 0) {
		candidate.gain_cache_generation.fill(0);
		candidate.gain_cache_current_generation = 1;
	}
	candidate.gain_cache_count = 0;
}

bool LookupGains(Candidate& candidate, lcd_response::Config config,
	uint64_t start_ns, uint64_t end_ns, double& rise, double& fall) {
	size_t cache_index = static_cast<size_t>(
		(start_ns ^ (start_ns >> 33) ^ (start_ns >> 17)) & (kGainCacheCapacity - 1));
	for (size_t probe = 0; probe != kGainCacheCapacity; ++probe) {
		if (candidate.gain_cache_generation[cache_index] != candidate.gain_cache_current_generation) {
			if (candidate.gain_cache_count >= kPixelCount)
				return false;
			const double elapsed_ms = static_cast<double>(end_ns - start_ns) / 1000000.0;
			candidate.gain_cache_start_ns[cache_index] = start_ns;
			candidate.gain_cache_generation[cache_index] = candidate.gain_cache_current_generation;
			candidate.gain_cache_rise[cache_index] =
				lcd_response::GainForElapsed(elapsed_ms, config.rise_half_life_ms);
			candidate.gain_cache_fall[cache_index] =
				lcd_response::GainForElapsed(elapsed_ms, config.fall_half_life_ms);
			++candidate.gain_cache_count;
			rise = candidate.gain_cache_rise[cache_index];
			fall = candidate.gain_cache_fall[cache_index];
			return true;
		}
		if (candidate.gain_cache_start_ns[cache_index] == start_ns) {
			rise = candidate.gain_cache_rise[cache_index];
			fall = candidate.gain_cache_fall[cache_index];
			return true;
		}
		cache_index = (cache_index + 1) & (kGainCacheCapacity - 1);
	}
	return false;
}

bool AdvancePixels(Candidate& candidate, std::span<const size_t> pixels, uint64_t end_ns) {
	const auto config = lcd_response::ForHardware(candidate.controls.hardware_id);
	for (const size_t i : pixels) {
		if (i >= kPixelCount || (candidate.active[i] && end_ns < candidate.alpha_steady_ns[i]))
			return false;
	}
	BeginGainCache(candidate);
	for (const size_t i : pixels) {
		if (!candidate.active[i])
			continue;
		double rise = 0.0, fall = 0.0;
		if (!LookupGains(candidate, config, candidate.alpha_steady_ns[i], end_ns, rise, fall))
			return false;
		candidate.alpha[i] = lcd_response::BlendWithGains(
			candidate.alpha[i], candidate.targets[i], rise, fall);
		candidate.alpha_steady_ns[i] = end_ns;
	}
	return true;
}

bool AdvanceAllPixels(Candidate& candidate, uint64_t end_ns) {
	const auto config = lcd_response::ForHardware(candidate.controls.hardware_id);
	for (size_t i = 0; i != kPixelCount; ++i)
		if (candidate.active[i] && end_ns < candidate.alpha_steady_ns[i])
			return false;
	BeginGainCache(candidate);
	for (size_t i = 0; i != kPixelCount; ++i) {
		if (!candidate.active[i])
			continue;
		double rise = 0.0, fall = 0.0;
		if (!LookupGains(candidate, config, candidate.alpha_steady_ns[i], end_ns, rise, fall))
			return false;
		candidate.alpha[i] = lcd_response::BlendWithGains(
			candidate.alpha[i], candidate.targets[i], rise, fall);
		candidate.alpha_steady_ns[i] = end_ns;
	}
	return true;
}

void RefreshScan(Candidate& candidate, uint64_t sdl_ms) {
	if (!IsFrozen(candidate.scan))
		screen_scan::UpdateScanAlpha(candidate.scan_alpha.data(), candidate.scan_curve,
			candidate.scan_curve_coefficient, candidate.scan_curve_valid, sdl_ms,
			candidate.scan.raw_rate, candidate.scan.flashing_threshold,
			candidate.controls.flashing_brightness_coeff);
}

void RefreshTargets(Candidate& c) {
	for (size_t i = 0; i != kPixelCount; ++i) {
		if (c.active[i] != 1) continue; // decay ranges retain zero targets
		c.targets[i] = c.controls.hardware_id == HW_CLASSWIZ_II
			? ordinary_lcd::EvaluatePixel<true>(c.pixel_sources[i], c.primary.data(), c.secondary.data(),
				c.scan_alpha.data(), c.scan.effective_rate >= c.scan.flashing_threshold)
			: ordinary_lcd::EvaluatePixel<false>(c.pixel_sources[i], c.primary.data(), nullptr,
				c.scan_alpha.data(), c.scan.effective_rate >= c.scan.flashing_threshold);
	}
}

// Anchor the integer SDL sample to its paired steady sample. Preemption and
// clock resolution introduce anchoring error; no sub-millisecond bound is
// assumed. Scan events re-anchor to State's adopted SDL timestamp.
bool AdvanceTimeline(Candidate& c, uint64_t end_ns) {
	if (end_ns < c.timeline_ns || c.timeline_ns < c.anchor_ns) return false;
	const auto rate = c.scan.raw_rate;
	if (c.controls.enabled && c.scan.active && rate > 0 &&
		rate >= c.scan.flashing_threshold && !IsFrozen(c.scan)) {
		while (true) {
			const uint64_t elapsed_ms = (c.timeline_ns - c.anchor_ns) / 1000000;
			if (elapsed_ms > UINT64_MAX - c.anchor_sdl_ms) return false;
			const uint64_t sdl = c.anchor_sdl_ms + elapsed_ms;
			const uint64_t delta = (250 - ((sdl % 250) * rate) % 250 + rate - 1) / rate;
			const uint64_t max_elapsed = (UINT64_MAX - c.anchor_ns) / 1000000;
			if (delta > max_elapsed || elapsed_ms > max_elapsed - delta) return false;
			const uint64_t boundary_ns = c.anchor_ns + (elapsed_ms + delta) * 1000000;
			if (boundary_ns > end_ns) break;
			if (++c.scan_segments > 64 || sdl > UINT64_MAX - delta) return false;
			if (!AdvanceAllPixels(c, boundary_ns)) return false;
			RefreshScan(c, sdl + delta);
			RefreshTargets(c);
			c.timeline_ns = boundary_ns;
		}
	}
	c.timeline_ns = end_ns;
	return true;
}

bool BuildTargetsImpl(const Controls& controls, const ScanState& scan,
	std::span<const uint8_t> primary, std::span<const uint8_t> secondary,
	std::span<const SpriteSpec> sprites, std::array<float, kPixelCount>& targets,
	std::array<uint8_t, kPixelCount>& active,
	std::array<uint16_t, kSourceBitCount * 2>* primary_map,
	std::array<uint16_t, kSourceBitCount * 2>* secondary_map,
	std::array<ordinary_lcd::PixelSource, kPixelCount>* pixel_sources,
	ordinary_lcd::TargetLevels* status_levels, const float* scan_alpha) {
	if (!IsSupported(controls.hardware_id)) {
		return false;
	}
	const bool classwiz_ii = controls.hardware_id == HW_CLASSWIZ_II;
	const bool classwiz = controls.hardware_id == HW_CLASSWIZ || classwiz_ii;
	const size_t sprite_count = classwiz ? 20 : (controls.hardware_id == HW_ES_PLUS ? 18 : 19);
	if (controls.n_row != (classwiz ? 63 : 31) || controls.row_size != (classwiz ? 32 : 16) ||
		controls.row_size_display != (classwiz ? 24 : 12) || sprites.size() != sprite_count) {
		return false;
	}
	const int expected_rows = controls.n_row + 1;
	const size_t expected_size = static_cast<size_t>(expected_rows) * controls.row_size;
	if (primary.size() != expected_size ||
		(classwiz_ii && secondary.size() != expected_size) ||
		(!classwiz_ii && !secondary.empty())) {
		return false;
	}
	for (const auto sprite : sprites) {
		if (sprite.mask == 0) {
			return false;
		}
	}
	if (controls.buffer_select) {
		return false;
	}
	if (controls.enabled && scan.effective_rate >= scan.flashing_threshold && !scan_alpha) {
		return false;
	}
	std::fill(targets.begin(), targets.end(), 0.0f);
	std::fill(active.begin(), active.end(), 0);
	if (primary_map)
		std::fill(primary_map->begin(), primary_map->end(), kInvalidPixel);
	if (secondary_map)
		std::fill(secondary_map->begin(), secondary_map->end(), kInvalidPixel);
	const auto levels = controls.hardware_id == HW_CLASSWIZ_II
		? ordinary_lcd::CalculateTargetLevels<true>(
			controls.brightness, controls.contrast, controls.residual_enabled,
			controls.residual_alpha_scale)
		: ordinary_lcd::CalculateTargetLevels<false>(
			controls.brightness, controls.contrast, controls.residual_enabled,
			controls.residual_alpha_scale);
	const ordinary_lcd::FrameControls frame{
		controls.n_row, controls.row_size, controls.row_size_display,
		controls.mode, controls.range, controls.offset, controls.enabled,
		classwiz, levels};
	bool failed = false;
	const auto add_mappings = [&](const ordinary_lcd::PixelSource& source, size_t pixel) {
		if (!source.read_source || source.clear)
			return;
		for (int bit = 0; bit != 8; ++bit) {
			if ((source.mask & (0x80u >> bit)) == 0)
				continue;
			const size_t source_bit = source.source_offset * 8 + static_cast<size_t>(bit);
			if (primary_map && !AddSourceMapping(*primary_map, source_bit, pixel))
				failed = true;
			if (classwiz_ii && secondary_map && !AddSourceMapping(*secondary_map, source_bit, pixel))
				failed = true;
		}
	};
	const auto pixel = [&](size_t index, const ordinary_lcd::PixelSource& source) {
		if (failed)
			return;
		if (index >= kPixelCount || source.source_offset >= primary.size() ||
			(classwiz_ii && source.source_offset >= secondary.size())) {
			failed = true;
			return;
		}
		if (classwiz_ii)
			targets[index] = ordinary_lcd::EvaluatePixel<true>(source, primary.data(), secondary.data(), scan_alpha, scan.effective_rate >= scan.flashing_threshold);
		else
			targets[index] = ordinary_lcd::EvaluatePixel<false>(source, primary.data(), nullptr, scan_alpha, scan.effective_rate >= scan.flashing_threshold);
		if (pixel_sources)
			(*pixel_sources)[index] = source;
		active[index] = 1;
		add_mappings(source, index);
	};
	const auto decay = [&](size_t begin, size_t end) {
		if (begin > end || end > kPixelCount) {
			failed = true;
			return;
		}
		std::fill(active.begin() + begin, active.begin() + end, 2);
	};
	const auto status = [&](ordinary_lcd::TargetLevels levels) {
		if (status_levels)
			*status_levels = levels;
	};
	if (classwiz_ii)
		ordinary_lcd::VisitFrame<true>(frame, sprites, pixel, decay, status);
	else
		ordinary_lcd::VisitFrame<false>(frame, sprites, pixel, decay, status);
	return !failed;
}

} // namespace

bool ReplaySession::Reject() {
	status_ = ReplayStatus::Rejected;
	return false;
}

bool ReplaySession::Begin(const Baseline& baseline,
	const ordinary_lcd_history::Cutoff& cutoff) {
	// Allocation/copy exceptions must not leave a usable pending session.
	status_ = ReplayStatus::Rejected;
	cutoff_ = cutoff;
	if (!baseline.coverage_complete) {
		return Reject();
	}
	if (!IsSupported(baseline.controls.hardware_id)) {
		return Reject();
	}
	if (cutoff.epoch != baseline.epoch || cutoff.end_seq < baseline.seq)
		return Reject();
	if (cutoff.steady_ns < baseline.steady_ns)
		return Reject();
	for (const uint64_t timestamp : baseline.alpha_steady_ns) {
		if (timestamp != baseline.steady_ns) {
			return Reject();
		}
	}
	if (!candidate_)
		candidate_ = std::make_unique<Candidate>();
	Candidate& candidate = *candidate_;
	candidate.controls = baseline.controls;
	candidate.scan = baseline.scan;
	candidate.primary = baseline.primary;
	candidate.secondary = baseline.secondary;
	candidate.sprites = baseline.sprites;
	candidate.alpha = baseline.alpha;
	candidate.status_levels = baseline.status_levels;
	candidate.scan_alpha = baseline.scan_alpha;
	candidate.anchor_ns = candidate.timeline_ns = baseline.steady_ns;
	candidate.anchor_sdl_ms = baseline.sdl_ms;
	candidate.scan_segments = 0;
	RefreshScan(candidate, baseline.sdl_ms);
	candidate.alpha_steady_ns.fill(baseline.steady_ns);
	candidate.epoch = baseline.epoch;
	candidate.seq = baseline.seq;
	candidate.steady_ns = baseline.steady_ns;
	candidate.last_event_ns = baseline.steady_ns;
	candidate.sdl_ms = baseline.sdl_ms;
	if (!BuildTargetsImpl(candidate.controls, candidate.scan,
		candidate.primary, candidate.secondary, candidate.sprites,
		candidate.targets, candidate.active, &candidate.primary_map,
		&candidate.secondary_map, &candidate.pixel_sources, &candidate.status_levels, candidate.scan_alpha.data()))
		return Reject();
	status_ = ReplayStatus::Pending;
	return true;
}

bool ReplaySession::BeginFromLive(const Baseline& baseline,
	const ordinary_lcd_history::Cutoff& cutoff, uint64_t previous_ns) {
	if (!Begin(baseline, cutoff)) return false;
	if (previous_ns > baseline.steady_ns) return Reject();
	candidate_->alpha_steady_ns.fill(previous_ns);
	if (!AdvanceAllPixels(*candidate_, baseline.steady_ns))
		return Reject();
	candidate_->alpha_steady_ns.fill(baseline.steady_ns);
	return true;
}

bool ReplaySession::Continue(const ordinary_lcd_history::Cutoff& cutoff) {
	if (status_ != ReplayStatus::Ready)
		return Reject();
	if (cutoff.epoch != candidate_->epoch || cutoff.end_seq < candidate_->seq)
		return Reject();
	if (cutoff.steady_ns < candidate_->steady_ns)
		return Reject();
	cutoff_ = cutoff;
	candidate_->scan_segments = 0;
	// A new event must follow the completed interval, even when the preceding
	// interval contained no events. Per-pixel origins were settled by Finish.
	candidate_->last_event_ns = candidate_->steady_ns;
	status_ = ReplayStatus::Pending;
	return true;
}

bool ReplaySession::AppendBatch(const ordinary_lcd_history::Batch& batch,
	const ordinary_lcd_history::ConsumeResult& consumed) {
	if (status_ != ReplayStatus::Pending)
		return Reject();
	if (consumed.incomplete)
		return Reject();
	if (!consumed.epoch_matches || !consumed.queue_epoch_matches || consumed.epoch != cutoff_.epoch)
		return Reject();
	if (batch.size > batch.events.size() || consumed.count != batch.size)
		return Reject();
	const uint64_t last_seq = batch.size ? batch.events[batch.size - 1].seq : 0;
	if (consumed.last_seq != last_seq)
		return Reject();
	if (!Append(std::span<const ordinary_lcd_history::Event>(batch.events.data(), batch.size)))
		return false;
	return !consumed.cutoff_complete || Finish();
}

bool ReplaySession::Append(std::span<const ordinary_lcd_history::Event> events) {
	if (status_ != ReplayStatus::Pending)
		return Reject();
	Candidate& candidate = *candidate_;
	const auto& cutoff = cutoff_;
	for (const auto& event : events) {
		if (event.epoch != candidate.epoch) {
			return Reject();
		}
		if (event.seq != candidate.seq + 1) {
			return Reject();
		}
		if (event.seq > cutoff.end_seq || event.steady_ns < candidate.last_event_ns ||
			event.steady_ns > cutoff.steady_ns) {
			return Reject();
		}
		if (!AdvanceTimeline(candidate, event.steady_ns))
			return Reject();
		switch (event.kind) {
		case ordinary_lcd_history::Kind::Byte: {
			if ((event.write_plane_mask & ~(ordinary_lcd_history::kPrimaryPlane |
				ordinary_lcd_history::kSecondaryPlane)) != 0 || event.write_plane_mask == 0) {
				return Reject();
			}
			if (event.offset >= candidate.primary.size()) {
				return Reject();
			}
			const uint8_t primary_changed = event.old_primary ^ event.new_primary;
			const uint8_t secondary_changed = event.old_secondary ^ event.new_secondary;
			std::array<size_t, 32> affected{};
			size_t affected_count = 0;
			bool affected_overflow = false;
			const auto collect = [&](const auto& map, uint8_t changed) {
				ForMappedPixels(map, event.offset, changed, [&](size_t pixel) {
					for (size_t i = 0; i != affected_count; ++i)
						if (affected[i] == pixel)
							return;
					if (affected_count != affected.size())
						affected[affected_count++] = pixel;
					else
						affected_overflow = true;
				});
			};
			if (event.buffer == ordinary_lcd_history::BufferId::F800Secondary) {
				if (event.write_plane_mask != ordinary_lcd_history::kSecondaryPlane ||
					candidate.secondary.empty() || event.offset >= candidate.secondary.size() ||
					event.old_secondary != candidate.secondary[event.offset]) {
					return Reject();
				}
				if (event.write_plane_mask & ordinary_lcd_history::kSecondaryPlane)
					collect(candidate.secondary_map, secondary_changed);
				if (affected_overflow) {
					return Reject();
				}
				if (!AdvancePixels(candidate,
					std::span<const size_t>(affected.data(), affected_count), event.steady_ns)) {
					return Reject();
				}
				if (event.write_plane_mask & ordinary_lcd_history::kSecondaryPlane)
					candidate.secondary[event.offset] = event.new_secondary;
			}
			else if (event.buffer == ordinary_lcd_history::BufferId::F800) {
				if ((event.write_plane_mask & ordinary_lcd_history::kSecondaryPlane) &&
					(candidate.secondary.empty() || event.offset >= candidate.secondary.size())) {
					return Reject();
				}
				if (event.offset >= candidate.primary.size() ||
					((event.write_plane_mask & ordinary_lcd_history::kPrimaryPlane) &&
					event.old_primary != candidate.primary[event.offset])) {
					return Reject();
				}
				if (event.write_plane_mask & ordinary_lcd_history::kPrimaryPlane)
					collect(candidate.primary_map, primary_changed);
				if ((event.write_plane_mask & ordinary_lcd_history::kSecondaryPlane) &&
					event.old_secondary != candidate.secondary[event.offset]) {
					return Reject();
				}
				if (event.write_plane_mask & ordinary_lcd_history::kSecondaryPlane)
					collect(candidate.secondary_map, secondary_changed);
				if (affected_overflow) {
					return Reject();
				}
				if (!AdvancePixels(candidate,
					std::span<const size_t>(affected.data(), affected_count), event.steady_ns)) {
					return Reject();
				}
				if (event.write_plane_mask & ordinary_lcd_history::kPrimaryPlane)
					candidate.primary[event.offset] = event.new_primary;
				if (event.write_plane_mask & ordinary_lcd_history::kSecondaryPlane) {
					candidate.secondary[event.offset] = event.new_secondary;
				}
			}
			else {
				return Reject();
			}
			for (size_t i = 0; i != affected_count; ++i) {
				const size_t pixel = affected[i];
				const auto& source = candidate.pixel_sources[pixel];
				if (candidate.controls.hardware_id == HW_CLASSWIZ_II)
					candidate.targets[pixel] = ordinary_lcd::EvaluatePixel<true>(
						source, candidate.primary.data(), candidate.secondary.data(), candidate.scan_alpha.data(), candidate.scan.effective_rate >= candidate.scan.flashing_threshold);
				else
					candidate.targets[pixel] = ordinary_lcd::EvaluatePixel<false>(
						source, candidate.primary.data(), nullptr, candidate.scan_alpha.data(), candidate.scan.effective_rate >= candidate.scan.flashing_threshold);
			}
			break;
		}
		case ordinary_lcd_history::Kind::Mode: {
			if (!AdvanceAllPixels(candidate, event.steady_ns)) {
				return Reject();
			}
			const uint8_t mode_mask = candidate.controls.hardware_id == HW_CLASSWIZ_II ? 0x7f :
				(candidate.controls.hardware_id == HW_CLASSWIZ ? 0x3f : 0x07);
			if (event.old_value != candidate.controls.mode ||
				((event.old_value | event.new_value) & ~mode_mask) != 0) {
				return Reject();
			}
			if (candidate.controls.hardware_id == HW_CLASSWIZ_II &&
				((event.old_value ^ event.new_value) & 0x08) != 0) {
				return Reject();
			}
			candidate.controls.mode = event.new_value;
			break;
		}
		case ordinary_lcd_history::Kind::Select:
			// Select is a bus/control bookkeeping event; it does not alter the
			// ordinary LCD target mapping or pixel values.
			if (event.old_value != candidate.controls.select ||
				((event.old_value | event.new_value) & ~uint8_t(0x05)) != 0) {
				return Reject();
			}
			candidate.controls.select = event.new_value;
			break;
		case ordinary_lcd_history::Kind::Range:
			if (!AdvanceAllPixels(candidate, event.steady_ns) ||
				event.old_value != candidate.controls.range ||
				((event.old_value | event.new_value) & ~uint8_t(0x2f)) != 0) {
				return Reject();
			}
			candidate.controls.range = event.new_value;
			break;
		case ordinary_lcd_history::Kind::Contrast:
			if (!AdvanceAllPixels(candidate, event.steady_ns) ||
				event.old_value != candidate.controls.contrast ||
				((event.old_value | event.new_value) & ~uint8_t(0x3f)) != 0) {
				return Reject();
			}
			candidate.controls.contrast = event.new_value;
			break;
		case ordinary_lcd_history::Kind::Brightness:
			if (!AdvanceAllPixels(candidate, event.steady_ns) ||
				event.old_value != candidate.controls.brightness ||
				((event.old_value | event.new_value) & ~uint8_t(0x07)) != 0) {
				return Reject();
			}
			candidate.controls.brightness = event.new_value;
			break;
		case ordinary_lcd_history::Kind::Offset:
			if (!AdvanceAllPixels(candidate, event.steady_ns) ||
				event.old_value != candidate.controls.offset ||
				((event.old_value | event.new_value) & ~uint8_t(0x3f)) != 0) {
				return Reject();
			}
			candidate.controls.offset = event.new_value;
			break;
		case ordinary_lcd_history::Kind::Scan:
			if (!AdvanceAllPixels(candidate, event.steady_ns)) {
				return Reject();
			}
			if (!SameScan(candidate.scan, event.scan_before)) {
				return Reject();
			}
			candidate.scan = ToScanState(event.scan_after_advance, event.state_sdl_ms);
			candidate.scan.raw_rate = event.scan_before.raw_rate;
			RefreshScan(candidate, event.state_sdl_ms);
			candidate.scan = ToScanState(event.scan_after_write, event.state_sdl_ms);
			candidate.anchor_ns = candidate.timeline_ns = event.steady_ns;
			candidate.anchor_sdl_ms = event.state_sdl_ms;
			RefreshScan(candidate, event.state_sdl_ms);
			break;
		default:
			return Reject();
		}
		candidate.seq = event.seq;
		candidate.last_event_ns = event.steady_ns;
		candidate.steady_ns = event.steady_ns;
		candidate.sdl_ms = event.kind == ordinary_lcd_history::Kind::Scan
			? event.state_sdl_ms : event.sdl_ms;
		if (event.kind != ordinary_lcd_history::Kind::Select &&
			event.kind != ordinary_lcd_history::Kind::Byte) {
			if (!BuildTargetsImpl(candidate.controls, candidate.scan,
				candidate.primary, candidate.secondary, candidate.sprites,
				candidate.targets, candidate.active, &candidate.primary_map,
				&candidate.secondary_map, &candidate.pixel_sources, &candidate.status_levels, candidate.scan_alpha.data()))
				return Reject();
			for (size_t i = 0; i != kPixelCount; ++i)
				if (candidate.active[i])
					candidate.alpha_steady_ns[i] = event.steady_ns;
		}
	}
	return true;
}

bool ReplaySession::Finish() {
	if (status_ != ReplayStatus::Pending)
		return Reject();
	Candidate& candidate = *candidate_;
	const auto& cutoff = cutoff_;
	if (candidate.seq != cutoff.end_seq) {
		return Reject();
	}
	if (!AdvanceTimeline(candidate, cutoff.steady_ns))
		return Reject();
	if (!AdvanceAllPixels(candidate, cutoff.steady_ns)) {
		return Reject();
	}
	candidate.steady_ns = cutoff.steady_ns;
	candidate.sdl_ms = cutoff.sdl_ms;
	// Inactive alpha stays constant: it is also known at this observation time,
	// without integrating across its disabled interval.
	candidate.alpha_steady_ns.fill(cutoff.steady_ns);
	status_ = ReplayStatus::Ready;
	return true;
}

const Candidate* ReplaySession::Result() const {
	return status_ == ReplayStatus::Ready ? candidate_.get() : nullptr;
}

} // namespace casioemu::lcd_temporal
