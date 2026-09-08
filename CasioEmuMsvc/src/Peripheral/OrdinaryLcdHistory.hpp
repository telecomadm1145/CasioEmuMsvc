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

	bool Changes() const {
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

struct ConsumeResult {
	size_t count = 0;
	uint64_t last_seq = 0;
	uint64_t epoch = 0;
	bool incomplete = false;
};

struct WorkerState {
	Batch batch;
	uint64_t consumed_seq = 0;
	uint64_t consumed_count = 0;
	bool incomplete = false;
};

struct DisabledBatch {};

struct DisabledWorkerState {
	DisabledBatch batch;
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
	ConsumeResult Consume(Batch& batch);
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
	ConsumeResult Consume(DisabledBatch&) { return {}; }
	bool Incomplete() const { return false; }
	uint64_t Dropped() const { return 0; }
	void InvalidateEpoch() {}
	void Reset() {}
};

} // namespace casioemu::ordinary_lcd_history
