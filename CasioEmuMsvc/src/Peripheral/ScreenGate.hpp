#pragma once

#include <cstdint>

namespace casioemu::screen_gate {

struct Gate {
	int flashing_threshold = 20;
	bool fading_enabled = false;
	// Monotonically increasing publication number assigned under the gate mutex.
	// Callers may observe it, but Set() never accepts a caller-provided value.
	uint64_t version = 0;
};

Gate Get();
void Set(Gate gate);
void SetFlashingThreshold(int value);
void SetFadingEnabled(bool value);

} // namespace casioemu::screen_gate
