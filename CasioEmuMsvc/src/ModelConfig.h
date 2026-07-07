#pragma once

#include "ModelInfo.h"

#include <filesystem>
#include <iosfwd>
#include <string>

namespace casioemu {
	constexpr const char* MODEL_CONFIG_JSON = "config.json";
	constexpr const char* MODEL_CONFIG_BIN = "config.bin";

	void WriteModelInfoJson(std::ostream& os, const ModelInfo& model);
	void ReadModelInfoJson(std::istream& is, ModelInfo& model);
	bool LoadModelInfoFromFolder(
		const std::filesystem::path& model_path,
		ModelInfo& model,
		std::string* loaded_from = nullptr,
		std::string* error = nullptr);
	void SaveModelInfoJson(const std::filesystem::path& model_path, const ModelInfo& model);
}
