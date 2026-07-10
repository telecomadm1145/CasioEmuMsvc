#include "OnlineModelPackage.h"

#include "ModelConfig.h"
#include "ModelInfo.h"
#include "ModelResourceStore.h"
#include "miniz.h"
#include "miniz_tinfl.h"

#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

namespace casioemu {
	namespace {
		constexpr std::uint64_t kMaxArchiveSize = 32ull * 1024 * 1024;
		constexpr std::uint64_t kMaxFileSize = 16ull * 1024 * 1024;
		constexpr std::uint64_t kMaxExtractedSize = 32ull * 1024 * 1024;

		bool SafeModelId(const std::string& value) {
			return !value.empty() && value.size() <= 80 && value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") == std::string::npos;
		}

		bool AllowedName(const std::string& name) {
			return name == "config.json" || name == "board.svg" || name == "face.svg" || name == "face.png" || name == "rom.bin";
		}

		std::uint16_t Read16(const std::vector<std::uint8_t>& data, size_t offset) {
			if (offset + 2 > data.size()) throw std::runtime_error("Truncated ZIP archive.");
			return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
		}

		std::uint32_t Read32(const std::vector<std::uint8_t>& data, size_t offset) {
			if (offset + 4 > data.size()) throw std::runtime_error("Truncated ZIP archive.");
			return static_cast<std::uint32_t>(data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24));
		}

		struct ZipEntry {
			std::string name;
			std::uint16_t method{};
			std::uint32_t crc{};
			std::uint32_t compressed_size{};
			std::uint32_t uncompressed_size{};
			std::uint32_t local_offset{};
		};

		std::vector<ZipEntry> ReadZipEntries(const std::vector<std::uint8_t>& archive) {
			if (archive.size() < 22) throw std::runtime_error("Truncated ZIP archive.");
			const size_t search_start = archive.size() > 0x10000 + 22 ? archive.size() - 0x10000 - 22 : 0;
			size_t eocd = std::string::npos;
			for (size_t offset = archive.size() - 22;; --offset) {
				if (Read32(archive, offset) == 0x06054b50) { eocd = offset; break; }
				if (offset == search_start) break;
			}
			if (eocd == std::string::npos) throw std::runtime_error("ZIP end record was not found.");
			const auto entry_count = Read16(archive, eocd + 10);
			const auto central_size = Read32(archive, eocd + 12);
			const auto central_offset = Read32(archive, eocd + 16);
			if (entry_count == 0xffff || central_offset + central_size > archive.size()) throw std::runtime_error("ZIP64 or invalid ZIP archives are not supported.");

			std::vector<ZipEntry> result;
			size_t offset = central_offset;
			for (std::uint16_t index = 0; index < entry_count; ++index) {
				if (Read32(archive, offset) != 0x02014b50) throw std::runtime_error("Invalid ZIP central directory.");
				const auto flags = Read16(archive, offset + 8);
				const auto name_length = Read16(archive, offset + 28);
				const auto extra_length = Read16(archive, offset + 30);
				const auto comment_length = Read16(archive, offset + 32);
				if (flags & 1 || offset + 46 + name_length + extra_length + comment_length > archive.size())
					throw std::runtime_error("Encrypted or truncated ZIP entries are not supported.");
				ZipEntry entry;
				entry.method = Read16(archive, offset + 10);
				entry.crc = Read32(archive, offset + 16);
				entry.compressed_size = Read32(archive, offset + 20);
				entry.uncompressed_size = Read32(archive, offset + 24);
				entry.local_offset = Read32(archive, offset + 42);
				entry.name.assign(reinterpret_cast<const char*>(archive.data() + offset + 46), name_length);
				result.push_back(std::move(entry));
				offset += 46 + name_length + extra_length + comment_length;
			}
			return result;
		}

		std::vector<std::uint8_t> ExtractZipEntry(const std::vector<std::uint8_t>& archive, const ZipEntry& entry) {
			const size_t local = entry.local_offset;
			if (Read32(archive, local) != 0x04034b50) throw std::runtime_error("Invalid ZIP local header.");
			const auto name_length = Read16(archive, local + 26);
			const auto extra_length = Read16(archive, local + 28);
			const size_t data_offset = local + 30 + name_length + extra_length;
			if (data_offset + entry.compressed_size > archive.size()) throw std::runtime_error("Truncated ZIP entry data.");
			std::vector<std::uint8_t> output;
			if (entry.method == 0) {
				output.assign(archive.begin() + data_offset, archive.begin() + data_offset + entry.compressed_size);
			}
			else if (entry.method == 8) {
				size_t output_size = 0;
				void* buffer = tinfl_decompress_mem_to_heap(archive.data() + data_offset, entry.compressed_size, &output_size, 0);
				if (!buffer) throw std::runtime_error("Failed to inflate ZIP entry.");
				output.assign(static_cast<std::uint8_t*>(buffer), static_cast<std::uint8_t*>(buffer) + output_size);
				mz_free(buffer);
			}
			else throw std::runtime_error("Unsupported ZIP compression method.");
			if (output.size() != entry.uncompressed_size || mz_crc32(MZ_CRC32_INIT, output.data(), output.size()) != entry.crc)
				throw std::runtime_error("ZIP entry size or checksum mismatch.");
			return output;
		}
	}

	std::shared_ptr<MemoryModelResourceStore> LoadOnlineModelPackage(const std::vector<std::uint8_t>& archive, const std::string& model_id) {
		if (archive.empty() || archive.size() > kMaxArchiveSize) throw std::runtime_error("Online model archive has an invalid size.");
		if (!SafeModelId(model_id)) throw std::runtime_error("Online model id is invalid.");

		std::set<std::string> names;
		std::map<std::string, std::vector<std::uint8_t>> files;
		std::uint64_t total_size = 0;
		for (const auto& entry : ReadZipEntries(archive)) {
			const std::string& name = entry.name;
			if (!AllowedName(name) || !names.insert(name).second)
				throw std::runtime_error("Online model ZIP contains an invalid or duplicate entry.");
			if (entry.uncompressed_size > kMaxFileSize || (total_size += entry.uncompressed_size) > kMaxExtractedSize)
				throw std::runtime_error("Online model ZIP is too large after extraction.");
			files.emplace(name, ExtractZipEntry(archive, entry));
		}
		if (!names.contains("config.json") || !names.contains("board.svg") || !names.contains("rom.bin") ||
			(names.contains("face.svg") == names.contains("face.png")))
			throw std::runtime_error("Online model ZIP does not contain the required files.");

		auto resources = std::make_shared<MemoryModelResourceStore>(std::move(files));
		ModelInfo model{};
		std::string error;
		if (!LoadModelInfoFromResourceStore(*resources, model, &error))
			throw std::runtime_error("Invalid online model configuration: " + error);
		if (model.board_path != "board.svg" || model.rom_path != "rom.bin" ||
			(model.interface_path != "face.svg" && model.interface_path != "face.png") || !model.flash_path.empty())
			throw std::runtime_error("Online model configuration references unsupported paths.");
		return resources;
	}
}
