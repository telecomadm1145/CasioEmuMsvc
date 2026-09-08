#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace casioemu::ordinary_lcd_history {

// C2a is an opt-in collector. It is deliberately independent from the
// display path and is hard-disabled for Web/Emscripten and Android.
#if defined(CASIOEMU_ENABLE_ORDINARY_LCD_HISTORY) && \
	!defined(CASIOEMU_CORE_WEB) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
inline constexpr bool kEnabled = true;
#else
inline constexpr bool kEnabled = false;
#endif

inline constexpr size_t kCapacity = 4096;
inline constexpr size_t kBatchCapacity = 256;

enum class BufferId : uint8_t {
	F800 = 0,
	F800Secondary = 1,
};

enum PlaneMask : uint8_t {
	kPrimaryPlane = 1 << 0,
	kSecondaryPlane = 1 << 1,
};

enum class Kind : uint8_t {
	Byte,
	Select,
	Mode,
	Scan,
};

enum class ScanOperation : uint8_t {
	Advance,
	WriteRate,
	WriteOption1,
	WriteOptionEnable,
};

struct ScanSnapshot {
	uint8_t raw_rate = 0;
	uint8_t effective_rate = 0;
	uint8_t option1 = 0;
	uint8_t option_enable = 0;
	int flashing_threshold = 0;
	bool fading_enabled = false;
	uint64_t gate_version = 0;
	bool active = false;
	bool gate_initialized = false;

	bool operator==(const ScanSnapshot& other) const {
		return raw_rate == other.raw_rate && effective_rate == other.effective_rate &&
			option1 == other.option1 && option_enable == other.option_enable &&
			flashing_threshold == other.flashing_threshold &&
			fading_enabled == other.fading_enabled && gate_version == other.gate_version &&
			active == other.active && gate_initialized == other.gate_initialized;
	}
};

enum class PrepareResult : uint8_t {
	NoChange,
	Commit,
	Reject,
};

enum class TransactionResult : uint8_t {
	Committed,
	Rejected,
	Failed,
};

struct Event {
	uint64_t epoch = 0;
	uint64_t seq = 0;
	uint64_t steady_ns = 0;
	uint64_t sdl_ms = 0;
	Kind kind = Kind::Byte;
	BufferId buffer = BufferId::F800;
	uint32_t offset = 0;
	uint8_t old_primary = 0;
	uint8_t new_primary = 0;
	uint8_t old_secondary = 0;
	uint8_t new_secondary = 0;
	uint8_t write_plane_mask = 0;
	uint8_t old_value = 0;
	uint8_t new_value = 0;
	uint64_t state_sdl_ms = 0;
	ScanOperation scan_operation = ScanOperation::Advance;
	ScanSnapshot scan_before{};
	ScanSnapshot scan_after_advance{};
	ScanSnapshot scan_after_write{};

	bool Changes() const {
		if (kind == Kind::Scan) {
			return !(scan_before == scan_after_advance) || !(scan_after_advance == scan_after_write);
		}
		if (kind != Kind::Byte)
			return old_value != new_value;
		return ((write_plane_mask & kPrimaryPlane) && old_primary != new_primary) ||
			((write_plane_mask & kSecondaryPlane) && old_secondary != new_secondary);
	}
};

struct Batch {
	std::array<Event, kBatchCapacity> events{};
	size_t size = 0;
};

struct Cutoff {
	uint64_t epoch = 0;
	uint64_t end_seq = 0;
	uint64_t steady_ns = 0;
	uint64_t sdl_ms = 0;
};

struct ConsumeResult {
	size_t count = 0;
	uint64_t last_seq = 0;
	uint64_t epoch = 0;
	bool epoch_matches = false;
	bool queue_epoch_matches = false;
	bool cutoff_complete = false;
	bool incomplete = false;
};

struct WorkerState {
	Batch batch;
	Cutoff cutoff{};
	bool cutoff_pending = false;
	uint64_t consumed_seq = 0;
	uint64_t consumed_count = 0;
	bool incomplete = false;
};

struct DisabledBatch {};

struct DisabledWorkerState {
	DisabledBatch batch;
	Cutoff cutoff{};
	bool cutoff_pending = false;
	uint64_t consumed_seq = 0;
	uint64_t consumed_count = 0;
	bool incomplete = false;
};

using PrepareFn = PrepareResult (*)(void*, Event&) noexcept;
using WriteFn = void (*)(void*) noexcept;

class History {
public:
	// Returns the transaction outcome after the original write ran while holding history mutex. The
	// prepare callback reads old bytes and fills Event under that same lock;
	// Failed/Rejected means the caller must perform the original write itself.
	TransactionResult TryRecord(PrepareFn prepare, WriteFn write, void* context);
	Cutoff CaptureCutoff() const;
	ConsumeResult ConsumeUntil(const Cutoff& cutoff, Batch& batch);
	bool Incomplete() const;
	uint64_t Dropped() const;

	// These lifecycle operations are intentionally explicit. Reset requires all
	// producers to be stopped; no runtime path calls it in C2a.
	void InvalidateEpoch();
	void Reset();

private:
	mutable std::mutex mutex;
	std::array<Event, kCapacity> ring{};
	size_t head = 0;
	size_t count = 0;
	uint64_t epoch = 1;
	uint64_t next_seq = 0;
	std::atomic_bool incomplete{false};
	std::atomic_uint64_t dropped{0};
};

// Keeps disabled builds free of the ring, mutex, and batch storage in each
// Screen instance. Calls are normally removed by if constexpr at the caller.
struct DisabledHistory {
	TransactionResult TryRecord(PrepareFn, WriteFn, void*) { return TransactionResult::Failed; }
	Cutoff CaptureCutoff() const { return {}; }
	ConsumeResult ConsumeUntil(const Cutoff&, DisabledBatch&) { return {}; }
	bool Incomplete() const { return false; }
	uint64_t Dropped() const { return 0; }
	void InvalidateEpoch() {}
	void Reset() {}
};

} // namespace casioemu::ordinary_lcd_history
