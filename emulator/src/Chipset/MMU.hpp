#pragma once
#include "../Config.hpp"

#include "MMURegion.hpp"

#include <cstdint>
#include <string>

namespace casioemu
{
	class Emulator;

	class MMU
	{
	private:
		Emulator &emulator;

		bool real_hardware;

		struct MemoryByte
		{
			MMURegion *region;
			/**
			 * Lua index to a function to execute when this byte is read from
			 * or written to as data. If this is LUA_REFNIL, no function is executed.
			 */
			int on_read, on_write;
		};
		MemoryByte **segment_dispatch;

	public:
		MMU(Emulator &emulator);
		~MMU();
		void SetupInternals();
		void GenerateSegmentDispatch(size_t segment_index);
		uint16_t ReadCode(size_t offset);
		uint8_t ReadData(size_t offset, bool softwareRead = true);
		void WriteData(size_t offset, uint8_t data, bool softwareWrite = true);
		size_t getRealOffset(size_t offset);

		void RegisterRegion(MMURegion *region);
		void UnregisterRegion(MMURegion *region);
	};
}

