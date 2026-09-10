#include "ScreenScan.hpp"

#include "Chipset/MMURegion.hpp"

#include <SDL.h>

namespace casioemu::screen_scan {

namespace {

constexpr int EffectiveRate(uint8_t rate) {
	return rate < 6 ? 6 : rate;
}

bool IsFrozen(uint8_t rate, Gate gate) {
	return static_cast<int>(rate) < gate.flashing_threshold && !gate.fading_enabled;
}

} // namespace

uint8_t CalculateScanReportFromPhase(int n, uint8_t option1, uint8_t option_enable) {
	// F035 permits zero while F036 can be truthy. Use the disabled-divider
	// period as a defined fallback for that reachable C++ divide-by-zero case;
	// this does not assert a hardware rule for the invalid register combination.
	const int divisor = option_enable ? (option1 == 0 ? 64 : option1) : 64;
	return static_cast<uint8_t>(((n / divisor) % 2 ? 3 : 0) ^
		(n % 64 == 0 ? 1 : (n % 64 == 32 ? 2 : 0)));
}

std::unique_lock<std::recursive_mutex> State::LockHistory() const {
	if constexpr (ordinary_lcd_history::kEnabled && kEnableIndependentScanReport)
		if (auto* owner = history.load(std::memory_order_acquire)) return owner->Lock();
	return {};
}

ordinary_lcd_history::ScanSnapshot State::CaptureSnapshot() const {
	std::lock_guard<std::mutex> lock(mutex);
	const auto s = SnapshotLocked();
	return {s.raw_rate, s.effective_rate, s.option1, s.option_enable,
		s.gate.flashing_threshold, s.gate.fading_enabled, s.gate.version,
		s.active, s.gate_initialized};
}

void State::Activate() {
	auto history_lock = LockHistory();
	if constexpr (ordinary_lcd_history::kEnabled && kEnableIndependentScanReport) {
		if (history_started.exchange(true, std::memory_order_acq_rel))
			if (auto* history = this->history.load(std::memory_order_acquire))
				history->InvalidateEpoch();
	}
	std::lock_guard<std::mutex> lock(mutex);
	if (active)
		return;
	active = true;
	gate_initialized = false;
	time_initialized = false;
}

void State::Deactivate() {
	auto history_lock = LockHistory();
	if constexpr (ordinary_lcd_history::kEnabled && kEnableIndependentScanReport) {
	if (auto* history = this->history.load(std::memory_order_acquire))
		history->InvalidateEpoch();
	}
	std::lock_guard<std::mutex> lock(mutex);
	active = false;
	gate_initialized = false;
	time_initialized = false;
	last_time_ms = 0;
	refresh_rate = 0;
	option1 = 0;
	option_enable = 0;
	last_report = 0;
}

void State::SetHistory(ordinary_lcd_history::History* value) {
	history.store(value, std::memory_order_release);
}

State::Snapshot State::SnapshotLocked() const {
	Snapshot snapshot{};
	snapshot.raw_rate = refresh_rate;
	snapshot.effective_rate = static_cast<uint8_t>(EffectiveRate(refresh_rate));
	snapshot.option1 = option1;
	snapshot.option_enable = option_enable;
	snapshot.gate = gate_initialized ? observed_gate : Gate{};
	snapshot.active = active;
	snapshot.gate_initialized = gate_initialized;
	return snapshot;
}

void State::ApplyOperationLocked(OperationContext& context) {
	context.before = SnapshotLocked();
	context.result = AdvanceLocked(context.now_ms, context.gate);
	context.after_advance = SnapshotLocked();
	switch (context.operation) {
	case ordinary_lcd_history::ScanOperation::WriteRate:
		refresh_rate = context.value & 0x7f;
		break;
	case ordinary_lcd_history::ScanOperation::WriteOption1:
		option1 = context.value & 0x1e;
		break;
	case ordinary_lcd_history::ScanOperation::WriteOptionEnable:
		option_enable = context.value & 0b1001;
		break;
	case ordinary_lcd_history::ScanOperation::Advance:
		break;
	}
	context.after = SnapshotLocked();
	if (context.result.active)
		context.after_advance.gate = context.after.gate = context.result.gate;
}

ordinary_lcd_history::PrepareResult State::PrepareOperation(
	void* raw, ordinary_lcd_history::Event& event) noexcept {
	auto* context = static_cast<OperationContext*>(raw);
	std::lock_guard<std::mutex> lock(context->state->mutex);
	context->state->ApplyOperationLocked(*context);
	event.kind = ordinary_lcd_history::Kind::Scan;
	event.scan_operation = context->operation;
	event.scan_before = {context->before.raw_rate, context->before.effective_rate,
		context->before.option1, context->before.option_enable,
		context->before.gate.flashing_threshold, context->before.gate.fading_enabled,
		context->before.gate.version, context->before.active, context->before.gate_initialized};
	event.scan_after_advance = {context->after_advance.raw_rate, context->after_advance.effective_rate,
		context->after_advance.option1, context->after_advance.option_enable,
		context->after_advance.gate.flashing_threshold, context->after_advance.gate.fading_enabled,
		context->after_advance.gate.version, context->after_advance.active, context->after_advance.gate_initialized};
	event.scan_after_write = {context->after.raw_rate, context->after.effective_rate,
		context->after.option1, context->after.option_enable,
		context->after.gate.flashing_threshold, context->after.gate.fading_enabled,
		context->after.gate.version, context->after.active, context->after.gate_initialized};
	event.state_sdl_ms = context->result.effective_now_ms;
	return event.Changes()
		? ordinary_lcd_history::PrepareResult::Commit
		: ordinary_lcd_history::PrepareResult::NoChange;
}

void State::NoopWrite(void*) noexcept {}

AdvanceResult State::Advance(uint64_t now_ms, Gate gate) {
	auto history_lock = LockHistory();
	if constexpr (ordinary_lcd_history::kEnabled && kEnableIndependentScanReport) {
	if (auto* history = this->history.load(std::memory_order_acquire)) {
		OperationContext context{this, ordinary_lcd_history::ScanOperation::Advance, now_ms, gate};
		if (history->TryRecord(&State::PrepareOperation, &State::NoopWrite, &context) ==
			ordinary_lcd_history::TransactionResult::Committed)
			return context.result;
	}
	}
	std::lock_guard<std::mutex> lock(mutex);
	return AdvanceLocked(now_ms, gate);
}

AdvanceResult State::AdvanceLocked(uint64_t now_ms, Gate gate) {
	const int phase_rate = refresh_rate;
	// Gate snapshots are captured before waiting for this state mutex. If an
	// older callback acquires the state mutex after a newer snapshot was
	// observed, retain the newer authoritative gate instead of rolling it back.
	const Gate actual_gate = gate_initialized && gate.version < observed_gate.version
		? observed_gate
		: gate;
	if (!active)
		return {phase_rate, phase_rate, option1, option_enable, 0, false, now_ms, actual_gate};
	uint64_t effective_now_ms = now_ms;
	// Callers sample SDL_GetTicks64() before waiting for this mutex. Preserve
	// one linearized, monotonic timeline when an older sample acquires the lock
	// after a newer callback; never move the absolute phase backwards.
	if (time_initialized && effective_now_ms < last_time_ms)
		effective_now_ms = last_time_ms;
	last_time_ms = effective_now_ms;
	time_initialized = true;

	const Gate previous_gate = gate_initialized ? observed_gate : actual_gate;
	const bool was_frozen = IsFrozen(refresh_rate, previous_gate);
	const bool now_frozen = IsFrozen(refresh_rate, actual_gate);
	if (!was_frozen) {
		const int n = static_cast<int>((effective_now_ms * static_cast<uint64_t>(phase_rate) / 250) % 64);
		last_report = CalculateScanReportFromPhase(n, option1, option_enable);
	}
	if (gate_initialized && was_frozen && !now_frozen) {
		const int n = static_cast<int>((effective_now_ms * static_cast<uint64_t>(phase_rate) / 250) % 64);
		last_report = CalculateScanReportFromPhase(n, option1, option_enable);
	}
	observed_gate = actual_gate;
	gate_initialized = true;
	refresh_rate = static_cast<uint8_t>(EffectiveRate(refresh_rate));
	return {phase_rate, refresh_rate, option1, option_enable, last_report, true, effective_now_ms, actual_gate};
}

void State::WriteRate(uint64_t now_ms, Gate gate, uint8_t value) {
	auto history_lock = LockHistory();
	if constexpr (ordinary_lcd_history::kEnabled && kEnableIndependentScanReport) {
	if (auto* history = this->history.load(std::memory_order_acquire)) {
		OperationContext context{this, ordinary_lcd_history::ScanOperation::WriteRate, now_ms, gate, value};
		if (history->TryRecord(&State::PrepareOperation, &State::NoopWrite, &context) ==
			ordinary_lcd_history::TransactionResult::Committed)
			return;
	}
	}
	std::lock_guard<std::mutex> lock(mutex);
	AdvanceLocked(now_ms, gate);
	refresh_rate = value & 0x7f;
}

void State::WriteOption1(uint64_t now_ms, Gate gate, uint8_t value) {
	auto history_lock = LockHistory();
	if constexpr (ordinary_lcd_history::kEnabled && kEnableIndependentScanReport) {
	if (auto* history = this->history.load(std::memory_order_acquire)) {
		OperationContext context{this, ordinary_lcd_history::ScanOperation::WriteOption1, now_ms, gate, value};
		if (history->TryRecord(&State::PrepareOperation, &State::NoopWrite, &context) ==
			ordinary_lcd_history::TransactionResult::Committed)
			return;
	}
	}
	std::lock_guard<std::mutex> lock(mutex);
	AdvanceLocked(now_ms, gate);
	option1 = value & 0x1e;
}

void State::WriteOptionEnable(uint64_t now_ms, Gate gate, uint8_t value) {
	auto history_lock = LockHistory();
	if constexpr (ordinary_lcd_history::kEnabled && kEnableIndependentScanReport) {
	if (auto* history = this->history.load(std::memory_order_acquire)) {
		OperationContext context{this, ordinary_lcd_history::ScanOperation::WriteOptionEnable, now_ms, gate, value};
		if (history->TryRecord(&State::PrepareOperation, &State::NoopWrite, &context) ==
			ordinary_lcd_history::TransactionResult::Committed)
			return;
	}
	}
	std::lock_guard<std::mutex> lock(mutex);
	AdvanceLocked(now_ms, gate);
	option_enable = value & 0b1001;
}

uint8_t State::ReadRate(Gate gate, uint64_t now_ms) {
	const auto result = Advance(now_ms, gate);
	return static_cast<uint8_t>(result.effective_rate) & 0x7f;
}

uint8_t State::ReadOption1() const {
	std::lock_guard<std::mutex> lock(mutex);
	return option1;
}

uint8_t State::ReadOptionEnable() const {
	std::lock_guard<std::mutex> lock(mutex);
	return option_enable;
}

uint8_t State::GetRateForState() const {
	std::lock_guard<std::mutex> lock(mutex);
	return refresh_rate;
}

void State::LoadRate(uint8_t value) {
	auto history_lock = LockHistory();
	if constexpr (ordinary_lcd_history::kEnabled && kEnableIndependentScanReport) {
	if (auto* history = this->history.load(std::memory_order_acquire))
		history->InvalidateEpoch();
	}
	std::lock_guard<std::mutex> lock(mutex);
	refresh_rate = value;
}

Gate CurrentGate() {
	return screen_gate::Get();
}

uint8_t ReadRefreshRate(MMURegion* region, size_t) {
	return static_cast<State*>(region->userdata)->ReadRate(CurrentGate(), SDL_GetTicks64());
}

uint8_t ReadOption1(MMURegion* region, size_t) {
	return static_cast<State*>(region->userdata)->ReadOption1();
}

uint8_t ReadOptionEnable(MMURegion* region, size_t) {
	return static_cast<State*>(region->userdata)->ReadOptionEnable();
}

uint8_t ReadReport(MMURegion* region, size_t) {
	const auto result = static_cast<State*>(region->userdata)->Advance(SDL_GetTicks64(), CurrentGate());
	return result.active ? result.report : 0;
}

void WriteRefreshRate(MMURegion* region, size_t, uint8_t data) {
	static_cast<State*>(region->userdata)->WriteRate(SDL_GetTicks64(), CurrentGate(), data);
}

void WriteOption1(MMURegion* region, size_t, uint8_t data) {
	static_cast<State*>(region->userdata)->WriteOption1(SDL_GetTicks64(), CurrentGate(), data);
}

void WriteOptionEnable(MMURegion* region, size_t, uint8_t data) {
	static_cast<State*>(region->userdata)->WriteOptionEnable(SDL_GetTicks64(), CurrentGate(), data);
}

} // namespace casioemu::screen_scan
