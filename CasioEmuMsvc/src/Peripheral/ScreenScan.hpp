#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

#include "ScreenGate.hpp"

namespace casioemu {
struct MMURegion;

namespace screen_scan {

// Native desktop CW/CWII uses the independent scan-report model by default.
// Define CASIOEMU_DISABLE_INDEPENDENT_SCAN_REPORT to restore the legacy path.
// Web/Emscripten and Android intentionally remain on the legacy path. The
// legacy ENABLE macro is not consulted, so platform and DISABLE always win.
#if !defined(CASIOEMU_CORE_WEB) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && \
	!defined(CASIOEMU_DISABLE_INDEPENDENT_SCAN_REPORT)
inline constexpr bool kEnableIndependentScanReport = true;
#else
inline constexpr bool kEnableIndependentScanReport = false;
#endif

using Gate = screen_gate::Gate;

struct AdvanceResult {
	int phase_rate = 0;
	int effective_rate = 0;
	uint8_t option1 = 0;
	uint8_t option_enable = 0;
	uint8_t report = 0;
	bool active = false;
	uint64_t effective_now_ms = 0;
	Gate gate{};
};

uint8_t CalculateScanReportFromPhase(int n, uint8_t option1, uint8_t option_enable);

class State {
public:
	void Activate();
	void Deactivate();
	AdvanceResult Advance(uint64_t now_ms, Gate gate);
	void WriteRate(uint64_t now_ms, Gate gate, uint8_t value);
	void WriteOption1(uint64_t now_ms, Gate gate, uint8_t value);
	void WriteOptionEnable(uint64_t now_ms, Gate gate, uint8_t value);
	uint8_t ReadRate(Gate gate, uint64_t now_ms);
	uint8_t ReadOption1() const;
	uint8_t ReadOptionEnable() const;
	uint8_t GetRateForState() const;
	void LoadRate(uint8_t value);

private:
	AdvanceResult AdvanceLocked(uint64_t now_ms, Gate gate);

	mutable std::mutex mutex;
	uint8_t refresh_rate = 0;
	uint8_t option1 = 0;
	uint8_t option_enable = 0;
	uint8_t last_report = 0;
	bool active = false;
	bool gate_initialized = false;
	bool time_initialized = false;
	uint64_t last_time_ms = 0;
	Gate observed_gate{};
};

Gate CurrentGate();

uint8_t ReadRefreshRate(MMURegion*, size_t);
uint8_t ReadOption1(MMURegion*, size_t);
uint8_t ReadOptionEnable(MMURegion*, size_t);
uint8_t ReadReport(MMURegion*, size_t);
void WriteRefreshRate(MMURegion*, size_t, uint8_t);
void WriteOption1(MMURegion*, size_t, uint8_t);
void WriteOptionEnable(MMURegion*, size_t, uint8_t);

} // namespace screen_scan
} // namespace casioemu
