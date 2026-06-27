#include "QrCode.h"

#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Emulator.hpp"
#include "ModelInfo.h"
#include <algorithm>

namespace {
	constexpr uint8_t kQrVersion11 = 0x05;
	constexpr uint8_t kQrExit = 0x06;
	constexpr uint8_t kQrVersion3 = 0x07;
	constexpr size_t kQrBufferCapacity = 0x1800;
	constexpr size_t kRealQrBufferCapacity = 0x300;

	bool ReadRealQrData(
		casioemu::Emulator& emulator,
		uint32_t dataAddress,
		std::string& data) {
		data.clear();
		data.reserve(256);
		bool terminated = false;
		for (size_t i = 0; i < kRealQrBufferCapacity; ++i) {
			const auto value = emulator.chipset.mmu.ReadData(dataAddress + i, false);
			if (value == 0) {
				terminated = true;
				break;
			}
			if (value < 0x20 || value > 0x7e)
				return false;
			data.push_back(static_cast<char>(value));
		}
		return terminated && !data.empty();
	}

	bool RealQrDataAddress(
		const casioemu::Emulator& emulator,
		uint32_t& dataAddress) {
		if (!emulator.ModelDefinition.real_hardware)
			return false;
		switch (emulator.hardware_id) {
		case casioemu::HW_CLASSWIZ:
			dataAddress = 0xed8a;
			return true;
		case casioemu::HW_CLASSWIZ_II:
			dataAddress = 0xed9e;
			return true;
		default:
			return false;
		}
	}

	bool RealQrContext(
		const casioemu::Emulator& emulator,
		uint32_t& contextAddress,
		uint32_t& dataAddress,
		uint32_t& pageAddress) {
		if (!emulator.ModelDefinition.real_hardware)
			return false;
		switch (emulator.hardware_id) {
		case casioemu::HW_CLASSWIZ:
			contextAddress = 0xefb0;
			dataAddress = 0xed8a;
			pageAddress = contextAddress + 0x0c;
			return true;
		case casioemu::HW_CLASSWIZ_II:
			contextAddress = 0xef96;
			dataAddress = 0xed9e;
			pageAddress = contextAddress + 0x0c;
			return true;
		default:
			return false;
		}
	}

	bool FindRealQrPages(
		casioemu::Emulator& emulator,
		uint32_t dataAddress,
		uint32_t preferredPageAddress,
		uint8_t& currentPage,
		uint8_t& totalPages) {
		currentPage = 0;
		totalPages = 0;
		if (preferredPageAddress) {
			const auto page = emulator.chipset.mmu.ReadData(preferredPageAddress, false);
			const auto total = emulator.chipset.mmu.ReadData(preferredPageAddress - 1, false);
			if (page >= 1 && page <= 16 && total >= page && total <= 16) {
				currentPage = page;
				totalPages = total;
				return true;
			}
		}

		for (uint32_t address = 0xef00; address + 0x10 < 0xf020; ++address) {
			const auto ptrLo = emulator.chipset.mmu.ReadData(address, false);
			if (ptrLo != (dataAddress & 0xff))
				continue;
			const auto ptrHi = emulator.chipset.mmu.ReadData(address + 1, false);
			if (ptrHi != ((dataAddress >> 8) & 0xff))
				continue;
			const auto page = emulator.chipset.mmu.ReadData(address + 0x0c, false);
			const auto total = emulator.chipset.mmu.ReadData(address + 0x0b, false);
			if (page >= 1 && page <= 16 && total >= page && total <= 16) {
				currentPage = page;
				totalPages = total;
				return true;
			}
		}
		return false;
	}

	bool FindRealQrContext(
		casioemu::Emulator& emulator,
		uint32_t& dataAddress,
		uint8_t& currentPage,
		uint8_t& totalPages,
		std::string& data) {
		for (uint32_t address = 0xef00; address + 0x10 < 0xf020; ++address) {
			const auto ptrLo = emulator.chipset.mmu.ReadData(address, false);
			const auto ptrHi = emulator.chipset.mmu.ReadData(address + 1, false);
			const uint32_t candidateDataAddress = ptrLo | (ptrHi << 8);
			if (candidateDataAddress < 0xed00 || candidateDataAddress >= 0xef00)
				continue;

			const auto total = emulator.chipset.mmu.ReadData(address + 0x0b, false);
			const auto page = emulator.chipset.mmu.ReadData(address + 0x0c, false);
			if (page < 1 || page > 16 || total < page || total > 16)
				continue;

			std::string candidateData;
			if (!ReadRealQrData(emulator, candidateDataAddress, candidateData))
				continue;

			dataAddress = candidateDataAddress;
			currentPage = page;
			totalPages = total;
			data = std::move(candidateData);
			return true;
		}
		return false;
	}

	bool IsRealQrActive(casioemu::Emulator& emulator) {
		if (!emulator.ModelDefinition.real_hardware)
			return false;
		switch (emulator.hardware_id) {
		case casioemu::HW_CLASSWIZ:
			{
				const auto state = emulator.chipset.mmu.ReadData(0xd113, false);
				return state == 1 || state == 3;
			}
		case casioemu::HW_CLASSWIZ_II:
			return emulator.chipset.mmu.ReadData(0xf000, false) == 5;
		default:
			return false;
		}
	}

	bool SimulatorQrAddresses(
		const casioemu::Emulator& emulator,
		uint32_t& stopTypeAddress,
		uint32_t& dataAddress) {
		if (emulator.ModelDefinition.real_hardware)
			return false;
		switch (emulator.hardware_id) {
		case casioemu::HW_CLASSWIZ:
			stopTypeAddress = 0x48e00;
			dataAddress = 0x4a800;
			break;
		case casioemu::HW_CLASSWIZ_II:
			stopTypeAddress = 0x88e00;
			dataAddress = 0x8a800;
			break;
		default:
			return false;
		}
		if (emulator.ModelDefinition.is_sample_rom)
			stopTypeAddress += 7;
		return true;
	}
}

namespace casioemu {
	bool QrCodeCapture::HandleStop(Emulator& emulator) {
		uint32_t stopTypeAddress = 0;
		uint32_t dataAddress = 0;
		if (!SimulatorQrAddresses(emulator, stopTypeAddress, dataAddress))
			return false;

		const uint8_t stopType = emulator.chipset.mmu.ReadData(stopTypeAddress, false);
		if (stopType == kQrExit) {
			return Clear();
		}
		if (stopType != kQrVersion11 && stopType != kQrVersion3)
			return false;

		std::string data;
		data.reserve(256);
		for (size_t i = 0; i < kQrBufferCapacity; ++i) {
			const auto value = emulator.chipset.mmu.ReadData(dataAddress + i, false);
			if (value == 0)
				break;
			data.push_back(static_cast<char>(value));
		}
		return SetCode(stopType == kQrVersion3 ? 3 : 11, std::move(data));
	}

	bool QrCodeCapture::Poll(Emulator& emulator) {
		return PollRealHardware(emulator);
	}

	void QrCodeCapture::Reset(bool clearHistory) {
		std::lock_guard lock(mutex_);
		active_ = false;
		complete_ = false;
		version_ = 0;
		data_.clear();
		pending_real_data_.clear();
		pending_real_page_ = 0;
		real_current_page_data_.clear();
		real_pages_.clear();
		real_last_page_ = 0;
		real_current_page_ = 0;
		real_total_pages_ = 0;
		real_session_recorded_ = false;
		real_active_session_recorded_ = false;
		real_inactive_pending_ = false;
		real_seen_inactive_ = false;
		if (clearHistory) {
			history_.clear();
			next_history_id_ = 1;
		}
		++revision_;
	}

	QrCodeState QrCodeCapture::GetState() const {
		std::lock_guard lock(mutex_);
		QrCodeState state{
			active_,
			complete_,
			version_,
			revision_,
			data_,
			history_,
			real_current_page_,
			real_total_pages_,
			real_current_page_data_};
		state.RealPageLengths.reserve(real_pages_.size());
		for (const auto& page : real_pages_)
			state.RealPageLengths.push_back(page.size());
		return state;
	}

	bool QrCodeCapture::PollRealHardware(Emulator& emulator) {
		uint32_t contextAddress = 0;
		uint32_t dataAddress = 0;
		uint32_t pageAddress = 0;
		if (!RealQrContext(emulator, contextAddress, dataAddress, pageAddress)
			&& !RealQrDataAddress(emulator, dataAddress))
			return false;

		if (!IsRealQrActive(emulator)) {
			std::lock_guard lock(mutex_);
			real_seen_inactive_ = true;
			real_active_session_recorded_ = false;
			if (active_ || real_session_recorded_) {
				const auto now = std::chrono::steady_clock::now();
				if (!real_inactive_pending_) {
					real_inactive_pending_ = true;
					real_inactive_since_ = now;
					return false;
				}
				if (now - real_inactive_since_ < std::chrono::seconds(4))
					return false;
				real_inactive_pending_ = false;
				real_session_recorded_ = false;
				real_active_session_recorded_ = false;
			}
			if (!active_ && version_ == 0 && data_.empty())
				return false;
			active_ = false;
			complete_ = false;
			version_ = 0;
			data_.clear();
			pending_real_data_.clear();
			pending_real_page_ = 0;
			real_current_page_data_.clear();
			real_pages_.clear();
			real_last_page_ = 0;
			real_current_page_ = 0;
			real_total_pages_ = 0;
			++revision_;
			return true;
		}

		uint8_t page = 0;
		uint8_t totalPages = 0;
		std::string data;
		if (!FindRealQrContext(emulator, dataAddress, page, totalPages, data)) {
			FindRealQrPages(emulator, dataAddress, pageAddress, page, totalPages);
			if (!ReadRealQrData(emulator, dataAddress, data))
				return false;
		}
		if (data.empty())
			return false;

		std::lock_guard lock(mutex_);
		const bool wasActive = active_;
		const auto previousPage = real_current_page_;
		const auto previousTotalPages = real_total_pages_;
		real_inactive_pending_ = false;
		if (pending_real_page_ != page || pending_real_data_ != data) {
			pending_real_page_ = page;
			pending_real_data_ = std::move(data);
			return false;
		}
		pending_real_page_ = 0;
		pending_real_data_.clear();
		real_current_page_ = page;
		if (totalPages != 0)
			real_total_pages_ = totalPages;
		real_current_page_data_ = data;

		if (complete_ && real_session_recorded_) {
			const bool totalChanged = totalPages != 0 && previousTotalPages != 0 && totalPages != previousTotalPages;
			const bool currentPageBelongsToCompletedCode =
				totalPages == 1
					? data_ == data
					: page > 0 && page <= real_pages_.size() && real_pages_[page - 1] == data;
			if (totalChanged || !currentPageBelongsToCompletedCode) {
				real_pages_.clear();
				complete_ = false;
				data_.clear();
				real_total_pages_ = totalPages;
			}
			else {
				if (!wasActive || previousPage != real_current_page_) {
					++revision_;
					return true;
				}
				return false;
			}
		}

		if (page != 0) {
			if (page == 1
				&& previousPage != 1
				&& (complete_
					|| real_pages_.size() > 1
					|| (real_pages_.size() == 1 && !real_pages_[0].empty() && real_pages_[0] != data))) {
				real_pages_.clear();
				complete_ = false;
				data_.clear();
				real_session_recorded_ = false;
			}
			if (real_pages_.empty() && page != 1) {
				if (!active_ || previousPage != real_current_page_) {
					active_ = true;
					version_ = 11;
					++revision_;
					return true;
				}
				return false;
			}
			if (page > real_pages_.size() + 1)
				return false;
			for (size_t i = 0; i < real_pages_.size(); ++i) {
				if (i + 1 != page && !real_pages_[i].empty() && real_pages_[i] == data)
					return false;
			}
			if (real_pages_.size() < page)
				real_pages_.resize(page);
			real_pages_[page - 1] = data;
			real_last_page_ = std::max<uint8_t>(real_last_page_, page);
		}

		active_ = true;
		version_ = 11;

		const bool hasAllPages =
			real_total_pages_ > 0
			&& real_pages_.size() >= real_total_pages_
			&& std::all_of(real_pages_.begin(), real_pages_.begin() + real_total_pages_,
				[](const std::string& pageData) { return !pageData.empty(); });

		if (!hasAllPages) {
			if (!wasActive || previousPage != real_current_page_) {
				++revision_;
				return true;
			}
			return false;
		}

		std::string combined;
		for (size_t i = 0; i < real_total_pages_; ++i)
			combined += real_pages_[i];

		if (complete_ && data_ == combined)
			return false;

		data_ = std::move(combined);
		complete_ = true;
		if (!real_active_session_recorded_)
			history_.push_back({next_history_id_++, version_, data_});
		else if (!history_.empty())
			history_.back().Data = data_;
		++revision_;
		real_session_recorded_ = true;
		real_active_session_recorded_ = true;
		return true;
	}

	bool QrCodeCapture::SetCode(int version, std::string data, bool addHistory) {
		std::lock_guard lock(mutex_);
		if (active_ && version_ == version && data_ == data)
			return false;
		active_ = true;
		complete_ = true;
		version_ = version;
		data_ = std::move(data);
		if (addHistory)
			history_.push_back({next_history_id_++, version_, data_});
		++revision_;
		return true;
	}

	bool QrCodeCapture::Clear() {
		std::lock_guard lock(mutex_);
		if (!active_ && version_ == 0 && data_.empty())
			return false;
		active_ = false;
		complete_ = false;
		version_ = 0;
		data_.clear();
		++revision_;
		return true;
	}
}
