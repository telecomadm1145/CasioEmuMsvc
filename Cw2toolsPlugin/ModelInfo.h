#pragma once
#include <map>
#include <string>
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

		HW_MAX = 8,
	};
	struct SpriteInfo {
		Rect src, dest;
	};
	struct ColourInfo {
		int r, g, b;
	};
	struct ButtonInfo {
		Rect rect{};
		int kiko{};
		std::string keyname;
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
		void Write(std::ostream& os) const {
			Binary::Write(os, std::string("\n\nnx-U16/U8 Emulator Configuration file v51\n\n模拟器配置文件v51\n\ntệp cấu hình giả lập v51\n\n"));
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
		}
		void Read(std::istream& is) {
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
			if (hardware_id == HW_ES_PLUS) {
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
		}
	};
} // namespace casioemu