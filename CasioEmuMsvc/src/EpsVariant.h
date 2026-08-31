#pragma once

#include <cstdint>

	namespace casioemu {
	enum class EpsVariant : uint8_t {
		None,
		Eps6800,
		Eps6009,
		Eps9500,
		Eps6800W192
	};
}
