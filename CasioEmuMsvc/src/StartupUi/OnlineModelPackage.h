#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ModelResourceStore.h"

namespace casioemu {
	std::shared_ptr<MemoryModelResourceStore> LoadOnlineModelPackage(
		const std::vector<std::uint8_t>& archive,
		const std::string& model_id);
}
