#pragma once
#include "../Config.hpp"

#include "Peripheral.hpp"
#include "../Chipset/MMURegion.hpp"
#include "../Data/SpriteInfo.hpp"
#include "../Data/ColourInfo.hpp"

namespace casioemu
{
	class Screen : public Peripheral
	{
		MMURegion region_buffer, region_contrast, region_mode, region_range;
		uint8_t *screen_buffer, screen_contrast, screen_mode, screen_range;

	    SDL_Renderer *renderer;
	    SDL_Texture *interface_texture;

		enum Sprite
		{
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

		struct SpriteBitmap
		{
			const char *name;
			uint8_t mask, offset;
		};

		static SpriteBitmap sprite_bitmap[];
		SpriteInfo sprite_info[SPR_MAX];
		ColourInfo ink_colour;

		/**
		 * Similar to MMURegion::DefaultRead, but takes the pointer to the Screen
		 * object as the userdata instead of the uint8_t member.
		 */
		template<typename value_type, value_type mask = (value_type)-1,
			value_type Screen:: *member_ptr>
		static uint8_t DefaultRead(MMURegion *region, size_t offset)
		{
			auto this_obj = (Screen *)(region->userdata);
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
		template<typename value_type, value_type mask = (value_type)-1,
			value_type Screen:: *member_ptr, bool only_on_change = true>
		static void SetRequireFrameWrite(MMURegion *region, size_t offset, uint8_t data)
		{
			auto this_obj = (Screen *)(region->userdata);
			value_type &value = this_obj->*member_ptr;

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
		using Peripheral::Peripheral;

		void Initialise();
		void Uninitialise();
		void Frame();
	};
}

