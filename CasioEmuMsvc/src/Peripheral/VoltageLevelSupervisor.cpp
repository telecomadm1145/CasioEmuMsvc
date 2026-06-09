#include "VoltageLevelSupervisor.h"
#include "Peripheral.hpp"
#include "MMURegion.hpp"
namespace casioemu {
	class VoltageLevelSupervisor : public Peripheral {
		using Peripheral::Peripheral;
		MMURegion vlscon{}, vlsmod{};
		void Initialise() override {

		}
		void Tick() override {

		}
	};
} // namespace casioemu
