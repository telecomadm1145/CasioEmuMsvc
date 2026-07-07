#pragma once

#include "ModelInfo.h"

#include <filesystem>
#include <map>
#include <string>

namespace casioemu {
	ModelInfo LoadSvgBoardModelInfo(
		const std::filesystem::path& model_path,
		unsigned short hardware_id,
		const std::string& requested_board,
		const std::string& requested_interface,
		const std::string& requested_rom,
		const std::string& requested_flash,
		const Rect& interface_src_rect,
		int display_w,
		int display_h,
		double screen_scale_y,
		const std::map<std::string, int>& configured_status_sprites);
}
