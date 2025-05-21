/*

		Screen peripheral implement.
		Copyright (C) 2024 telecomadm1145/Xyzst/user202729/LBPHacker/hieuxyz

		This program is free software: you can redistribute it and/or modify
		it under the terms of the GNU General Public License as published by
		the Free Software Foundation, either version 3 of the License, or
		(at your option) any later version.

		This program is distributed in the hope that it will be useful,
		but WITHOUT ANY WARRANTY; without even the implied warranty of
		MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
		GNU General Public License for more details.

		You should have received a copy of the GNU General Public License
		along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#include "Screen.hpp"
#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Chipset/MMURegion.hpp"
#include "Emulator.hpp"
#include "Gui/HwController.h"
#include "Logger.hpp"
#include "ML620Ports.h"
#include "ModelInfo.h"
#include "Models.h"
#include "PopUpDisplay.h"
#include "Ui.hpp"
#include <algorithm> // for std::generate
#include <array>
#include <cstdlib> // for std::rand
#include <ctime>   // for std::time
#include <iomanip>
#include <vector>

#ifdef __ANDROID__
#include <jni.h>
#include <android/log.h>
#include <android/api-level.h>

bool saveImageToMediaStore(const void* pixels, int width, int height, int pitch, const char* filename) {
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    
    // Create a Java direct ByteBuffer from the pixel data
    jobject byteBuffer = env->NewDirectByteBuffer((void*)pixels, height * pitch);
    
    // Call the Java method to handle saving to MediaStore
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID saveImageMethod = env->GetMethodID(activityClass, "saveImageToMediaStore", 
        "(Ljava/nio/ByteBuffer;IIILjava/lang/String;)Z");
    
    // If the method doesn't exist, we need to add it to the Java side
    if (saveImageMethod == NULL) {
        SDL_Log("Error: saveImageToMediaStore method not found. Please add it to your Java activity.");
        env->DeleteLocalRef(byteBuffer);
        env->DeleteLocalRef(activityClass);
        env->DeleteLocalRef(activity);
        return false;
    }
    
    jstring jfilename = env->NewStringUTF(filename);
    jboolean result = env->CallBooleanMethod(activity, saveImageMethod, byteBuffer, width, height, pitch, jfilename);
    
    env->DeleteLocalRef(jfilename);
    env->DeleteLocalRef(byteBuffer);
    env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(activity);
    
    return result;
}
#endif

inline constexpr uint8_t reverse_bits(uint8_t n) {
	uint8_t reversed = 0;
	for (int i = 0; i < 8; ++i) {
		reversed |= ((n >> i) & 1) << (7 - i);
	}
	return reversed;
}

// constexpr 生成查找表
inline constexpr std::array<uint8_t, 256> generate_lookup_table() {
	std::array<uint8_t, 256> table = {};
	for (int i = 0; i < 256; ++i) {
		table[i] = reverse_bits(static_cast<uint8_t>(i));
	}
	return table;
}

// 定义查找表
constexpr auto bit_lookup_table = generate_lookup_table();

inline void fillRandomData(unsigned char* buf, size_t size) {
	std::srand(static_cast<unsigned int>(SDL_GetPerformanceCounter())); // 使用当前时间作为随机种子
	std::generate(buf, buf + size, []() {
		return static_cast<unsigned char>(std::rand() % 256); // 生成0到255之间的随机数
	});
}

#pragma warning(disable : 4244)

namespace casioemu {
	struct SpriteBitmap {
		const char* name;
		uint8_t mask, offset;
	};
	inline int update_screen_scan_alpha(float* screen_scan_alpha, Uint64 t, int screen_refresh_rate) {
		int n = (static_cast<Uint64>((t * screen_refresh_rate) / 250)) % 64;

		if (screen_refresh_rate < screen_flashing_threshold) {
			for (size_t i = 0; i < 64; i++) {
				screen_scan_alpha[i] = 1.0f;
			}
			return n;
		}

		// 计算归一化所需的归一化因子
		float normalization_factor = 0.0f;
		std::vector<float> exp_values(64);

		for (size_t i = 0; i < 64; i++) {
			exp_values[i] = std::exp(-screen_flashing_brightness_coeff * i / 64.0f);
			normalization_factor += exp_values[i];
		}

		// 归一化
		for (size_t i = 0; i < 64; i++) {
			screen_scan_alpha[(i + n) % 64] = std::pow(exp_values[i] / normalization_factor * 80., 0.2);
		}

		return n;
	}
	template <HardwareId hardware_id>
	class Screen : public Peripheral {
		static int const N_ROW,
			ROW_SIZE,
			OFFSET,
			ROW_SIZE_DISP,
			SPR_MAX;

		MMURegion region_buffer{}, region_buffer1{}, region_contrast{}, region_brightness{}, region_scan_report_op1{}, region_mode{}, region_range{}, region_select{}, region_offset{}, region_refresh_rate{}, region_scan_report{};
		uint8_t *screen_buffer{}, *screen_buffer1{}, screen_contrast{}, screen_brightness{}, screen_scan_report_op1{}, screen_mode{}, screen_range{}, screen_select{}, screen_offset{}, screen_refresh_rate{}, screen_scan_report{};

		MMURegion region_power{}, region_scan_report_en{};
		uint8_t screen_power{}, screen_scan_report_en{};

		MMURegion region_unk1{}, region_unk2{};

		uint8_t unk_f034{};

		// TI things

		MMURegion ti_port7_data{}, ti_port5_data{};
		int ti_contrast{}, ti_port_status{};
		bool ti_enabled = 0;
		bool ti_a0 = 0;
		bool ti_rw = 0;
		int ti_col = 0;
		int ti_page = 0;

		int ti_port7{};
		int ti_port5{};

		float screen_scan_alpha[64]{};
		float position = 0;
		SDL_Renderer* renderer{};
		SDL_Texture* interface_texture{};
		float screen_ink_alpha[66 * 192]{};
		static const SpriteBitmap sprite_bitmap[];
		std::vector<SpriteInfo> sprite_info;
		ColourInfo ink_colour{};

		bool inited = 0;
		bool enabled_2 = 0;

	public:
		Screen(Emulator& emu)
			: Peripheral(emu) {
			std::thread thd([&]() {
				while (1) {
					tick();
#ifdef __ANDROID__
					SDL_Delay(10);
#endif
				}
			});
			thd.detach();
		}
		~Screen() {
			if (screen_buffer)
				delete[] screen_buffer;
			if (screen_buffer1)
				delete[] screen_buffer1;
		}
		void Initialise() override;
		void Uninitialise() override;
		void Frame() override;
		void Reset() override;
		void tick() {
			float ratio = 0;
			if constexpr (hardware_id == HW_ES_PLUS)
				ratio = 1 - 1e-4;
			else
				ratio = 1 - 5e-4;

			if constexpr (hardware_id == HW_TI) {
				ratio = 1 - 1e-4;
				if (!ti_enabled) {
					for (size_t i = 0; i < 65 * 192; i++) {
						screen_ink_alpha[i] *= ratio;
					}
					return;
				}
				if (!n_ram_buffer) //  || !emulator.chipset.ti_status_buf) //  || !emulator.chipset.ti_screen_buf
					return;
				float ink_alpha_on = (ti_contrast - 100) * 20.0;
				float ink_alpha_off = std::clamp(ink_alpha_on * 0.1, 0.0, 255.0);
				ink_alpha_on = std::clamp(ink_alpha_on, 0.0f, 255.0f);
				uint8_t* screen_buffer = (uint8_t*)n_ram_buffer - casioemu::GetRamBaseAddr(hardware_id) + 0xE708;
				if (emulator.ModelDefinition.real_hardware) {
					screen_buffer = this->screen_buffer;
				}
				for (int ix = 0; ix < 192; ++ix) {
					for (int iy = 0; iy < 64; ++iy) {
						uint32_t i = (ix << 6) | iy;
						int bIndx = (i >> 3);
						int subIndx = (i & 7);
						int mask = (1 << subIndx);
						bool on = (screen_buffer[bIndx] & mask) != 0;
						auto& data = screen_ink_alpha[(iy * 192 + 192) + ix];
						data = data * ratio + (on ? ink_alpha_on : ink_alpha_off) * (1 - ratio);
					}
				}
				screen_buffer = (uint8_t*)n_ram_buffer - casioemu::GetRamBaseAddr(hardware_id) + 0xe5d4;
				if (emulator.ModelDefinition.real_hardware) {
					screen_buffer = this->screen_buffer + 8 * 192;
				}
				int x = 0;
				for (int ix = 1; ix != SPR_MAX; ++ix) {
					auto off = sprite_bitmap[ix].offset;
					auto& data = screen_ink_alpha[x];
					data = data * ratio + ((screen_buffer[off] & sprite_bitmap[ix].mask) ? ink_alpha_on : ink_alpha_off) * (1 - ratio);
					x++;
				}

				return;
			}

#ifdef __ANDROID__
			ratio = 0.80;
#endif

			if (screen_refresh_rate < screen_flashing_threshold && !enable_screen_fading)
				;
			else {
				int n = update_screen_scan_alpha(screen_scan_alpha, SDL_GetTicks64(), screen_refresh_rate);
				screen_scan_report = ((n / (screen_scan_report_en ? screen_scan_report_op1 : 64)) % 2 ? 3 : 0) ^ (n % 64 == 0 ? 1 : (n % 64 == 32 ? 2 : 0));
			}
			if (screen_refresh_rate < 6) {
				screen_refresh_rate = 6;
			}
			auto sb = screen_brightness;
			if (sb < 3) {
				sb = 3;
			}
			auto contrast = (int)screen_contrast;
			// if (screen_contrast2_en) {
			//        contrast += screen_contrast2 * 0.5;
			// }
			if (contrast < 0) {
				contrast = 0;
			}
			auto coeff = 16;
			auto off = 0;
			if constexpr (hardware_id != HW_CLASSWIZ_II) {
				coeff = 28;
				off = -240;
			}
			int ink_alpha_on = off + contrast * coeff - sb * 8;
			int ink_alpha_off = off + 20 + (contrast) * (coeff - 11) - sb * 13;
			if (ink_alpha_on < 0)
				ink_alpha_on = 0;
			if (ink_alpha_off < 0)
				ink_alpha_off = 0;
			bool enable_status, enable_dotmatrix, clear_dots;

			bool mode_6 = false;

			auto screen_buffer = this->screen_buffer;
			uint8_t* screen_buffer1;
			size_t row_size = ROW_SIZE;
			if constexpr (hardware_id == HW_CLASSWIZ_II) {
				screen_buffer1 = this->screen_buffer1;
			}
			if (screen_buffer_select != 0) {
				screen_buffer = (uint8_t*)n_ram_buffer - casioemu::GetRamBaseAddr(hardware_id) + casioemu::GetScreenBufferOffset(emulator.hardware_id, screen_buffer_select);
				if (hardware_id == HW_CLASSWIZ_II) {
					screen_buffer1 = screen_buffer + 0x600;
				}
				row_size = ROW_SIZE_DISP;
			}

			if (!enabled_2)
				goto clean_scr;

			switch (screen_mode & 7) {
			case 4: // 100
				enable_dotmatrix = true;
				clear_dots = true;
				enable_status = false;
				break;

			case 5: // 101
				enable_dotmatrix = true;
				clear_dots = false;
				enable_status = true;
				break;

			case 6: // 110
				enable_dotmatrix = true;
				clear_dots = true;
				enable_status = true;
				mode_6 = true;
				break;

			default:
				goto clean_scr;
			}
			if (screen_range & 0b100000)
				goto clean_scr;
			{
				bool flip_screen_h = screen_mode & 0b1000;
				bool flip_screen_v = !(screen_mode & 0b10000);
				if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
				}
				else {
					flip_screen_v = flip_screen_v = 0;
				}
				int rng1 = (4 - (screen_range & 0x3));
				ink_alpha_off *= (4 / rng1);
				ink_alpha_on *= (4 / rng1);
				int rng = rng1 * 8;

				if (enable_status) {
					int ink_alpha = ink_alpha_off;
					if constexpr (hardware_id == HW_CLASSWIZ_II) {
						int x = 0;
						for (int ix = 1; ix != SPR_MAX; ++ix) {
							ink_alpha = ink_alpha_off;
							auto off = (sprite_bitmap[ix].offset + screen_offset * row_size) % ((N_ROW + 1) * row_size);
							if (screen_buffer[off] & sprite_bitmap[ix].mask)
								ink_alpha += (ink_alpha_on - ink_alpha_off) * 0.2;
							if (screen_buffer1[off] & sprite_bitmap[ix].mask)
								ink_alpha += (ink_alpha_on - ink_alpha_off) * 0.8;
							if (screen_refresh_rate >= screen_flashing_threshold)
								ink_alpha *= screen_scan_alpha[0];
							screen_ink_alpha[x] = screen_ink_alpha[x] * ratio + ink_alpha * (1 - ratio);
							x++;
						}
					}
					else {
						int x = 0;
						for (int ix = 1; ix != SPR_MAX; ++ix) {
							auto off = (sprite_bitmap[ix].offset + screen_offset * row_size) % ((N_ROW + 1) * row_size);
							if (screen_buffer[off] & sprite_bitmap[ix].mask)
								ink_alpha = ink_alpha_on;
							else
								ink_alpha = ink_alpha_off;
							if (screen_refresh_rate >= screen_flashing_threshold)
								ink_alpha *= screen_scan_alpha[0];
							screen_ink_alpha[x] = screen_ink_alpha[x] * ratio + ink_alpha * (1 - ratio);
							x++;
						}
					}
				}
				else {
					if constexpr (hardware_id == HW_CLASSWIZ_II) {
						for (size_t i = 0; i < 192; i++) {
							screen_ink_alpha[i] *= ratio;
						}
					}
					else {
						for (size_t i = 0; i < 192; i++) {
							screen_ink_alpha[i] *= ratio;
						}
					}
				}

				if (enable_dotmatrix) {
					static constexpr auto SPR_PIXEL = 0;
					SDL_Rect dest = Screen<hardware_id>::sprite_info[SPR_PIXEL].dest;
					int ink_alpha = ink_alpha_off;
					if (mode_6) {
						ink_alpha_on = ink_alpha_off /= 2.55;
					}
					if constexpr (hardware_id == HW_CLASSWIZ_II) {
						for (int iy2 = 1; iy2 != (N_ROW + 1); ++iy2) {
							int iy = (iy2 + screen_offset) % (N_ROW + 1);
							bool clear = 0;
							if (iy2 >= rng && iy2 < 32)
								clear = 1;
							if (iy2 >= 32) {
								if (iy2 <= 32 + rng) {
									iy = (iy2 - 32 + rng + screen_offset) % (N_ROW + 1);
								}
								else {
									clear = 1;
								}
							}
							dest.x = sprite_info[SPR_PIXEL].dest.x;
							dest.y = sprite_info[SPR_PIXEL].dest.y + (iy2 - 1) * sprite_info[SPR_PIXEL].src.h;
							int x = 0;
							for (int ix = 0; ix != ROW_SIZE_DISP; ++ix) {
								auto index = (flip_screen_v ? N_ROW - iy : iy) * row_size + ix;
								for (uint8_t mask = 0x80; mask; mask >>= 1, dest.x += sprite_info[SPR_PIXEL].src.w) {
									ink_alpha = ink_alpha_off;
									if (!clear_dots && screen_buffer[index] & mask)
										ink_alpha += (ink_alpha_on - ink_alpha_off) * 0.2;
									if (!clear_dots && screen_buffer1[index] & mask)
										ink_alpha += (ink_alpha_on - ink_alpha_off) * 0.8;
									if (screen_refresh_rate >= screen_flashing_threshold)
										ink_alpha *= screen_scan_alpha[iy];
									if (clear)
										ink_alpha = 0;
									float& dat = screen_ink_alpha[(flip_screen_h ? (191 - x) : x) + iy2 * 192];
									dat = dat * ratio + ink_alpha * (1 - ratio);
									x++;
								}
							}
						}
					}
					else {
						for (int iy2 = 1; iy2 != (N_ROW + 1); ++iy2) {
							int iy = (iy2 + screen_offset) % (N_ROW + 1);
							bool clear = 0;
							if (iy2 >= rng && iy2 < 32)
								clear = 1;
							if (iy2 >= 32) {
								if (iy2 <= 32 + rng) {
									iy = (iy2 - 32 + rng + screen_offset) % (N_ROW + 1);
								}
								else {
									clear = 1;
								}
							}
							dest.x = sprite_info[SPR_PIXEL].dest.x;
							dest.y = sprite_info[SPR_PIXEL].dest.y + (iy2 - 1) * sprite_info[SPR_PIXEL].src.h;
							int x = 0;
							for (int ix = 0; ix != ROW_SIZE_DISP; ++ix) {
								auto index = (flip_screen_v ? N_ROW + 1 - iy : iy) * row_size + ix;
								for (uint8_t mask = 0x80; mask; mask >>= 1, dest.x += sprite_info[SPR_PIXEL].src.w) {
									if (screen_buffer[index] & mask)
										ink_alpha = ink_alpha_on;
									else
										ink_alpha = ink_alpha_off;
									if (screen_refresh_rate >= screen_flashing_threshold)
										ink_alpha *= screen_scan_alpha[iy];
									if (clear)
										ink_alpha = 0;
									float& dat = screen_ink_alpha[(flip_screen_h ? (191 - x) : x) + iy2 * 192];
									dat = dat * ratio + ink_alpha * (1 - ratio);
									x++;
								}
							}
						}
					}
				}
				else {
					if constexpr (hardware_id == HW_CLASSWIZ_II) {
						for (size_t i = 192; i < 64 * 192; i++) {
							screen_ink_alpha[i] *= ratio;
						}
					}
					else {
						for (size_t i = 192; i < 64 * 192; i++) {
							screen_ink_alpha[i] *= ratio;
						}
					}
				}
			}
			return;
		clean_scr:
			if constexpr (hardware_id == HW_CLASSWIZ_II) {
				for (size_t i = 0; i < 64 * 192; i++) {
					screen_ink_alpha[i] *= ratio;
				}
			}
			else {
				for (size_t i = 0; i < 64 * 192; i++) {
					screen_ink_alpha[i] *= ratio;
				}
			}
			return;
		}
	};

	template <>
	const int Screen<HW_TI>::N_ROW = 64;
	template <>
	const int Screen<HW_TI>::ROW_SIZE = 32;
	template <>
	const int Screen<HW_TI>::OFFSET = 32;
	template <>
	const int Screen<HW_TI>::ROW_SIZE_DISP = 24;
	template <>
	const int Screen<HW_TI>::SPR_MAX = 14;
	template <>
	const SpriteBitmap Screen<HW_TI>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		{"rsd_2nd", 1, 17},
		{"rsd_fix", 0, 0x00},
		{"rsd_hbo", 0, 0x00},
		{"rsd_sci", 0, 0x01},
		{"rsd_eng", 0, 0x01},
		{"rsd_deg", 0, 0x01},
		{"rsd_rad", 0, 0x01},
		{"rsd_bat", 0, 0x02},
		{"rsd_wait", 1, 164},
		{"rsd_left", 0, 0x02},
		{"rsd_up", 0, 0x02},
		{"rsd_down", 0, 0x02},
		{"rsd_right", 0, 0x02},
	};

	template <>
	const int Screen<HW_CLASSWIZ_II>::N_ROW = 63;
	template <>
	const int Screen<HW_CLASSWIZ_II>::ROW_SIZE = 32;
	template <>
	const int Screen<HW_CLASSWIZ_II>::OFFSET = 32;
	template <>
	const int Screen<HW_CLASSWIZ_II>::ROW_SIZE_DISP = 24;
	template <>
	const int Screen<HW_CLASSWIZ_II>::SPR_MAX = 19;

	template <>
	const int Screen<HW_CLASSWIZ>::N_ROW = 63;
	template <>
	const int Screen<HW_CLASSWIZ>::ROW_SIZE = 32;
	template <>
	const int Screen<HW_CLASSWIZ>::OFFSET = 32;
	template <>
	const int Screen<HW_CLASSWIZ>::ROW_SIZE_DISP = 24;
	template <>
	const int Screen<HW_CLASSWIZ>::SPR_MAX = 21;

	template <>
	const int Screen<HW_ES_PLUS>::N_ROW = 31;
	template <>
	const int Screen<HW_ES_PLUS>::ROW_SIZE = 16;
	template <>
	const int Screen<HW_ES_PLUS>::OFFSET = 16;
	template <>
	const int Screen<HW_ES_PLUS>::ROW_SIZE_DISP = 12;
	template <>
	const int Screen<HW_ES_PLUS>::SPR_MAX = 19;

	template <>
	const SpriteBitmap Screen<HW_CLASSWIZ_II>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		{"rsd_s", 0x01, 0x01},
		{"rsd_math", 0x01, 0x03},
		{"rsd_d", 0x01, 0x04},
		{"rsd_r", 0x01, 0x05},
		{"rsd_g", 0x01, 0x06},
		{"rsd_fix", 0x01, 0x07},
		{"rsd_sci", 0x01, 0x08},
		{"rsd_e", 0x01, 0x0A},
		{"rsd_cmplx", 0x01, 0x0B},
		{"rsd_angle", 0x01, 0x0C},
		{"rsd_wdown", 0x01, 0x0D},
		{"rsd_verify", 0x01, 0x0E},
		{"rsd_left", 0x01, 0x10},
		{"rsd_down", 0x01, 0x11},
		{"rsd_up", 0x01, 0x12},
		{"rsd_right", 0x01, 0x13},
		{"rsd_pause", 0x01, 0x15},
		{"rsd_sun", 0x01, 0x16}};

	template <>
	const SpriteBitmap Screen<HW_CLASSWIZ>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		{"rsd_s", 0x01, 0x00},
		{"rsd_a", 0x01, 0x01},
		{"rsd_m", 0x01, 0x02},
		{"rsd_sto", 0x01, 0x03},
		{"rsd_math", 0x01, 0x05},
		{"rsd_d", 0x01, 0x06},
		{"rsd_r", 0x01, 0x07},
		{"rsd_g", 0x01, 0x08},
		{"rsd_fix", 0x01, 0x09},
		{"rsd_sci", 0x01, 0x0A},
		{"rsd_e", 0x01, 0x0B},
		{"rsd_cmplx", 0x01, 0x0C},
		{"rsd_angle", 0x01, 0x0D},
		{"rsd_wdown", 0x01, 0x0F},
		{"rsd_left", 0x01, 0x10},
		{"rsd_down", 0x01, 0x11},
		{"rsd_up", 0x01, 0x12},
		{"rsd_right", 0x01, 0x13},
		{"rsd_pause", 0x01, 0x15},
		{"rsd_sun", 0x01, 0x16}};

	template <>
	const SpriteBitmap Screen<HW_ES_PLUS>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		{"rsd_s", 0x10, 0x00},
		{"rsd_a", 0x04, 0x00},
		{"rsd_m", 0x10, 0x01},
		{"rsd_sto", 0x02, 0x01},
		{"rsd_rcl", 0x40, 0x02},
		{"rsd_stat", 0x40, 0x03},
		{"rsd_cmplx", 0x80, 0x04},
		{"rsd_mat", 0x40, 0x05},
		{"rsd_vct", 0x01, 0x05},
		{"rsd_d", 0x20, 0x07},
		{"rsd_r", 0x02, 0x07},
		{"rsd_g", 0x10, 0x08},
		{"rsd_fix", 0x01, 0x08},
		{"rsd_sci", 0x20, 0x09},
		{"rsd_math", 0x40, 0x0A},
		{"rsd_down", 0x08, 0x0A},
		{"rsd_up", 0x80, 0x0B},
		{"rsd_disp", 0x10, 0x0B}};

	template <HardwareId hardware_id>
	void Screen<hardware_id>::Initialise() {
		if (!inited) {
			renderer = emulator.GetRenderer();
			interface_texture = emulator.GetInterfaceTexture();
			sprite_info.resize(SPR_MAX);
			for (int ix = 0; ix != SPR_MAX; ++ix)
				sprite_info[ix] = emulator.ModelDefinition.sprites[sprite_bitmap[ix].name];

			ink_colour = emulator.ModelDefinition.ink_color;
			if constexpr (hardware_id == HW_TI) {
				screen_buffer = new uint8_t[192 * 9];
				// TODO: remove this
				memset(screen_buffer, 0, 192 * 9);
				// fillRandomData(screen_buffer, 192*9);
			}
			else {
				screen_buffer = new uint8_t[(N_ROW + 1) * ROW_SIZE];
				fillRandomData(screen_buffer, (N_ROW + 1) * ROW_SIZE);
			}
			if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
				region_power.Setup(
					0xF03D, 1, "Screen/Power", this,
					[](MMURegion* region, size_t offset) {
						return ((Screen*)region->userdata)->screen_power;
					},
					[](MMURegion* region, size_t offset, uint8_t data) {
						bool a = (((Screen*)region->userdata)->screen_power & 1) ^ (data & 1);
						((Screen*)region->userdata)->screen_power = data & 0xf;
						if (a && ((data & 1) == 0)) { // 关闭屏幕
							((Screen*)region->userdata)->Uninitialise();
						}
						else {
							((Screen*)region->userdata)->Initialise();
						}
					},
					emulator);
			}
			if constexpr (hardware_id == HW_CLASSWIZ_II) {
				screen_buffer1 = new uint8_t[(N_ROW + 1) * ROW_SIZE];
				fillRandomData(screen_buffer1, (N_ROW + 1) * ROW_SIZE);
			}
			inited = true;
		}
		if constexpr (hardware_id == HW_TI) {
			auto pp = emulator.chipset.QueryInterface<IPortProvider>();
			pp->SetPortOutputCallback(7, [&](uint8_t data) {
				ti_port7 = data;
			});
			pp->SetPortOutputCallback(5, [&](uint8_t data) {
				// ti_port5 = data;
				if (ti_a0 && !(data & 0x40)) {
					if ((data & 0x10)) {
						auto bit_off = ti_col;
						auto off = bit_off + ti_page * 192;
						if (off > 192 * 9) {
							return;
						}
						if (off > 192 * 8) {
							std::cout << std::dec << off - 192 * 8 << " <- 0x" << std::hex << ti_port7 << "\n";
						}
						screen_buffer[off] = ti_port7;
						ti_col++;
						if (ti_col >= 192) {
							ti_col = 0;
							ti_page++;
						}
					}
					else {
						auto data = ti_port7;
						switch (ti_port_status) {
						case 0: {
							auto dh = data >> 4;
							if (dh == 0) {
								ti_col = (ti_col & 0xf0) | (data & 0xf);
							}
							else if (dh == 1) {
								ti_col = (ti_col & 0xf) | ((data & 0xf) << 4);
							}
							else if ((dh & 0b1100) == 0b0100) {
								// std::cout << "Set Scroll line " << (data & 0x3f) << "\n";
							}
							else if (dh == 0b1011) {
								// std::cout << "Set page  " << (data & 0xf) << "\n";
								ti_page = (data & 0xf);
							}
							else if ((data >> 3) == 17) {
								// std::cout << "Set addressing mode\n";
							}
							else if ((data >> 2) == 58) {
								// std::cout << "Set bias\n";
							}
							else if ((data >> 2) == 40) {
								// std::cout << "Set frame rate\n";
							}
							else if ((data >> 1) == 82) {
								// std::cout << "Clear all display segments\n";
							}
							else if ((data >> 1) == 83) {
								// std::cout << "Set inverse display\n";
							}
							else if ((data & 0xf9) == 0xc0) {
								// std::cout << "Set Com Seg Scan Direction\n";
							}
							else if (data == 0xe3) {
								// std::cout << "Nop\n";
							}
							else if (data == 0xe2) {
								// std::cout << "Software reset\n";
							}
							else if (data == 0xaf) {
								// std::cout << "Enabled screen!\n";
								ti_enabled = 1;
							}
							else if (data == 0x81) {
								ti_port_status = 1;
							}
							else if (data == 0xae) {
								// std::cout << "Disabled screen!\n";
								ti_enabled = 0;
							}
							else {
								std::cout << "[Screen][Warn] Unknown ST7525 command: 0x" << std::hex << (int)data << "\n";
							}
							break;
						}
						case 1:
							// std::cout << "Set contrast!\n";
							ti_contrast = data;
							ti_port_status = 0;
							break;
						}
					}
				}
				ti_a0 = (data & 0x40);
			});
			return;
		}
		if (!(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) || (!enabled_2 && (screen_power & 1))) {
			if constexpr (hardware_id != HW_CLASSWIZ_II) {
				region_buffer.Setup(
					0xF800, (N_ROW + 1) * ROW_SIZE, "Screen/Buffer", this, [](MMURegion* region, size_t offset) {
				offset -= region->base;
				if (offset % ROW_SIZE >= ROW_SIZE_DISP)
					return (uint8_t)0;
				return ((Screen*)region->userdata)->screen_buffer[offset]; },
					[](MMURegion* region, size_t offset, uint8_t data) {
					offset -= region->base;
					if (offset % ROW_SIZE >= ROW_SIZE_DISP)
						return;

                                        auto this_obj = (Screen*)region->userdata;
                                        this_obj->screen_buffer[offset] = data; },
					emulator);
			}
			else {
				region_buffer.Setup(
					0xF800, (N_ROW + 1) * ROW_SIZE, "Screen/Buffer", this,
					[](MMURegion* region, size_t offset) {
						offset -= region->base;
						if (offset % ROW_SIZE >= ROW_SIZE_DISP)
							return (uint8_t)0;
						if (((Screen*)region->userdata)->screen_select & 0x04) {
							return ((Screen*)region->userdata)->screen_buffer1[offset];
						}
						else {
							return ((Screen*)region->userdata)->screen_buffer[offset];
						}
					},
					[](MMURegion* region, size_t offset, uint8_t data) {
						offset -= region->base;
						if (offset % ROW_SIZE >= ROW_SIZE_DISP)
							return;

						auto this_obj = (Screen*)region->userdata;
						if (!(this_obj->screen_mode & 0x40)) {
							this_obj->screen_buffer1[offset] = this_obj->screen_buffer[offset] = data;
							return;
						}
						if (this_obj->screen_select & 0x04) {
							this_obj->screen_buffer1[offset] = data;
						}
						else {
							this_obj->screen_buffer[offset] = data;
						}
					},
					emulator);
				if (!emulator.ModelDefinition.real_hardware) {
					// region_buffer.Setup(
					//	0xF800, (N_ROW + 1) * ROW_SIZE, "Screen/Buffer", this,
					//	[](MMURegion* region, size_t offset) {
					//		offset -= region->base;
					//		if (offset % ROW_SIZE >= ROW_SIZE_DISP)
					//			return (uint8_t)0;
					//		return ((Screen*)region->userdata)->screen_buffer[offset];
					//	},
					//	[](MMURegion* region, size_t offset, uint8_t data) {
					//		offset -= region->base;
					//		if (offset % ROW_SIZE >= ROW_SIZE_DISP)
					//			return;

					//                auto this_obj = (Screen*)region->userdata;
					//                this_obj->screen_buffer[offset] = data;
					//        },
					//        emulator);
					region_buffer1.Setup(
						0x89000, (N_ROW + 1) * ROW_SIZE, "Screen/Buffer1", this,
						[](MMURegion* region, size_t offset) {
							offset -= region->base;
							if (offset % ROW_SIZE >= ROW_SIZE_DISP)
								return (uint8_t)0;
							return ((Screen*)region->userdata)->screen_buffer1[offset];
						},
						[](MMURegion* region, size_t offset, uint8_t data) {
							offset -= region->base;
							if (offset % ROW_SIZE >= ROW_SIZE_DISP)
								return;

							auto this_obj = (Screen*)region->userdata;
							this_obj->screen_buffer1[offset] = data;
						},
						emulator);
				}
			}
			if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
				region_range.Setup(0xF030, 1, "Screen/Range", &screen_range, MMURegion::DefaultRead<uint8_t, 0x2F>,
					MMURegion::DefaultWrite<uint8_t, 0x2F>, emulator);
			}
			else {
				region_range.Setup(0xF030, 1, "Screen/Range", &screen_range, MMURegion::DefaultRead<uint8_t, 0x07>,
					MMURegion::DefaultWrite<uint8_t, 0x07>, emulator);
			}

			if constexpr (hardware_id == HW_CLASSWIZ_II) {
				region_mode.Setup(
					0xF031, 1, "Screen/Mode", this,
					[](MMURegion* region, size_t offset) {
						auto screen = ((Screen*)region->userdata);
						return screen->screen_mode;
					},
					[](MMURegion* region, size_t offset, uint8_t data) {
						auto screen = ((Screen*)region->userdata);
						auto old = screen->screen_mode & 0b1000;
						auto new_ = data & 0b1000;
						if (old ^ new_) {
							auto sb = screen->screen_buffer;
							for (int iy = 0; iy != (N_ROW + 1); ++iy) {
								for (int ix = 0; ix != ROW_SIZE_DISP; ++ix) {
									sb[ix + iy * ROW_SIZE] = bit_lookup_table[sb[(ix) + iy * ROW_SIZE]];
								}
							}
							for (int iy = 0; iy != (N_ROW + 1); ++iy) {
								for (int ix = 0; ix != (ROW_SIZE_DISP / 2); ++ix) {
									std::swap(sb[ix + iy * ROW_SIZE], sb[(ROW_SIZE_DISP - 1 - ix) + iy * ROW_SIZE]);
								}
							}
							if constexpr (hardware_id == HW_CLASSWIZ_II) {
								sb = screen->screen_buffer1;
								for (int iy = 0; iy != (N_ROW + 1); ++iy) {
									for (int ix = 0; ix != ROW_SIZE_DISP; ++ix) {
										sb[ix + iy * ROW_SIZE] = bit_lookup_table[sb[(ix) + iy * ROW_SIZE]];
									}
								}
								for (int iy = 0; iy != (N_ROW + 1); ++iy) {
									for (int ix = 0; ix != (ROW_SIZE_DISP / 2); ++ix) {
										std::swap(sb[ix + iy * ROW_SIZE], sb[(ROW_SIZE_DISP - 1 - ix) + iy * ROW_SIZE]);
									}
								}
							}
						}
						screen->screen_mode = data & 127;
					},
					emulator);
			}
			else if constexpr (hardware_id == HW_CLASSWIZ) {
				region_mode.Setup(0xF031, 1, "Screen/Mode", &screen_mode, MMURegion::DefaultRead<uint8_t, 63>,
					MMURegion::DefaultWrite<uint8_t, 63>, emulator);
			}
			else {
				region_mode.Setup(0xF031, 1, "Screen/Mode", &screen_mode, MMURegion::DefaultRead<uint8_t, 0x07>,
					MMURegion::DefaultWrite<uint8_t, 0x07>, emulator);
			}
			if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
				region_contrast.Setup(0xF032, 1, "Screen/Contrast", &screen_contrast, MMURegion::DefaultRead<uint8_t, 0x3F>,
					MMURegion::DefaultWrite<uint8_t, 0x3F>, emulator);
				region_unk1.Setup(
					0xF03E, 1, "Screen/Unk1", this,
					[](MMURegion* region, size_t offset) {
						return (uint8_t)0;
					},
					[](MMURegion* region, size_t offset, uint8_t data) {
						((Screen*)region->userdata)->emulator.chipset.mmu.WriteData(0xF817, data);
					},
					emulator);
				region_unk2.Setup(
					0xF03F, 1, "Screen/Unk2", this,
					[](MMURegion* region, size_t offset) {
						return (uint8_t)0;
					},
					[](MMURegion* region, size_t offset, uint8_t data) {
						((Screen*)region->userdata)->emulator.chipset.mmu.WriteData(0xF817, data);
					},
					emulator);
			}
			else {
				region_contrast.Setup(0xF032, 1, "Screen/Contrast", &screen_contrast, MMURegion::DefaultRead<uint8_t, 0x1f>,
					MMURegion::DefaultWrite<uint8_t, 0x1f>, emulator);
			}

			if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
				region_select.Setup(0xF037, 1, "Screen/Select", &screen_select, MMURegion::DefaultRead < uint8_t, 0x04 | 1 >,
					MMURegion::DefaultWrite < uint8_t, 0x04 | 1 >, emulator);

				region_brightness.Setup(0xF033, 1, "Screen/Brightness", &screen_brightness, MMURegion::DefaultRead<uint8_t, 0x07>,
					MMURegion::DefaultWrite<uint8_t, 0x07>, emulator);

				/*
cwx中F03B的值应该是由屏幕扫描和F035/F036决定的
1.每行扫描的时间大概是( [0xF034] * 25 ) us
2.F03B的mask是3，屏幕每扫描( [0xF036] == 0 ? 64 : [0xF035] )行后F03B的基础值会在0和3之间切换，如果F036是0的话这个循环的半周期和屏幕扫描应该是对齐的，也就是F03B的基础值切换后对应屏幕的第0行扫描（注：F035.0始终为1）
3.扫描屏幕的第0行 (对应bit0?) 及第32行 (对应bit1?) 时，F03B对应的bit会反转

n为行扫描计数，[0xF03B] = ( ( n / ( [0xF036] == 0 ? 64 : [0xF035] ) ) % 2 ? 3 : 0 ) ^ ( n % 64 == 0 ? 1 : ( n % 64 == 32 ? 2 : 0)  )
				*/

				region_scan_report_op1.Setup(0xF035, 1, "Screen/ScanReportOption1", &screen_scan_report_op1, MMURegion::DefaultRead<uint8_t, 0x1E>,
					MMURegion::DefaultWrite<uint8_t, 0x1E>, emulator);

				region_scan_report_en.Setup(0xF036, 1, "Screen/ScanReportOptionEnable", &screen_scan_report_en, MMURegion::DefaultRead<uint8_t, 0b1001>,
					MMURegion::DefaultWrite<uint8_t, 0b1001>, emulator);

				region_scan_report.Setup(0xF03B, 1, "Screen/ScanReport", &screen_scan_report, MMURegion::DefaultRead<uint8_t, 0x3>,
					MMURegion::IgnoreWrite, emulator);
			}
			else {
				screen_scan_report_op1 = 0x17;
				screen_scan_report_en = 1;
			}

			if constexpr (hardware_id == HardwareId::HW_ES_PLUS) {
				region_refresh_rate.Setup(0xF034, 1, "Screen/Unknown_F034", &unk_f034, MMURegion::DefaultRead<uint8_t, 0b11>,
					MMURegion::DefaultWrite<uint8_t, 0b11>, emulator);
			}
			else {
				region_offset.Setup(0xF039, 1, "Screen/DSPOFST", &screen_offset, MMURegion::DefaultRead<uint8_t, 0x3F>,
					MMURegion::DefaultWrite<uint8_t, 0x3F>, emulator);

				// 25us
				region_refresh_rate.Setup(0xF034, 1, "Screen/RefreshRate", &screen_refresh_rate, MMURegion::DefaultRead<uint8_t, 0x7F>,
					MMURegion::DefaultWrite<uint8_t, 0x7F>, emulator);
			}
			enabled_2 = true;
		}
	}

	template <HardwareId hardware_id>
	void Screen<hardware_id>::Uninitialise() {
		fillRandomData(screen_buffer, (N_ROW + 1) * ROW_SIZE);
		if constexpr (hardware_id == HW_CLASSWIZ_II) {
			fillRandomData(screen_buffer1, (N_ROW + 1) * ROW_SIZE);
		}
		if constexpr (hardware_id != HW_CLASSWIZ_II) {
			region_buffer.Kill();
		}
		else {
			if (!emulator.ModelDefinition.real_hardware) {
				region_buffer.Kill();
				region_buffer1.Kill();
			}
			else {
				region_buffer.Kill();
			}
		}
		screen_range = 0;
		region_range.Kill();
		screen_mode = 0;
		region_mode.Kill();
		screen_contrast = 0;
		region_contrast.Kill();
		if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
			screen_select = 0;
			region_select.Kill();
			screen_scan_report_op1 = 0;
			region_scan_report_op1.Kill();
			screen_scan_report_en = 0;
			region_scan_report_en.Kill();
			screen_scan_report = 0;
			region_scan_report.Kill();
			region_unk1.Kill();
			region_unk2.Kill();
			screen_brightness = 0;
			region_brightness.Kill();
		}
		screen_refresh_rate = 0;
		region_refresh_rate.Kill();
		if constexpr (hardware_id != HardwareId::HW_ES_PLUS) {
			screen_offset = 0;
			region_offset.Kill();
		}
		enabled_2 = false;
	}
	// Function to capture the current screen and save it as a PNG file
    void CaptureScreenshot(SDL_Renderer* renderer, const std::vector<SDL_Rect>& spriteRects, const std::vector<SDL_Rect>& pixelRects) {
        // Get current time to generate a unique filename
        std::time_t t = std::time(nullptr);
        std::tm tm = *std::localtime(&t);
        std::ostringstream filename;
        
        filename << "screenshot-" 
                 << std::put_time(&tm, "%Y-%m-%d-%H-%M-%S-") << std::rand() % 1000 
                 << ".png";
        
        // Calculate the bounding box of the rendering area from both sprite and pixel rectangles  
        int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
    
        // Traverse all sprite rectangles
        for (const auto& rect : spriteRects) {
            minX = std::min(minX, rect.x);
            minY = std::min(minY, rect.y);
            maxX = std::max(maxX, rect.x + rect.w);
            maxY = std::max(maxY, rect.y + rect.h);
        }
    
        // Traverse all pixel rectangles (representing the screen pixels)
        for (const auto& rect : pixelRects) {
            minX = std::min(minX, rect.x);
            minY = std::min(minY, rect.y);
            maxX = std::max(maxX, rect.x + rect.w);
            maxY = std::max(maxY, rect.y + rect.h);
        }
    
        // Calculate the width and height of the capture area
        int captureWidth = maxX - minX;
        int captureHeight = maxY - minY;
    
        // Create a surface to capture the screen content
        SDL_Surface* screenSurface = SDL_CreateRGBSurface(0, captureWidth, captureHeight, 32,
            0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
        
        if (screenSurface != nullptr) {
            // Define the area to capture
            SDL_Rect captureRect = {minX, minY, captureWidth, captureHeight};
    
            // Copy the renderer to the surface
            if (SDL_RenderReadPixels(renderer, &captureRect, SDL_PIXELFORMAT_RGBA32, 
                screenSurface->pixels, screenSurface->pitch) == 0) {
                
    #ifdef __ANDROID__
                auto str = filename.str();
                bool success = saveImageToMediaStore(screenSurface->pixels, screenSurface->w, screenSurface->h, screenSurface->pitch, str.c_str());
                if (!success) {
                    SDL_Log("Error saving screenshot using MediaStore API");
                } else {
                    SDL_Log("Screenshot saved successfully with MediaStore API");
                }
    #else
                auto str = filename.str();
                if (IMG_SavePNG(screenSurface, str.c_str()) != 0) {
                    SDL_Log("Error saving screenshot: %s", IMG_GetError());
                } else {
                    SDL_Log("Screenshot saved to %s", str.c_str());
                }
    #endif
            } else {
                SDL_Log("Error capturing screen pixels: %s", SDL_GetError());
            }
            SDL_FreeSurface(screenSurface); // Free the surface after use
        } else {
            SDL_Log("Error creating surface: %s", SDL_GetError());
        }
    }

	void UpdatePreview(SDL_Renderer* renderer, ScreenMirror* sm, const std::vector<SDL_Rect>& spriteRects, const std::vector<SDL_Rect>& pixelRects) {

		// Calculate the bounding box of the rendering area from both sprite and pixel rectangles
		int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;

		// Traverse all sprite rectangles
		for (const auto& rect : spriteRects) {
			minX = std::min(minX, rect.x);
			minY = std::min(minY, rect.y);
			maxX = std::max(maxX, rect.x + rect.w);
			maxY = std::max(maxY, rect.y + rect.h);
		}

		// Traverse all pixel rectangles (representing the screen pixels)
		for (const auto& rect : pixelRects) {
			minX = std::min(minX, rect.x);
			minY = std::min(minY, rect.y);
			maxX = std::max(maxX, rect.x + rect.w);
			maxY = std::max(maxY, rect.y + rect.h);
		}

		// Calculate the width and height of the capture area
		int captureWidth = maxX - minX;
		int captureHeight = maxY - minY;

		// Create a surface to capture the screen content
		SDL_Surface* screenSurface = SDL_CreateRGBSurface(0, captureWidth, captureHeight, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
		if (screenSurface != nullptr) {
			// Define the area to capture
			SDL_Rect captureRect = {minX, minY, captureWidth, captureHeight};

			// Copy the renderer to the surface
			if (SDL_RenderReadPixels(renderer, &captureRect, SDL_PIXELFORMAT_RGBA32, screenSurface->pixels, screenSurface->pitch) == 0) {
				sm->update(screenSurface->pixels, screenSurface->pitch);
			}
			else {
				SDL_Log("Error capturing screen pixels: %s", SDL_GetError());
			}
			SDL_FreeSurface(screenSurface); // Free the surface after use
		}
		else {
			SDL_Log("Error creating surface: %s", SDL_GetError());
		}
	}

	std::pair<int, int> GetSize(const std::vector<SDL_Rect>& spriteRects, const std::vector<SDL_Rect>& pixelRects) {

		// Calculate the bounding box of the rendering area from both sprite and pixel rectangles
		int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;

		// Traverse all sprite rectangles
		for (const auto& rect : spriteRects) {
			minX = std::min(minX, rect.x);
			minY = std::min(minY, rect.y);
			maxX = std::max(maxX, rect.x + rect.w);
			maxY = std::max(maxY, rect.y + rect.h);
		}

		// Traverse all pixel rectangles (representing the screen pixels)
		for (const auto& rect : pixelRects) {
			minX = std::min(minX, rect.x);
			minY = std::min(minY, rect.y);
			maxX = std::max(maxX, rect.x + rect.w);
			maxY = std::max(maxY, rect.y + rect.h);
		}

		// Calculate the width and height of the capture area
		int captureWidth = maxX - minX;
		int captureHeight = maxY - minY;
		return {captureWidth, captureHeight};
	}
	// Function to collect all sprite and pixel rectangles
	template <HardwareId hardware_id>
	void Screen<hardware_id>::Frame() {
		int x = 0;
		int screenWidth = 0, screenHeight = 0;

		// Get the renderer output size if not already available
		SDL_GetRendererOutputSize(renderer, &screenWidth, &screenHeight);

		if (!emulator.ModelDefinition.enable_new_screen) {
			SDL_SetTextureColorMod(interface_texture, ink_colour.r, ink_colour.g, ink_colour.b);
		}

		// Store all the rendering rectangles (sprites and pixel areas)
		std::vector<SDL_Rect> spriteRects;
		std::vector<SDL_Rect> pixelRects;

		// Set texture transparency and copy sprites as before
		for (int ix = 1; ix != SPR_MAX; ++ix) {
			SDL_SetTextureAlphaMod(interface_texture, Uint8(std::clamp((int)screen_ink_alpha[x], 0, 255)));
			x++;
			SDL_Rect tmp1 = sprite_info[ix].src;
			SDL_Rect tmp2 = sprite_info[ix].dest;
			SDL_RenderCopy(renderer, interface_texture, &tmp1, &tmp2);
			// Store the sprite rectangle for later
			spriteRects.push_back(sprite_info[ix].dest);
		}

		static constexpr auto SPR_PIXEL = 0;
		SDL_Rect dest = Screen<hardware_id>::sprite_info[SPR_PIXEL].dest;
		for (int iy2 = 1; iy2 != (N_ROW + 1); ++iy2) {
			int x = 0;
			dest.x = sprite_info[SPR_PIXEL].dest.x;
			dest.y = sprite_info[SPR_PIXEL].dest.y + (iy2 - 1) * sprite_info[SPR_PIXEL].src.h;
			for (int ix = 0; ix != ROW_SIZE_DISP; ++ix) {
				for (uint8_t mask = 0x80; mask; mask >>= 1, dest.x += sprite_info[SPR_PIXEL].src.w) {
					// Calculate pixel-specific colors and modify texture
					if (screen_ink_alpha[x + iy2 * 192] > 255) {
						SDL_SetTextureColorMod(interface_texture,
							std::max(0, ink_colour.r - (int)(screen_ink_alpha[x + iy2 * 192] - 255)),
							std::max(0, ink_colour.g - (int)((screen_ink_alpha[x + iy2 * 192] - 255) * 0.8)),
							std::max(0, ink_colour.b - (int)((screen_ink_alpha[x + iy2 * 192] - 255) * 0.1)));
						SDL_SetTextureAlphaMod(interface_texture, 255);
					}
					else {
						SDL_SetTextureColorMod(interface_texture, ink_colour.r, ink_colour.g, ink_colour.b);
						SDL_SetTextureAlphaMod(interface_texture, Uint8(std::clamp((int)screen_ink_alpha[x + iy2 * 192], 0, 255)));
					}
					x++;
					SDL_Rect tmp1 = sprite_info[SPR_PIXEL].src;
					SDL_RenderCopy(renderer, interface_texture, &tmp1, &dest);
					// Store the pixel rectangle for later
					pixelRects.push_back(dest);
				}
			}
		}

		// If screenshot is requested, capture only the rendered screen region
		if (emulator.screenshot_requested.load()) {
			// Capture the region using both sprite and pixel rectangles
			CaptureScreenshot(renderer, spriteRects, pixelRects);
			emulator.screenshot_requested.store(false);
		}
		static ScreenMirror* mirror = nullptr;
		if (emulator.mirroring_requested.load()) {
			auto p = GetSize(spriteRects, pixelRects);
			auto sm = new ScreenMirror(p.first, p.second);
			sm->create();
			mirror = sm;
			emulator.mirroring_requested.store(false);
		}
		if (mirror) {
			UpdatePreview(renderer, mirror, spriteRects, pixelRects);
		}
	}

	template <HardwareId hardware_id>
	void Screen<hardware_id>::Reset() {
	}

	Peripheral* CreateScreen(Emulator& emulator) {
		switch (emulator.hardware_id) {
		case HW_FX_5800P:
		case HW_ES_PLUS:
			return new Screen<HW_ES_PLUS>(emulator);

		case HW_CLASSWIZ:
			return new Screen<HW_CLASSWIZ>(emulator);

		case HW_CLASSWIZ_II:
			return new Screen<HW_CLASSWIZ_II>(emulator);

		case HW_TI:
			return new Screen<HW_TI>(emulator);

		default:
			PANIC("Unknown hardware id\n");
		}
		std::abort();
	}
} // namespace casioemu