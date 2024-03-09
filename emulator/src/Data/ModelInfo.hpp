#pragma once
#include "../Config.hpp"

#include <string>
#include "SpriteInfo.hpp"
#include "ColourInfo.hpp"

namespace casioemu
{
	class Emulator;
	//class SpriteInfo;
	//class ColourInfo;

	struct ModelInfo
	{
		ModelInfo(Emulator &emulator, std::string key);
		Emulator &emulator;
		std::string key;

		operator std::string();
		operator int();
		operator SpriteInfo();
		operator ColourInfo();
	};
}

