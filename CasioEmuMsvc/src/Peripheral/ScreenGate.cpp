#include "ScreenGate.hpp"
#include "OrdinaryLcdHistory.hpp"

#include <mutex>

namespace casioemu::screen_gate {

namespace {
	std::mutex gate_mutex;
	Gate gate{};
	uint64_t next_version = 0;
}

Gate Get() {
	std::lock_guard<std::mutex> lock(gate_mutex);
	return gate;
}

void Set(Gate value) {
	ordinary_lcd_history::UntrackedChange change;
	std::lock_guard<std::mutex> lock(gate_mutex);
	gate.flashing_threshold = value.flashing_threshold;
	gate.fading_enabled = value.fading_enabled;
	gate.version = ++next_version;
}

void SetFlashingThreshold(int value) {
	ordinary_lcd_history::UntrackedChange change;
	std::lock_guard<std::mutex> lock(gate_mutex);
	gate.flashing_threshold = value;
	gate.version = ++next_version;
}

void SetFadingEnabled(bool value) {
	ordinary_lcd_history::UntrackedChange change;
	std::lock_guard<std::mutex> lock(gate_mutex);
	gate.fading_enabled = value;
	gate.version = ++next_version;
}

} // namespace casioemu::screen_gate
