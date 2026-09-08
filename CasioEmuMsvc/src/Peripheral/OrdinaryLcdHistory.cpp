#include "OrdinaryLcdHistory.hpp"

#include <chrono>

#include <SDL.h>

namespace casioemu::ordinary_lcd_history {

namespace {

uint64_t SteadyNowNs() {
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace

TransactionResult History::TryRecord(PrepareFn prepare, WriteFn write, void* context) {
	std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
	if (!lock.owns_lock()) {
		incomplete.store(true, std::memory_order_release);
		dropped.fetch_add(1, std::memory_order_relaxed);
		return TransactionResult::Failed;
	}
	if (incomplete.load(std::memory_order_acquire) || count == ring.size()) {
		incomplete.store(true, std::memory_order_release);
		dropped.fetch_add(1, std::memory_order_relaxed);
		return TransactionResult::Failed;
	}

	Event event{};
	const PrepareResult prepared = prepare(context, event);
	if (prepared == PrepareResult::Reject) {
		incomplete.store(true, std::memory_order_release);
		return TransactionResult::Rejected;
	}
	if (prepared == PrepareResult::NoChange) {
		// Same-value writes have no extra byte-side effect, but still execute
		// under the transaction lock so they cannot bypass Incomplete handling.
		write(context);
		return TransactionResult::Committed;
	}
	event.epoch = epoch;
	event.seq = ++next_seq;
	// Sampling after taking the mutex makes timestamp order follow commit order.
	event.steady_ns = SteadyNowNs();
	event.sdl_ms = SDL_GetTicks64();
	// The original write runs under this short history lock, preserving the
	// event/write order without taking Screen's state mutex.
	write(context);
	ring[(head + count) % ring.size()] = event;
	++count;
	return TransactionResult::Committed;
}

ConsumeResult History::Consume(Batch& batch) {
	std::lock_guard<std::mutex> lock(mutex);
	batch.size = 0;
	while (batch.size < batch.events.size() && count != 0) {
		batch.events[batch.size++] = ring[head];
		head = (head + 1) % ring.size();
		--count;
	}
	return {
		batch.size,
		batch.size == 0 ? 0 : batch.events[batch.size - 1].seq,
		epoch,
		incomplete.load(std::memory_order_acquire)};
}

bool History::Incomplete() const {
	return incomplete.load(std::memory_order_acquire);
}

uint64_t History::Dropped() const {
	return dropped.load(std::memory_order_relaxed);
}

void History::InvalidateEpoch() {
	std::lock_guard<std::mutex> lock(mutex);
	incomplete.store(true, std::memory_order_release);
}

void History::Reset() {
	std::lock_guard<std::mutex> lock(mutex);
	head = 0;
	count = 0;
	++epoch;
	incomplete.store(false, std::memory_order_release);
	dropped.store(0, std::memory_order_relaxed);
}

} // namespace casioemu::ordinary_lcd_history
