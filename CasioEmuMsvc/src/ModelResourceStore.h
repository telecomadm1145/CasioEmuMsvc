#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace casioemu {
	class ModelResourceStore {
	public:
		virtual ~ModelResourceStore() = default;
		virtual bool Exists(std::string_view name) const = 0;
		virtual std::vector<std::uint8_t> Read(std::string_view name) const = 0;
		virtual void WriteSession(std::string_view name, const std::vector<std::uint8_t>& data) = 0;
	};

	class MemoryModelResourceStore final : public ModelResourceStore {
	public:
		explicit MemoryModelResourceStore(std::map<std::string, std::vector<std::uint8_t>> resources);
		bool Exists(std::string_view name) const override;
		std::vector<std::uint8_t> Read(std::string_view name) const override;
		void WriteSession(std::string_view name, const std::vector<std::uint8_t>& data) override;

	private:
		mutable std::mutex mutex_;
		std::map<std::string, std::vector<std::uint8_t>> resources_;
		std::map<std::string, std::vector<std::uint8_t>> session_;
	};
}
