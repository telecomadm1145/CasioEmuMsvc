﻿#include "Screen.hpp"

#include "../Chipset/MMURegion.hpp"
#include "../Data/HardwareId.hpp"
#include "../Data/SpriteInfo.hpp"
#include "../Data/ColourInfo.hpp"
#include "../Logger.hpp"
#include "../Chipset/MMU.hpp"
#include "../Emulator.hpp"
#include "../Chipset/Chipset.hpp"
#include "../Gui/ScreenController.h"
#include <vector>

namespace casioemu {
	struct SpriteBitmap {
		const char* name;
		uint8_t mask, offset;
	};
	template <bool val, class T1, class T2>
	struct TypeBranch;
	template <class T1, class T2>
	struct TypeBranch<true, T1, T2> {
		using Type = T1;
	};
	template <class T1, class T2>
	struct TypeBranch<false, T1, T2> {
		using Type = T2;
	};
	template <bool val, class T1, class T2>
	using TypeBranch_t = TypeBranch<val, T1, T2>::Type;
	ScreenBase* m_screen = 0;
	template <HardwareId hardware_id>
	class Screen : public ScreenBase {
		static int const N_ROW, // excluding the 1 row used for status line
			ROW_SIZE,			// bytes
			OFFSET,				// bytes
			ROW_SIZE_DISP;		// bytes used to display

		MMURegion region_buffer, region_buffer1, region_contrast, region_mode, region_range, region_select, region_offset, region_refresh_rate;
		uint8_t *screen_buffer, *screen_buffer1, screen_contrast, screen_mode, screen_range, screen_select, screen_offset, screen_refresh_rate;
		uint8_t unk_f034;
		float screen_scan_alpha[64]{};
		float position = 0;

		SDL_Renderer* renderer;
		SDL_Texture* interface_texture;

		float screen_ink_alpha[64 * 192]{};

		enum class clswizii_sprite {
			SPR_PIXEL,
			SPR_S,
			SPR_MATH,
			SPR_D,
			SPR_R,
			SPR_G,
			SPR_FIX,
			SPR_SCI,
			SPR_E,
			SPR_CMPLX,
			SPR_ANGLE,
			SPR_WDOWN,
			SPR_VERIFY,
			SPR_LEFT,
			SPR_DOWN,
			SPR_UP,
			SPR_RIGHT,
			SPR_PAUSE,
			SPR_SUN,
			SPR_MAX
		};
		enum class clswiz_sprite {
			SPR_PIXEL,
			SPR_S,
			SPR_A,
			SPR_M,
			SPR_STO,
			SPR_MATH,
			SPR_D,
			SPR_R,
			SPR_G,
			SPR_FIX,
			SPR_SCI,
			SPR_E,
			SPR_CMPLX,
			SPR_ANGLE,
			SPR_WDOWN,
			SPR_LEFT,
			SPR_DOWN,
			SPR_UP,
			SPR_RIGHT,
			SPR_PAUSE,
			SPR_SUN,
			SPR_MAX
		};
		enum class esplus_sprite {
			SPR_PIXEL,
			SPR_S,
			SPR_A,
			SPR_M,
			SPR_STO,
			SPR_RCL,
			SPR_STAT,
			SPR_CMPLX,
			SPR_MAT,
			SPR_VCT,
			SPR_D,
			SPR_R,
			SPR_G,
			SPR_FIX,
			SPR_SCI,
			SPR_MATH,
			SPR_DOWN,
			SPR_UP,
			SPR_DISP,
			SPR_MAX
		};
		using Sprite = TypeBranch_t<hardware_id == HardwareId::HW_CLASSWIZ, clswiz_sprite,
			TypeBranch_t<hardware_id == HardwareId::HW_CLASSWIZ_II, clswizii_sprite, esplus_sprite>>;
		// Note: SPR_PIXEL must be the first enum member and SPR_MAX must be the last one.
		static const SpriteBitmap sprite_bitmap[];
		std::vector<SpriteInfo> sprite_info;
		ColourInfo ink_colour;
		/**
		 * Similar to MMURegion::DefaultRead, but takes the pointer to the Screen
		 * object as the userdata instead of the uint8_t member.
		 */
		template <typename value_type, value_type mask = (value_type)-1,
			value_type Screen::*member_ptr>
		static uint8_t DefaultRead(MMURegion* region, size_t offset) {
			auto this_obj = (Screen*)(region->userdata);
			value_type value = this_obj->*member_ptr;
			return (value & mask) >> ((offset - region->base) * 8);
		}

		/**
		 * Similar to MMURegion::DefaultWrite, except this also set the
		 * (require_frame) flag of (Peripheral) class.
		 * If (only_on_change) is true, (require_frame) is not set if the new value
		 * is the same as the old value.
		 * (region->userdata) should be a pointer to a (Screen) instance.
		 *
		 * TODO: Probably this should be a member of Peripheral class instead.
		 * (in that case (Screen) class needs to be parameterized)
		 */
		template <typename value_type, value_type mask = (value_type)-1,
			value_type Screen::*member_ptr, bool only_on_change = true>
		static void SetRequireFrameWrite(MMURegion* region, size_t offset, uint8_t data) {
			auto this_obj = (Screen*)(region->userdata);
			value_type& value = this_obj->*member_ptr;

			value_type old_value;
			if (only_on_change)
				old_value = value;

			// This part is identical to MMURegion::DefaultWrite.
			// * TODO Try to avoid duplication?
			value &= ~(((value_type)0xFF) << ((offset - region->base) * 8));
			value |= ((value_type)data) << ((offset - region->base) * 8);
			value &= mask;

			if (only_on_change && old_value == value)
				return;
			this_obj->require_frame = true;
		}


	public:
		using ScreenBase::ScreenBase;

		void Initialise() override;
		void Uninitialise() override;
		void Frame() override;
		void Reset() override;
		void RenderScreen(SDL_Renderer*, SDL_Surface*, int ink_alpha_off, bool clear_dots, int ink_alpha_on) override;
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
	const int Screen<HW_CLASSWIZ>::N_ROW = 63;
	template <>
	const int Screen<HW_CLASSWIZ>::ROW_SIZE = 32;
	template <>
	const int Screen<HW_CLASSWIZ>::OFFSET = 32;
	template <>
	const int Screen<HW_CLASSWIZ>::ROW_SIZE_DISP = 24;

	template <>
	const int Screen<HW_ES_PLUS>::N_ROW = 31;
	template <>
	const int Screen<HW_ES_PLUS>::ROW_SIZE = 16;
	template <>
	const int Screen<HW_ES_PLUS>::OFFSET = 16;
	template <>
	const int Screen<HW_ES_PLUS>::ROW_SIZE_DISP = 12;


	template <>
	const SpriteBitmap Screen<HW_CLASSWIZ_II>::sprite_bitmap[] = {
		{ "rsd_pixel", 0, 0 },
		{ "rsd_s", 0x01, 0x01 },
		{ "rsd_math", 0x01, 0x03 },
		{ "rsd_d", 0x01, 0x04 },
		{ "rsd_r", 0x01, 0x05 },
		{ "rsd_g", 0x01, 0x06 },
		{ "rsd_fix", 0x01, 0x07 },
		{ "rsd_sci", 0x01, 0x08 },
		{ "rsd_e", 0x01, 0x0A },
		{ "rsd_cmplx", 0x01, 0x0B },
		{ "rsd_angle", 0x01, 0x0C },
		{ "rsd_wdown", 0x01, 0x0D },
		{ "rsd_verify", 0x01, 0x0E },
		{ "rsd_left", 0x01, 0x10 },
		{ "rsd_down", 0x01, 0x11 },
		{ "rsd_up", 0x01, 0x12 },
		{ "rsd_right", 0x01, 0x13 },
		{ "rsd_pause", 0x01, 0x15 },
		{ "rsd_sun", 0x01, 0x16 }
	};

	template <>
	const SpriteBitmap Screen<HW_CLASSWIZ>::sprite_bitmap[] = {
		{ "rsd_pixel", 0, 0 },
		{ "rsd_s", 0x01, 0x00 },
		{ "rsd_a", 0x01, 0x01 },
		{ "rsd_m", 0x01, 0x02 },
		{ "rsd_sto", 0x01, 0x03 },
		{ "rsd_math", 0x01, 0x05 },
		{ "rsd_d", 0x01, 0x06 },
		{ "rsd_r", 0x01, 0x07 },
		{ "rsd_g", 0x01, 0x08 },
		{ "rsd_fix", 0x01, 0x09 },
		{ "rsd_sci", 0x01, 0x0A },
		{ "rsd_e", 0x01, 0x0B },
		{ "rsd_cmplx", 0x01, 0x0C },
		{ "rsd_angle", 0x01, 0x0D },
		{ "rsd_wdown", 0x01, 0x0F },
		{ "rsd_left", 0x01, 0x10 },
		{ "rsd_down", 0x01, 0x11 },
		{ "rsd_up", 0x01, 0x12 },
		{ "rsd_right", 0x01, 0x13 },
		{ "rsd_pause", 0x01, 0x15 },
		{ "rsd_sun", 0x01, 0x16 }
	};

	template <>
	const SpriteBitmap Screen<HW_ES_PLUS>::sprite_bitmap[] = {
		{ "rsd_pixel", 0, 0 },
		{ "rsd_s", 0x10, 0x00 },
		{ "rsd_a", 0x04, 0x00 },
		{ "rsd_m", 0x10, 0x01 },
		{ "rsd_sto", 0x02, 0x01 },
		{ "rsd_rcl", 0x40, 0x02 },
		{ "rsd_stat", 0x40, 0x03 },
		{ "rsd_cmplx", 0x80, 0x04 },
		{ "rsd_mat", 0x40, 0x05 },
		{ "rsd_vct", 0x01, 0x05 },
		{ "rsd_d", 0x20, 0x07 },
		{ "rsd_r", 0x02, 0x07 },
		{ "rsd_g", 0x10, 0x08 },
		{ "rsd_fix", 0x01, 0x08 },
		{ "rsd_sci", 0x20, 0x09 },
		{ "rsd_math", 0x40, 0x0A },
		{ "rsd_down", 0x08, 0x0A },
		{ "rsd_up", 0x80, 0x0B },
		{ "rsd_disp", 0x10, 0x0B }
	};

	template <HardwareId hardware_id>
	void Screen<hardware_id>::Initialise() {
		auto constexpr SPR_MAX = (int)Sprite::SPR_MAX;

		// static_assert(SPR_MAX == (sizeof(sprite_bitmap) / sizeof(sprite_bitmap[0])), "SPR_MAX and sizeof(sprite_bitmap) don't match");

		renderer = emulator.GetRenderer();
		interface_texture = emulator.GetInterfaceTexture();
		sprite_info.resize(SPR_MAX);
		for (int ix = 0; ix != SPR_MAX; ++ix)
			sprite_info[ix] = emulator.GetModelInfo(sprite_bitmap[ix].name);

		ink_colour = emulator.GetModelInfo("ink_colour");
		require_frame = true;

		screen_buffer = new uint8_t[(N_ROW + 1) * ROW_SIZE];

		if (emulator.hardware_id != HW_CLASSWIZ_II) {
			region_buffer.Setup(
				0xF800, (N_ROW + 1) * ROW_SIZE, "Screen/Buffer", this, [](MMURegion* region, size_t offset) {
				offset -= region->base;
				if (offset % ROW_SIZE >= ROW_SIZE_DISP)
					return (uint8_t)0;
				return ((Screen*)region->userdata)->screen_buffer[offset]; }, [](MMURegion* region, size_t offset, uint8_t data) {
					offset -= region->base;
					if (offset % ROW_SIZE >= ROW_SIZE_DISP)
						return;

					auto this_obj = (Screen*)region->userdata;
					// * Set require_frame to true only if the value changed.
					this_obj->require_frame |= this_obj->screen_buffer[offset] != data;
					this_obj->screen_buffer[offset] = data; }, emulator);
		}
		else {
			screen_buffer1 = new uint8_t[(N_ROW + 1) * ROW_SIZE];
			region_select.Setup(0xF037, 1, "Screen/Select", this, DefaultRead<uint8_t, 0x04, &Screen::screen_select>,
				SetRequireFrameWrite<uint8_t, 0x04, &Screen::screen_select>, emulator);
			if (!emulator.GetModelInfo("real_hardware")) {
				region_buffer.Setup(
					0xF800, (N_ROW + 1) * ROW_SIZE, "Screen/Buffer", this, [](MMURegion* region, size_t offset) {
					offset -= region->base;
					if (offset % ROW_SIZE >= ROW_SIZE_DISP)
						return (uint8_t)0;
					return ((Screen*)region->userdata)->screen_buffer[offset]; }, [](MMURegion* region, size_t offset, uint8_t data) {
						offset -= region->base;
						if (offset % ROW_SIZE >= ROW_SIZE_DISP)
							return;

						auto this_obj = (Screen*)region->userdata;
						// * Set require_frame to true only if the value changed.
						this_obj->require_frame |= this_obj->screen_buffer[offset] != data;
						this_obj->screen_buffer[offset] = data; }, emulator);
				region_buffer1.Setup(
					0x89000, (N_ROW + 1) * ROW_SIZE, "Screen/Buffer1", this, [](MMURegion* region, size_t offset) {
					offset -= region->base;
					if (offset % ROW_SIZE >= ROW_SIZE_DISP)
						return (uint8_t)0;
					return ((Screen*)region->userdata)->screen_buffer1[offset]; }, [](MMURegion* region, size_t offset, uint8_t data) {
						offset -= region->base;
						if (offset % ROW_SIZE >= ROW_SIZE_DISP)
							return;

						auto this_obj = (Screen*)region->userdata;
						// * Set require_frame to true only if the value changed.
						this_obj->require_frame |= this_obj->screen_buffer1[offset] != data;
						this_obj->screen_buffer1[offset] = data; }, emulator);
			}
			else {
				region_buffer.Setup(
					0xF800, (N_ROW + 1) * ROW_SIZE, "Screen/Buffer", this, [](MMURegion* region, size_t offset) {
					offset -= region->base;
					if (offset % ROW_SIZE >= ROW_SIZE_DISP)
						return (uint8_t)0;
					if (((Screen*)region->userdata)->screen_select & 0x04) {
						return ((Screen*)region->userdata)->screen_buffer1[offset];
					}
					else {
						return ((Screen*)region->userdata)->screen_buffer[offset];
					} }, [](MMURegion* region, size_t offset, uint8_t data) {
						offset -= region->base;
						if (offset % ROW_SIZE >= ROW_SIZE_DISP)
							return;

						auto this_obj = (Screen*)region->userdata;
						// * Set require_frame to true only if the value changed.
						if (((Screen*)region->userdata)->screen_select & 0x04) {
							this_obj->require_frame |= this_obj->screen_buffer1[offset] != data;
							this_obj->screen_buffer1[offset] = data;
						}
						else {
							this_obj->require_frame |= this_obj->screen_buffer[offset] != data;
							this_obj->screen_buffer[offset] = data;
						} }, emulator);
			}
		}

		region_range.Setup(0xF030, 1, "Screen/Range", this, DefaultRead<uint8_t, 0x07, &Screen::screen_range>,
			SetRequireFrameWrite<uint8_t, 0x07, &Screen::screen_range>, emulator);

		region_mode.Setup(0xF031, 1, "Screen/Mode", this, DefaultRead<uint8_t, 0x07, &Screen::screen_mode>,
			SetRequireFrameWrite<uint8_t, 0x07, &Screen::screen_mode>, emulator);

		region_contrast.Setup(0xF032, 1, "Screen/Contrast", this, DefaultRead<uint8_t, 0x3F, &Screen::screen_contrast>,
			SetRequireFrameWrite<uint8_t, 0x3F, &Screen::screen_contrast>, emulator);

		if (emulator.hardware_id == HardwareId::HW_ES_PLUS) {
			region_refresh_rate.Setup(0xF034, 1, "Screen/Unknown_F034", this, DefaultRead<uint8_t, 0b11, &Screen::unk_f034>,
				SetRequireFrameWrite<uint8_t, 0b11, &Screen::unk_f034>, emulator);
		}
		else {
			region_offset.Setup(0xF039, 1, "Screen/DSPOFST", this, DefaultRead<uint8_t, 0x3F, &Screen::screen_offset>,
				SetRequireFrameWrite<uint8_t, 0x3F, &Screen::screen_offset>, emulator);

			region_refresh_rate.Setup(0xF034, 1, "Screen/RefreshRate", this, DefaultRead<uint8_t, 0x3F, &Screen::screen_refresh_rate>,
				SetRequireFrameWrite<uint8_t, 0x3F, &Screen::screen_refresh_rate>, emulator);
		}
	}

	template <HardwareId hardware_id>
	void Screen<hardware_id>::Uninitialise() {
		delete[] screen_buffer;
		if (emulator.hardware_id == HW_CLASSWIZ_II)
			delete[] screen_buffer1;
	}
	inline void update_screen_scan_alpha(float* screen_scan_alpha, float t, float screen_refresh_rate) {
		// auto t_mod = fmodf(t,screen_refresh_rate);
		if (screen_refresh_rate < screen_flashing_threshold) { // thd
			for (size_t i = 0; i < 64; i++) {
				screen_scan_alpha[i] = 1;
			}
			return;
		}
		float position = fmodf(t * pow(screen_refresh_rate, -0.8) * 5, 64);
		for (size_t i = 0; i < 64; i++) {
			screen_scan_alpha[(i + int(floor(position))) % 64] = screen_flashing_brightness_coeff - i / 64. * screen_flashing_brightness_coeff;
		}
	}
	template <HardwareId hardware_id>
	void Screen<hardware_id>::Frame() {

		// if (screen_refresh_rate == 0)
		//	require_frame = false;
		// else {
		update_screen_scan_alpha(screen_scan_alpha, SDL_GetTicks64(), screen_refresh_rate);
		//}
		int ink_alpha_on = 20 + screen_contrast * 16;
		if (ink_alpha_on > 255)
			ink_alpha_on = 255;
		int ink_alpha_off = (screen_contrast - 8) * 5;
		if (ink_alpha_off < 0)
			ink_alpha_off = 0;
		float ratio = 0;
		if (enable_screen_fading) {
			if (screen_fading_blending_coefficient <= 0.01) {
				if (emulator.hardware_id == HW_CLASSWIZ_II) {
					screen_fading_blending_coefficient = 0.86;
				}
				else if (emulator.hardware_id == HW_CLASSWIZ) {
					screen_fading_blending_coefficient = 0.82;
				}
				else {
					screen_fading_blending_coefficient = 0.5;
				}
			}
			ratio = screen_fading_blending_coefficient;
		}
		bool enable_status, enable_dotmatrix, clear_dots;

		switch (screen_mode) {
		case 4:
			enable_dotmatrix = true;
			clear_dots = true;
			enable_status = false;
			break;

		case 5:
			enable_dotmatrix = true;
			clear_dots = false;
			enable_status = true;
			break;

		case 6:
			enable_dotmatrix = true;
			clear_dots = true;
			enable_status = true;
			ink_alpha_on = 80;
			ink_alpha_off = 20;
			break;

		default:
			return;
		}

		SDL_SetTextureColorMod(interface_texture, ink_colour.r, ink_colour.g, ink_colour.b);

		if (enable_status) {
			int ink_alpha = ink_alpha_off;
			if (emulator.hardware_id == HW_CLASSWIZ_II) {
				int x = 0;
				for (int ix = (int)Sprite::SPR_PIXEL + 1; ix != (int)Sprite::SPR_MAX; ++ix) {
					ink_alpha = ink_alpha_off;
					auto off = (sprite_bitmap[ix].offset + screen_offset * ROW_SIZE) % ((N_ROW + 1) * ROW_SIZE);
					if (screen_buffer[off] & sprite_bitmap[ix].mask)
						ink_alpha += (ink_alpha_on - ink_alpha_off) * 0.333;
					if (screen_buffer1[off] & sprite_bitmap[ix].mask)
						ink_alpha += (ink_alpha_on - ink_alpha_off) * 0.667;
					if (screen_refresh_rate != 0)
						ink_alpha *= screen_scan_alpha[0];
					screen_ink_alpha[x] = screen_ink_alpha[x] * ratio + ink_alpha * (1 - ratio);
					SDL_SetTextureAlphaMod(interface_texture, Uint8(std::clamp((int)screen_ink_alpha[x], 0, 255)));
					x++;
					SDL_RenderCopy(renderer, interface_texture, &sprite_info[ix].src, &sprite_info[ix].dest);
				}
			}
			else {
				int x = 0;
				for (int ix = (int)Sprite::SPR_PIXEL + 1; ix != (int)Sprite::SPR_MAX; ++ix) {
					auto off = (sprite_bitmap[ix].offset + screen_offset * ROW_SIZE) % ((N_ROW + 1) * ROW_SIZE);
					if (screen_buffer[off] & sprite_bitmap[ix].mask)
						ink_alpha = ink_alpha_on;
					else
						ink_alpha = ink_alpha_off;
					if (screen_refresh_rate != 0)
						ink_alpha *= screen_scan_alpha[0];
					screen_ink_alpha[x] = screen_ink_alpha[x] * ratio + ink_alpha * (1 - ratio);
					SDL_SetTextureAlphaMod(interface_texture, Uint8(std::clamp((int)screen_ink_alpha[x], 0, 255)));
					x++;
					SDL_RenderCopy(renderer, interface_texture, &sprite_info[ix].src, &sprite_info[ix].dest);
				}
			}
		}
		else {
			if (emulator.hardware_id == HW_CLASSWIZ_II) {
				for (size_t i = 0; i < 192; i++) {
					const float ratio = 0.9;
					screen_ink_alpha[i] *= ratio;
				}
			}
			else {
				for (size_t i = 0; i < 192; i++) {
					const float ratio = 0.85;
					screen_ink_alpha[i] *= ratio;
				}
			}
		}

		if (enable_dotmatrix) {
			static constexpr auto SPR_PIXEL = (int)Sprite::SPR_PIXEL;
			SDL_Rect dest = Screen<hardware_id>::sprite_info[SPR_PIXEL].dest;
			int ink_alpha = ink_alpha_off;
			if (emulator.hardware_id == HW_CLASSWIZ_II) {
				for (int iy2 = 1; iy2 != (N_ROW + 1); ++iy2) {
					int iy = (iy2 + screen_offset) % (N_ROW + 1);
					dest.x = sprite_info[SPR_PIXEL].dest.x;
					dest.y = sprite_info[SPR_PIXEL].dest.y + (iy2 - 1) * sprite_info[SPR_PIXEL].src.h;
					int x = 0;
					for (int ix = 0; ix != ROW_SIZE_DISP; ++ix) {
						for (uint8_t mask = 0x80; mask; mask >>= 1, dest.x += sprite_info[SPR_PIXEL].src.w) {
							ink_alpha = ink_alpha_off;
							if (!clear_dots && screen_buffer[iy * ROW_SIZE + ix] & mask)
								ink_alpha += (ink_alpha_on - ink_alpha_off) * 0.333;
							if (!clear_dots && screen_buffer1[iy * ROW_SIZE + ix] & mask)
								ink_alpha += (ink_alpha_on - ink_alpha_off) * 0.667;
							if (screen_refresh_rate != 0)
								ink_alpha *= screen_scan_alpha[iy];
							screen_ink_alpha[x + iy2 * 192] = screen_ink_alpha[x + iy2 * 192] * ratio + ink_alpha * (1 - ratio);
							SDL_SetTextureAlphaMod(interface_texture, Uint8(std::clamp((int)screen_ink_alpha[x + iy2 * 192], 0, 255)));
							x++;
							SDL_RenderCopy(renderer, interface_texture, &sprite_info[SPR_PIXEL].src, &dest);
						}
					}
				}
			}
			else {
				for (int iy2 = 1; iy2 != (N_ROW + 1); ++iy2) {
					int iy = (iy2 + screen_offset) % (N_ROW + 1);
					dest.x = sprite_info[SPR_PIXEL].dest.x;
					dest.y = sprite_info[SPR_PIXEL].dest.y + (iy2 - 1) * sprite_info[SPR_PIXEL].src.h;
					int x = 0;
					for (int ix = 0; ix != ROW_SIZE_DISP; ++ix) {
						for (uint8_t mask = 0x80; mask; mask >>= 1, dest.x += sprite_info[SPR_PIXEL].src.w) {
							if (screen_buffer[iy * ROW_SIZE + ix] & mask)
								ink_alpha = ink_alpha_on;
							else
								ink_alpha = ink_alpha_off;
							if (screen_refresh_rate != 0)
								ink_alpha *= screen_scan_alpha[iy];
							screen_ink_alpha[x + iy2 * 192] = screen_ink_alpha[x + iy2 * 192] * ratio + ink_alpha * (1 - ratio);
							SDL_SetTextureAlphaMod(interface_texture, Uint8(std::clamp((int)screen_ink_alpha[x + iy2 * 192], 0, 255)));
							x++;
							SDL_RenderCopy(renderer, interface_texture, &sprite_info[SPR_PIXEL].src, &dest);
						}
					}
				}
			}
		}
		else {
			if (emulator.hardware_id == HW_CLASSWIZ_II) {
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

	template <HardwareId hardware_id>
	void Screen<hardware_id>::Reset() {
		memset(screen_ink_alpha, 0, sizeof(screen_ink_alpha));
	}

	template <HardwareId hardware_id>
	void Screen<hardware_id>::RenderScreen(SDL_Renderer* renderer, SDL_Surface* surface, int ink_alpha_off, bool clear_dots, int ink_alpha_on) {
		static constexpr auto SPR_PIXEL = (int)Sprite::SPR_PIXEL;
		SDL_Rect dest = Screen<hardware_id>::sprite_info[SPR_PIXEL].dest;
		int ink_alpha = ink_alpha_off;
		if (emulator.hardware_id == HW_CLASSWIZ_II) {
			for (int iy2 = 0; iy2 != N_ROW; ++iy2) {
				int iy = (iy2 + screen_offset) % 64;
				dest.x = sprite_info[SPR_PIXEL].dest.x;
				dest.y = sprite_info[SPR_PIXEL].dest.y + iy * sprite_info[SPR_PIXEL].src.h;
				for (int ix = 0; ix != ROW_SIZE_DISP; ++ix) {
					for (uint8_t mask = 0x80; mask; mask >>= 1, dest.x += sprite_info[SPR_PIXEL].src.w) {
						ink_alpha = ink_alpha_off;
						if (!clear_dots && screen_buffer[iy * ROW_SIZE + OFFSET + ix] & mask)
							ink_alpha += (ink_alpha_on - ink_alpha_off) * 0.333;
						if (!clear_dots && screen_buffer1[iy * ROW_SIZE + OFFSET + ix] & mask)
							ink_alpha += (ink_alpha_on - ink_alpha_off) * 0.667;
						SDL_FillRect(surface, &dest, 0xffffffff); // ink_alpha
					}
				}
			}
		}
		else {
			for (int iy2 = 0; iy2 != N_ROW; ++iy2) {
				int iy = (iy2 + screen_offset) % 64;
				dest.x = sprite_info[SPR_PIXEL].dest.x;
				dest.y = sprite_info[SPR_PIXEL].dest.y + iy * sprite_info[SPR_PIXEL].src.h;
				for (int ix = 0; ix != ROW_SIZE_DISP; ++ix) {
					for (uint8_t mask = 0x80; mask; mask >>= 1, dest.x += sprite_info[SPR_PIXEL].src.w) {
						if (!clear_dots && screen_buffer[iy * ROW_SIZE + OFFSET + ix] & mask)
							ink_alpha = ink_alpha_on;
						else
							ink_alpha = ink_alpha_off;
						SDL_FillRect(surface, &dest, 0xffffffff); // ink_alpha
					}
				}
			}
		}
	}

	Peripheral* CreateScreen(Emulator& emulator) {
		switch (emulator.hardware_id) {
		case HW_5800P:
		case HW_ES_PLUS:
			return m_screen = new Screen<HW_ES_PLUS>(emulator);

		case HW_CLASSWIZ:
			return m_screen = new Screen<HW_CLASSWIZ>(emulator);

		case HW_CLASSWIZ_II:
			return m_screen = new Screen<HW_CLASSWIZ_II>(emulator);
		default:
			PANIC("Unknown hardware id\n");
		}
	}
}