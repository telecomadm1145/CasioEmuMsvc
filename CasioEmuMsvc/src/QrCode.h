#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace casioemu {
	class Emulator;

	struct QrCodeHistoryEntry {
		uint64_t Id = 0;
		int Version = 0;
		std::string Data;
	};

	struct QrCodeState {
		bool Active = false;
		bool Complete = false;
		int Version = 0;
		uint64_t Revision = 0;
		std::string Data;
		std::vector<QrCodeHistoryEntry> History;
		uint8_t RealCurrentPage = 0;
		uint8_t RealTotalPages = 0;
		std::string RealCurrentPageData;
		std::vector<size_t> RealPageLengths;
	};

	class QrCodeCapture {
	public:
		bool HandleStop(Emulator& emulator);
		bool Poll(Emulator& emulator);
		void Reset(bool clearHistory = true);
		QrCodeState GetState() const;

	private:
		bool PollRealHardware(Emulator& emulator);
		bool SetCode(int version, std::string data, bool addHistory = true);
		bool Clear();

		mutable std::mutex mutex_;
		bool active_ = false;
		int version_ = 0;
		uint64_t revision_ = 0;
		uint64_t next_history_id_ = 1;
		std::string data_;
		bool complete_ = false;
		std::string pending_real_data_;
		uint8_t pending_real_page_ = 0;
		std::string real_current_page_data_;
		std::vector<std::string> real_pages_;
		uint8_t real_last_page_ = 0;
		uint8_t real_current_page_ = 0;
		uint8_t real_total_pages_ = 0;
		bool real_session_recorded_ = false;
		bool real_active_session_recorded_ = false;
		bool real_inactive_pending_ = false;
		bool real_seen_inactive_ = false;
		std::chrono::steady_clock::time_point real_inactive_since_{};
		std::vector<QrCodeHistoryEntry> history_;
	};
}
