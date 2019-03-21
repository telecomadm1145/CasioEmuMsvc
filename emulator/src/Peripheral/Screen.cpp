#include "Screen.hpp"

#include "../Logger.hpp"
#include "../Chipset/MMU.hpp"
#include "../Emulator.hpp"
#include "../Chipset/Chipset.hpp"

namespace casioemu
{
	Screen::SpriteBitmap Screen::sprite_bitmap[] = {
		{"rsd_pixel",    0,    0},
		{"rsd_s",     0x10, 0x00},
		{"rsd_a",     0x04, 0x00},
		{"rsd_m",     0x10, 0x01},
		{"rsd_sto",   0x02, 0x01},
		{"rsd_rcl",   0x40, 0x02},
		{"rsd_stat",  0x40, 0x03},
		{"rsd_cmplx", 0x80, 0x04},
		{"rsd_mat",   0x40, 0x05},
		{"rsd_vct",   0x01, 0x05},
		{"rsd_d",     0x20, 0x07},
		{"rsd_r",     0x02, 0x07},
		{"rsd_g",     0x10, 0x08},
		{"rsd_fix",   0x01, 0x08},
		{"rsd_sci",   0x20, 0x09},
		{"rsd_math",  0x40, 0x0A},
		{"rsd_down",  0x08, 0x0A},
		{"rsd_up",    0x80, 0x0B},
		{"rsd_disp",  0x10, 0x0B}
	};

	void Screen::Initialise()
	{
		static_assert(SPR_MAX == (sizeof(sprite_bitmap) / sizeof(sprite_bitmap[0])), "SPR_MAX and sizeof(sprite_bitmap) don't match");

	    renderer = emulator.GetRenderer();
	    interface_texture = emulator.GetInterfaceTexture();
		for (int ix = 0; ix != SPR_MAX; ++ix)
			sprite_info[ix] = emulator.GetModelInfo(sprite_bitmap[ix].name);
		
		ink_colour = emulator.GetModelInfo("ink_colour");
		require_frame = true;

		screen_buffer = new uint8_t[0x0200];

		region_buffer.Setup(0xF800, 0x0200, "Screen/Buffer", this, [](MMURegion *region, size_t offset) {
			offset -= region->base;
			if ((offset & 0x000F) >= 0x000C)
				return (uint8_t)0;
			return ((Screen *)region->userdata)->screen_buffer[offset];
		}, [](MMURegion *region, size_t offset, uint8_t data) {
			offset -= region->base;
			if ((offset & 0x000F) >= 0x000C)
				return;

			auto this_obj = (Screen *)region->userdata;
			// * Set require_frame to true only if the value changed.
			this_obj->require_frame |= this_obj->screen_buffer[offset] != data;
			this_obj->screen_buffer[offset] = data;
		}, emulator);

		region_range.Setup(0xF030, 1, "Screen/Range", this, DefaultRead<uint8_t, 0x07, &Screen::screen_range>,
				SetRequireFrameWrite<uint8_t, 0x07, &Screen::screen_range>, emulator);

		region_mode.Setup(0xF031, 1, "Screen/Mode", this, DefaultRead<uint8_t, 0x07, &Screen::screen_mode>,
				SetRequireFrameWrite<uint8_t, 0x07, &Screen::screen_mode>, emulator);

		region_contrast.Setup(0xF032, 1, "Screen/Contrast", this, DefaultRead<uint8_t, 0x1F, &Screen::screen_contrast>,
				SetRequireFrameWrite<uint8_t, 0x1F, &Screen::screen_contrast>, emulator);
	}

	void Screen::Uninitialise()
	{
		delete[] screen_buffer;
	}

	void Screen::Frame()
	{
		require_frame = false;

		int ink_alpha_on = 20 + screen_contrast * 16;
		if (ink_alpha_on > 255)
			ink_alpha_on = 255;
		int ink_alpha_off = (screen_contrast - 8) * 7;
		if (ink_alpha_off < 0)
			ink_alpha_off = 0;

		bool enable_status, enable_dotmatrix, clear_dots;

		switch (screen_mode)
		{
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

		if (enable_status)
		{
			for (int ix = SPR_PIXEL + 1; ix != SPR_MAX; ++ix)
			{
				if (screen_buffer[sprite_bitmap[ix].offset] & sprite_bitmap[ix].mask)
					SDL_SetTextureAlphaMod(interface_texture, ink_alpha_on);
				else
					SDL_SetTextureAlphaMod(interface_texture, ink_alpha_off);
				SDL_RenderCopy(renderer, interface_texture, &sprite_info[ix].src, &sprite_info[ix].dest);
			}
		}

		if (enable_dotmatrix)
		{
			SDL_Rect dest = sprite_info[SPR_PIXEL].dest;
			for (int iy = 0; iy != 31; ++iy)
			{
				dest.x = sprite_info[SPR_PIXEL].dest.x;
				dest.y = sprite_info[SPR_PIXEL].dest.y + iy * sprite_info[SPR_PIXEL].src.h;
				for (int ix = 0; ix != 12; ++ix)
				{
					for (uint8_t mask = 0x80; mask; mask >>= 1, dest.x += sprite_info[SPR_PIXEL].src.w)
					{
						if (!clear_dots && screen_buffer[(iy << 4) + 0x10 + ix] & mask)
							SDL_SetTextureAlphaMod(interface_texture, ink_alpha_on);
						else
							SDL_SetTextureAlphaMod(interface_texture, ink_alpha_off);
						SDL_RenderCopy(renderer, interface_texture, &sprite_info[SPR_PIXEL].src, &dest);
					}
				}
			}
		}
	}
}
