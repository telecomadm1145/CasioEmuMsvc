#include "QrCode.h"

#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Emulator.hpp"
#include "ModelInfo.h"
#include <algorithm>
#include <array>
#include <vector>

namespace {
	constexpr uint8_t kQrVersion11 = 0x05;
	constexpr uint8_t kQrExit = 0x06;
	constexpr uint8_t kQrVersion3 = 0x07;
	constexpr size_t kQrBufferCapacity = 0x1800;
	constexpr size_t kRealQrBufferCapacity = 0x300;
	constexpr size_t kRealQrEncodedBufferCapacity = 0x300;
	constexpr uint32_t kRealQrContextScanStart = 0xef00;
	constexpr uint32_t kRealQrContextScanEnd = 0xf000;

	class BitReader {
		const std::vector<uint8_t>& data_;
		size_t bit_pos_ = 0;

	public:
		explicit BitReader(const std::vector<uint8_t>& data) : data_(data) {}

		bool Read(size_t bitCount, uint32_t& value) {
			if (bitCount > 32 || bit_pos_ + bitCount > data_.size() * 8)
				return false;
			value = 0;
			for (size_t i = 0; i < bitCount; ++i) {
				const auto byte = data_[bit_pos_ / 8];
				const auto bit = (byte >> (7 - (bit_pos_ & 7))) & 1;
				value = (value << 1) | bit;
				++bit_pos_;
			}
			return true;
		}
	};

	struct DecodedQrPayload {
		std::string Data;
		bool HasStructuredAppend = false;
		uint8_t StructuredAppendPage = 0;
		uint8_t StructuredAppendTotal = 0;
		uint8_t StructuredAppendParity = 0;
	};

	bool DecodeQrPayloadSegments(
		const std::vector<uint8_t>& encodedData,
		int qrVersion,
		DecodedQrPayload& payload) {
		payload = {};
		BitReader reader(encodedData);
		constexpr std::array<char, 45> kAlphanumeric = {
			'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
			'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
			'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
			'U', 'V', 'W', 'X', 'Y', 'Z', ' ', '$', '%', '*',
			'+', '-', '.', '/', ':'};
		for (size_t segment = 0; segment < 16; ++segment) {
			uint32_t mode = 0;
			if (!reader.Read(4, mode))
				return false;
			if (mode == 0)
				return !payload.Data.empty();
			if (mode == 3) {
				uint32_t sequence = 0;
				uint32_t total = 0;
				uint32_t parity = 0;
				if (payload.HasStructuredAppend
					|| !reader.Read(4, sequence)
					|| !reader.Read(4, total)
					|| !reader.Read(8, parity)
					|| sequence > total)
					return false;
				payload.HasStructuredAppend = true;
				payload.StructuredAppendPage = static_cast<uint8_t>(sequence + 1);
				payload.StructuredAppendTotal = static_cast<uint8_t>(total + 1);
				payload.StructuredAppendParity = static_cast<uint8_t>(parity);
				continue;
			}

			size_t countBits = 0;
			switch (mode) {
			case 1:
				countBits = qrVersion <= 9 ? 10 : qrVersion <= 26 ? 12 : 14;
				break;
			case 2:
				countBits = qrVersion <= 9 ? 9 : qrVersion <= 26 ? 11 : 13;
				break;
			case 4:
				countBits = qrVersion <= 9 ? 8 : 16;
				break;
			default:
				return false;
			}

			uint32_t count = 0;
			if (!reader.Read(countBits, count))
				return false;
			if (count > kRealQrBufferCapacity)
				return false;

			if (mode == 1) {
				while (count >= 3) {
					uint32_t value = 0;
					if (!reader.Read(10, value) || value > 999)
						return false;
					payload.Data.push_back(static_cast<char>('0' + value / 100));
					payload.Data.push_back(static_cast<char>('0' + (value / 10) % 10));
					payload.Data.push_back(static_cast<char>('0' + value % 10));
					count -= 3;
				}
				if (count == 2) {
					uint32_t value = 0;
					if (!reader.Read(7, value) || value > 99)
						return false;
					payload.Data.push_back(static_cast<char>('0' + value / 10));
					payload.Data.push_back(static_cast<char>('0' + value % 10));
				}
				else if (count == 1) {
					uint32_t value = 0;
					if (!reader.Read(4, value) || value > 9)
						return false;
					payload.Data.push_back(static_cast<char>('0' + value));
				}
			}
			else if (mode == 2) {
				while (count >= 2) {
					uint32_t value = 0;
					if (!reader.Read(11, value) || value >= 45 * 45)
						return false;
					payload.Data.push_back(kAlphanumeric[value / 45]);
					payload.Data.push_back(kAlphanumeric[value % 45]);
					count -= 2;
				}
				if (count == 1) {
					uint32_t value = 0;
					if (!reader.Read(6, value) || value >= 45)
						return false;
					payload.Data.push_back(kAlphanumeric[value]);
				}
			}
			else if (mode == 4) {
				for (uint32_t i = 0; i < count; ++i) {
					uint32_t value = 0;
					if (!reader.Read(8, value) || value < 0x20 || value > 0x7e)
						return false;
					payload.Data.push_back(static_cast<char>(value));
				}
			}

			if (payload.Data.size() > kRealQrBufferCapacity)
				return false;
		}
		return false;
	}

	bool ReadRealQrEncodedPayload(
		casioemu::Emulator& emulator,
		uint32_t encodedDataAddress,
		int qrVersion,
		uint8_t currentPage,
		uint8_t totalPages,
		std::string& data) {
		if (encodedDataAddress < 0xe900 || encodedDataAddress >= 0xef00)
			return false;
		std::vector<uint8_t> encodedData;
		encodedData.reserve(kRealQrEncodedBufferCapacity);
		for (size_t i = 0; i < kRealQrEncodedBufferCapacity; ++i) {
			const auto address = encodedDataAddress + static_cast<uint32_t>(i);
			if (address >= 0xef00)
				break;
			encodedData.push_back(emulator.chipset.mmu.ReadData(address, false));
		}
		DecodedQrPayload payload;
		if (!DecodeQrPayloadSegments(encodedData, qrVersion, payload))
			return false;
		if (totalPages > 1) {
			if (!payload.HasStructuredAppend
				|| payload.StructuredAppendPage != currentPage
				|| payload.StructuredAppendTotal != totalPages)
				return false;
		}
		else if (payload.HasStructuredAppend
			&& (payload.StructuredAppendPage != 1 || payload.StructuredAppendTotal != 1))
			return false;
		data = std::move(payload.Data);
		return true;
	}

	bool ReadStrideEncodedPayload(
		casioemu::Emulator& emulator,
		uint32_t encodedDataAddress,
		int qrVersion,
		uint8_t currentPage,
		uint8_t totalPages,
		std::string& data) {
		constexpr uint32_t kStride = 5;
		if (encodedDataAddress < 0xea00 || encodedDataAddress >= 0xef00)
			return false;
		std::vector<uint8_t> encodedData;
		encodedData.reserve(kRealQrEncodedBufferCapacity);
		for (size_t i = 0; i < kRealQrEncodedBufferCapacity; ++i) {
			const auto address = encodedDataAddress + static_cast<uint32_t>(i) * kStride;
			if (address >= 0xef00)
				break;
			encodedData.push_back(emulator.chipset.mmu.ReadData(address, false));
		}
		DecodedQrPayload payload;
		if (!DecodeQrPayloadSegments(encodedData, qrVersion, payload))
			return false;
		if (totalPages > 1) {
			if (!payload.HasStructuredAppend
				|| payload.StructuredAppendPage != currentPage
				|| payload.StructuredAppendTotal != totalPages)
				return false;
		}
		else if (payload.HasStructuredAppend
			&& (payload.StructuredAppendPage != 1 || payload.StructuredAppendTotal != 1))
			return false;
		data = std::move(payload.Data);
		return true;
	}

	bool DataMatchesSourceBuffer(
		casioemu::Emulator& emulator,
		uint32_t dataAddress,
		const std::string& data) {
		if (data.empty() || dataAddress < 0xed00 || dataAddress + data.size() > 0xef00)
			return false;
		for (size_t i = 0; i < data.size(); ++i) {
			if (emulator.chipset.mmu.ReadData(dataAddress + static_cast<uint32_t>(i), false)
				!= static_cast<uint8_t>(data[i]))
				return false;
		}
		return true;
	}

	bool FindContinuousPayloadInRange(
		casioemu::Emulator& emulator,
		uint32_t scanStart,
		uint32_t scanEnd,
		uint32_t dataAddress,
		int qrVersion,
		uint8_t currentPage,
		uint8_t totalPages,
		std::string& data) {
		bool found = false;
		std::string foundData;
		scanStart = std::max<uint32_t>(scanStart, 0xe900);
		scanEnd = std::min<uint32_t>(scanEnd, 0xef00);
		for (uint32_t address = scanStart; address < scanEnd; ++address) {
			std::string candidateData;
			if (!ReadRealQrEncodedPayload(emulator, address, qrVersion, currentPage, totalPages, candidateData))
				continue;
			if (!DataMatchesSourceBuffer(emulator, dataAddress, candidateData))
				continue;
			if (found && foundData != candidateData)
				return false;
			found = true;
			foundData = std::move(candidateData);
		}
		if (!found)
			return false;
		data = std::move(foundData);
		return true;
	}

	bool FindRealQrEncodedPayload(
		casioemu::Emulator& emulator,
		uint32_t encodedDataAddress,
		uint32_t dataAddress,
		int qrVersion,
		uint8_t currentPage,
		uint8_t totalPages,
		std::string& data) {
		if (ReadRealQrEncodedPayload(emulator, encodedDataAddress, qrVersion, currentPage, totalPages, data)) {
			if (totalPages != 1 || DataMatchesSourceBuffer(emulator, dataAddress, data))
				return true;
		}

		if (FindContinuousPayloadInRange(
				emulator,
				encodedDataAddress >= 0x300 ? encodedDataAddress - 0x300 : 0,
				encodedDataAddress + 0x20,
				dataAddress,
				qrVersion,
				currentPage,
				totalPages,
				data))
			return true;

		if (!ReadStrideEncodedPayload(emulator, encodedDataAddress, qrVersion, currentPage, totalPages, data))
			return false;
		return totalPages != 1 || DataMatchesSourceBuffer(emulator, dataAddress, data);
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

		for (uint32_t address = kRealQrContextScanStart; address + 0x12 <= kRealQrContextScanEnd; ++address) {
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
		int& qrVersion,
		std::string& data) {
		for (uint32_t address = kRealQrContextScanStart; address + 0x12 <= kRealQrContextScanEnd; ++address) {
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
			const auto candidateQrVersion = emulator.chipset.mmu.ReadData(address + 0x0e, false);
			if (candidateQrVersion < 1 || candidateQrVersion > 40)
				continue;
			const uint32_t encodedDataAddress =
				emulator.chipset.mmu.ReadData(address + 0x10, false)
				| (emulator.chipset.mmu.ReadData(address + 0x11, false) << 8);
			if (!FindRealQrEncodedPayload(emulator, encodedDataAddress, candidateDataAddress, candidateQrVersion, page, total, candidateData))
				continue;

			dataAddress = candidateDataAddress;
			currentPage = page;
			totalPages = total;
			qrVersion = candidateQrVersion;
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
				return state == 1 || state == 2 || state == 3;
			}
		case casioemu::HW_CLASSWIZ_II:
			{
				const auto state = emulator.chipset.mmu.ReadData(0x91a3, false);
				return state >= 1 && state <= 4;
			}
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

		auto markInactive = [this]() {
			std::lock_guard lock(mutex_);
			real_seen_inactive_ = true;
			if (active_ || real_session_recorded_) {
				const auto now = std::chrono::steady_clock::now();
				if (!real_inactive_pending_) {
					real_inactive_pending_ = true;
					real_inactive_since_ = now;
					return false;
				}
				if (now - real_inactive_since_ < std::chrono::seconds(2))
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
		};

		if (!IsRealQrActive(emulator))
			return markInactive();

		uint8_t page = 0;
		uint8_t totalPages = 0;
		int qrVersion = 11;
		std::string data;
		if (!FindRealQrContext(emulator, dataAddress, page, totalPages, qrVersion, data))
			return markInactive();
		if (page == 0 || totalPages == 0)
			return markInactive();
		if (data.empty())
			return markInactive();

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
					version_ = qrVersion;
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
		version_ = qrVersion;

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
