#pragma once
#include "../Config.hpp"

#include <string>
#include "SpriteInfo.hpp"
#include "ColourInfo.hpp"

namespace casioemu
{
	class Emulator;

	struct ModelInfo
	{
		ModelInfo(Emulator& emulator, std::string key);
		Emulator& emulator;
		std::string key;
		SpriteInfo AsSpriteInfo();
		ColourInfo AsColourInfo();
		operator std::string();
		operator int();
		operator SpriteInfo() {
			return AsSpriteInfo();
		}
		operator ColourInfo() {
			return AsColourInfo();
		}
	};
}

