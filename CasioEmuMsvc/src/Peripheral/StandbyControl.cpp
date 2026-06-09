#include "StandbyControl.hpp"

#include "Chipset/CPU.hpp"
#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Emulator.hpp"
#include "Logger.hpp"

namespace casioemu {
	class StandbyControl : public Peripheral {
		MMURegion region_stpacp, region_sbycon, region_F312;
		uint8_t stpacp_last, F312_last;
		bool stop_acceptor_enabled, shutdown_acceptor_enabled;

	public:
		using Peripheral::Peripheral;

		void Initialise();
		void Reset();
	};
	void StandbyControl::Initialise() {
		region_stpacp.Setup(
			0xF008, 1, "StandbyControl/STPACP", this, MMURegion::IgnoreRead<0x00>, [](MMURegion* region, size_t, uint8_t data) {
				StandbyControl* self = (StandbyControl*)(region->userdata);
				if ((data & 0xF0) == 0xA0 && (self->stpacp_last & 0xF0) == 0x50)
					self->stop_acceptor_enabled = true;
				self->stpacp_last = data;
			},
			emulator);

		region_sbycon.Setup(
			0xF009, 1, "StandbyControl/SBYCON", this, MMURegion::IgnoreRead<0x00>, [](MMURegion* region, size_t, uint8_t data) {
				StandbyControl* self = (StandbyControl*)(region->userdata);

				if (data & 0x01) {
					self->emulator.chipset.Halt();
					return;
				}

				if (data & 0x02 && self->stop_acceptor_enabled) {
					self->stop_acceptor_enabled = false;
					self->emulator.chipset.Stop();
					return;
				}
				if (self->emulator.hardware_id == HW_TI) { // TODO: DEEP_HALT
					if (data & 0x04) {
						self->emulator.chipset.Halt();
						return;
					}
					if (data & 0x08) {
						self->emulator.chipset.Halt();
						return;
					}
				}
			},
			emulator);

		if (emulator.hardware_id == HW_CLASSWIZ_II) {
			region_F312.Setup(
				0xF312, 1, "StandbyControl/F312", this, MMURegion::IgnoreRead<0x00>, [](MMURegion* region, size_t, uint8_t data) {
					StandbyControl* self = (StandbyControl*)(region->userdata);
					if (data == 0x3C && self->F312_last == 0x5A)
						self->shutdown_acceptor_enabled = true;
					self->F312_last = data;

					if (self->shutdown_acceptor_enabled && (data & 0xF0) == 0) {

						/*
						* TODO:
						如果当前有cpu尚未处理的中断的话，F312介导的关机会暂时挂起
						当相应的IE/IRQ bit被清除之后才会触发关机
						*/
						self->emulator.chipset.mmu.WriteData(0xF031, 0x03, false);
						self->emulator.chipset.Stop();
						for (int i = 0; i < 4; i++)
							self->emulator.chipset.mmu.WriteData(0xF010 + i, 00);
						self->shutdown_acceptor_enabled = false;
					}
				},
				emulator);
		}
	}

	void StandbyControl::Reset() {
		stpacp_last = 0;
		F312_last = 0;
		stop_acceptor_enabled = false;
		shutdown_acceptor_enabled = false;
	}
	Peripheral* CreateStbCtrl(Emulator& emu) {
		return new StandbyControl(emu);
	}
} // namespace casioemu
