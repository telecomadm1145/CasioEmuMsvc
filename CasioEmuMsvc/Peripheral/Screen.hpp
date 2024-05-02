#pragma once
#include "../Config.hpp"

#include "Peripheral.hpp"

namespace casioemu
{
	class ScreenBase : public Peripheral {
	public:
		using Peripheral::Peripheral;
		virtual void RenderScreen(SDL_Renderer*, SDL_Surface*, int ink_alpha_off, bool clear_dots, int ink_alpha_on) = 0;
	};
	extern ScreenBase* m_screen;
	Peripheral* CreateScreen(Emulator& emulator);
}

