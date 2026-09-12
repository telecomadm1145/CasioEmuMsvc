#pragma once

#include "ModelInfo.h"

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace casioemu {

class ePSCPU;

class EpsScreenTemporalState {
public:
	EpsScreenTemporalState();
	~EpsScreenTemporalState();
	EpsScreenTemporalState(const EpsScreenTemporalState&) = delete;
	EpsScreenTemporalState& operator=(const EpsScreenTemporalState&) = delete;
	void Reset();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
	friend void UpdateEpsScreen(struct EpsScreenContext& context);
};

struct EpsScreenSpec {
	int pixel_width = 0;
	int pixel_height = 0;
	int export_width = 0;
	int export_height = 0;
	int logical_width = 0;
	int logical_height = 0;
	int internal_stride = 192;
	int storage_rows = 0;
	int storage_row_bytes = 0;
};

EpsScreenSpec GetEpsScreenSpec(
	HardwareId hardware_id,
	int model_screen_width,
	int model_screen_height);

struct EpsLcdResponseTick {
	bool enabled = false;
	double rise_gain = 0.0;
	double fall_gain = 0.0;
};

struct EpsScreenContext {
	HardwareId hardware_id;
	ePSCPU* eps_cpu = nullptr;
	const std::vector<StatusIndicatorInfo>& status_indicators;
	std::array<float, 66 * 192>& eps_screen_ink_alpha;
	std::mutex& eps_screen_alpha_mutex;
	EpsScreenTemporalState& temporal_state;
	bool residual_enabled = false;
	float residual_alpha_scale = 1.0f;
	float transition_ratio = 0.0f;
	EpsLcdResponseTick response;
};

void UpdateEpsScreen(EpsScreenContext& context);

} // namespace casioemu
