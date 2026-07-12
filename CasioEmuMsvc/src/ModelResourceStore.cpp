#include "ModelResourceStore.h"

#include <stdexcept>
#include <utility>

namespace casioemu {
	MemoryModelResourceStore::MemoryModelResourceStore(std::map<std::string, std::vector<std::uint8_t>> resources)
		: resources_(std::move(resources)) {}

	bool MemoryModelResourceStore::Exists(std::string_view name) const {
		const std::lock_guard lock(mutex_);
		const std::string key(name);
		return session_.contains(key) || resources_.contains(key);
	}

	std::vector<std::uint8_t> MemoryModelResourceStore::Read(std::string_view name) const {
		const std::lock_guard lock(mutex_);
		const std::string key(name);
		if (const auto session = session_.find(key); session != session_.end()) return session->second;
		if (const auto resource = resources_.find(key); resource != resources_.end()) return resource->second;
		throw std::runtime_error("Model resource was not found: " + key);
	}

	void MemoryModelResourceStore::WriteSession(std::string_view name, const std::vector<std::uint8_t>& data) {
		const std::lock_guard lock(mutex_);
		session_[std::string(name)] = data;
	}
}
