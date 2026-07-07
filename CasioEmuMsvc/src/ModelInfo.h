#pragma once
#include <filesystem>
#include <map>
#include <iosfwd>
#include <istream>
#include <string>
#include <utility>
#include <vector>

#include "Binary.h"
struct Rect {
	int x, y;
	int w, h;
#ifdef SDL_rect_h_
	constexpr operator SDL_Rect() const {
		return {x, y, w, h};
	}
#endif
};
namespace casioemu {
	enum HardwareId {
		HW_MIN = 3,
		HW_ES_PLUS = 3,
		HW_CLASSWIZ = 4,
		HW_CLASSWIZ_II = 5,
		HW_FX_5800P = 6,
		HW_TI = 7,
		HW_SOLARII = 8,
		// our test ends here, so we can add new models without worrying about breaking old configs
		HW_MAX = HW_SOLARII,
		HW_EPS6800 = 9,
	};
	struct SpriteInfo {
		Rect src, dest;
		std::string svg_shape;
		std::string svg_defs;
		void Write(std::ostream& stm) const {
			Binary::Write(stm, src);
			Binary::Write(stm, dest);
		}
		void Read(std::istream& stm) {
			Binary::Read(stm, src);
			Binary::Read(stm, dest);
			svg_shape.clear();
			svg_defs.clear();
		}
	};
	struct ColourInfo {
		int r, g, b;
	};
	struct ButtonInfo {
		Rect rect{};
		int kiko{};
		std::string keyname;
		std::string svg_shape;
		std::string svg_defs;
		void Write(std::ostream& stm) const {
			Binary::Write(stm, rect);
			Binary::Write(stm, kiko);
			Binary::Write(stm, keyname);
		}
		void Read(std::istream& stm) {
			Binary::Read(stm, rect);
			Binary::Read(stm, kiko);
			Binary::Read(stm, keyname);
		}
	};
	struct ModelInfo {
		std::vector<ButtonInfo> buttons;
		std::map<std::string, SpriteInfo> sprites;
		ColourInfo ink_color{};
		std::string interface_path;
		std::string model_name;
		std::string rom_path;
		std::string flash_path;
		unsigned short csr_mask{};
		unsigned short hardware_id{};
		bool real_hardware{};
		unsigned char pd_value{};
		bool enable_new_screen{};
		bool is_sample_rom{};
		bool legacy_ko{};
		bool u16_mode{};
		bool LARGE_model{};
		// ML620 style mirroring(1->8) or ML610 style mirroring(1->4)
		bool ml620_mirroring{};
		std::string board_path;
		int screen_width{};
		int screen_height{};
		double screen_scale_y{};
		std::map<std::string, int> status_sprites;
		std::map<std::string, std::string> extra;
		void Write(std::ostream& os) const {
			Binary::Write(os, std::string("\n\nnx-U16/U8 Emulator Configuration file v52\n\n模拟器配置文件v52\n\ntệp cấu hình giả lập v52\n\n"));
			Binary::Write(os, csr_mask);
			Binary::Write(os, hardware_id);
			Binary::Write(os, real_hardware);
			Binary::Write(os, pd_value);
			Binary::Write(os, buttons);
			Binary::Write(os, sprites);
			Binary::Write(os, ink_color);
			Binary::Write(os, interface_path);
			Binary::Write(os, model_name);
			Binary::Write(os, rom_path);
			Binary::Write(os, flash_path);
			Binary::Write(os, enable_new_screen);
			Binary::Write(os, is_sample_rom);
			Binary::Write(os, legacy_ko);
			Binary::Write(os, u16_mode);
			Binary::Write(os, LARGE_model);
			Binary::Write(os, ml620_mirroring);
			Binary::Write(os, extra);
			Binary::Write(os, status_sprites);

			std::vector<std::string> button_svg_shapes;
			std::vector<std::string> button_svg_defs;
			button_svg_shapes.reserve(buttons.size());
			button_svg_defs.reserve(buttons.size());
			for (const auto& button : buttons) {
				button_svg_shapes.push_back(button.svg_shape);
				button_svg_defs.push_back(button.svg_defs);
			}
			Binary::Write(os, button_svg_shapes);
			Binary::Write(os, button_svg_defs);

			std::map<std::string, std::string> sprite_svg_shapes;
			std::map<std::string, std::string> sprite_svg_defs;
			for (const auto& [name, sprite] : sprites) {
				if (!sprite.svg_shape.empty())
					sprite_svg_shapes[name] = sprite.svg_shape;
				if (!sprite.svg_defs.empty())
					sprite_svg_defs[name] = sprite.svg_defs;
			}
			Binary::Write(os, sprite_svg_shapes);
			Binary::Write(os, sprite_svg_defs);
			Binary::Write(os, board_path);
			Binary::Write(os, screen_width);
			Binary::Write(os, screen_height);
			Binary::Write(os, screen_scale_y);
		}
		void Read(std::istream& is) {
			status_sprites.clear();
			board_path.clear();
			screen_width = 0;
			screen_height = 0;
			screen_scale_y = 0;
			{
				std::string unused;
				Binary::Read(is, unused);
			}
			Binary::Read(is, csr_mask);
			Binary::Read(is, hardware_id);
			Binary::Read(is, real_hardware);
			Binary::Read(is, pd_value);
			Binary::Read(is, buttons);
			Binary::Read(is, sprites);
			Binary::Read(is, ink_color);
			Binary::Read(is, interface_path);
			Binary::Read(is, model_name);
			Binary::Read(is, rom_path);
			Binary::Read(is, flash_path);
			Binary::Read(is, enable_new_screen);
			Binary::Read(is, is_sample_rom);
			Binary::Read(is, legacy_ko);
			// set default value if loaded a old config
			if (hardware_id == HW_ES_PLUS || hardware_id == HW_SOLARII) {
				u16_mode = false;
			}
			else {
				u16_mode = true;
			}
			if (hardware_id == HW_SOLARII) {
				LARGE_model = false;
			}
			else {
				LARGE_model = true;
			}
			if (hardware_id == HW_CLASSWIZ) {
				ml620_mirroring = false;
			}
			else {
				ml620_mirroring = true;
			}
			Binary::Read(is, u16_mode);
			Binary::Read(is, LARGE_model);
			Binary::Read(is, ml620_mirroring);
			Binary::Read(is, extra);
			if (is.peek() != std::char_traits<char>::eof()) {
				Binary::Read(is, status_sprites);

				std::vector<std::string> button_svg_shapes;
				std::vector<std::string> button_svg_defs;
				Binary::Read(is, button_svg_shapes);
				Binary::Read(is, button_svg_defs);
				for (size_t ix = 0; ix < buttons.size(); ++ix) {
					if (ix < button_svg_shapes.size())
						buttons[ix].svg_shape = std::move(button_svg_shapes[ix]);
					if (ix < button_svg_defs.size())
						buttons[ix].svg_defs = std::move(button_svg_defs[ix]);
				}

				std::map<std::string, std::string> sprite_svg_shapes;
				std::map<std::string, std::string> sprite_svg_defs;
				Binary::Read(is, sprite_svg_shapes);
				Binary::Read(is, sprite_svg_defs);
				for (auto& [name, shape] : sprite_svg_shapes)
					sprites[name].svg_shape = std::move(shape);
				for (auto& [name, defs] : sprite_svg_defs)
					sprites[name].svg_defs = std::move(defs);

				if (is.peek() != std::char_traits<char>::eof()) {
					Binary::Read(is, board_path);
					Binary::Read(is, screen_width);
					Binary::Read(is, screen_height);
					Binary::Read(is, screen_scale_y);
				}
			}
		}
	};
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
} // namespace casioemu
