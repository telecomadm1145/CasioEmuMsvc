#include "ML620Ports.h"
#include "Chipset.hpp"
#include "Config.hpp"
#include "Emulator.hpp"
#include "MMURegion.hpp"
#include "Peripheral.hpp"
#define DefSfr(x)        \
	MMURegion reg_##x{}; \
	uint8_t dat_##x{};

#define DefSfr2(x)       \
	MMURegion reg_##x{}; \
	uint16_t dat_##x{};

#define DefSfr4(x)       \
	MMURegion reg_##x{}; \
	uint32_t dat_##x{};

namespace casioemu {
	class ML620Port {
		DefSfr(data);
		DefSfr(dir);
		DefSfr2(mode0);
		DefSfr2(mode1);
		DefSfr2(con);

		DefSfr4(exicon);

		DefSfr(ie);
		DefSfr(is);
		MMURegion reg_ic{};
		MMURegion reg_iu{};

		// 端口电平
		uint8_t PortLevel{};

	public:
		// 端口电平输入
		uint8_t PortInput{};
		// 端口电平输入是否存在(悬空)
		uint8_t PortInputExists{};

	private:
		uint8_t PortInputOld{};

		class Ports& pt;
		int ind;

		uint8_t TriggerWhenRise{};
		uint8_t TriggerWhenFall{};
		uint8_t SamplingMode{};

	public:
		ML620Port(Emulator& emulator, Ports& pt, int i) : pt(pt), ind(i) {
			auto pbase = 0xf210 + i * 8;
			reg_data.Setup(
				pbase++, 1, "PortN/Data", this,
				[](MMURegion* reg, size_t offset) -> uint8_t {
					auto p = (ML620Port*)reg->userdata;
					return p->PortLevel;
				},
				[](MMURegion* reg, size_t offset, uint8_t data) {
					auto p = (ML620Port*)reg->userdata;
					p->dat_data = data;
					p->UpdateStatus();
				},
				emulator);
			reg_dir.Setup(
				pbase++, 1, "PortN/Direction", this,
				[](MMURegion* reg, size_t offset) -> uint8_t {
					auto p = (ML620Port*)reg->userdata;
					return p->dat_dir;
				},
				[](MMURegion* reg, size_t offset, uint8_t data) {
					auto p = (ML620Port*)reg->userdata;
					p->dat_dir = data;
					p->UpdateStatus();
				},
				emulator);
			reg_con.Setup((pbase += 2) - 2, 2, "PortN/Control", &dat_con, MMURegion::DefaultRead<uint16_t>, MMURegion::DefaultWrite<uint16_t>, emulator);

			reg_mode0.Setup((pbase += 2) - 2, 2, "PortN/Mode01", &dat_mode0, MMURegion::DefaultRead<uint16_t>, MMURegion::DefaultWrite<uint16_t>, emulator);
			if (i == 3 || i == 5) {
				reg_mode1.Setup((pbase += 2) - 2, 2, "PortN/Mode23", &dat_mode1, MMURegion::DefaultRead<uint16_t>, MMURegion::DefaultWrite<uint16_t>, emulator);
			}
			if (i != 6) {
				pbase = 0xf980 + i * 8;
				if (i == 2) { // P2 only have 1
					reg_exicon.Setup((pbase += 4) - 4, 2, "PortN/ExI", this,
						[](MMURegion* reg, size_t offset) -> uint8_t {
							auto p = (ML620Port*)reg->userdata;
							uint32_t* value = (uint32_t*)(&p->dat_exicon);
							return (*value) >> ((offset - reg->base) * 8);
						},
						[](MMURegion* reg, size_t offset, uint8_t data) {
							auto p = (ML620Port*)reg->userdata;
							uint32_t* value = (uint32_t*)(&p->dat_exicon);
							*value &= ~(((uint32_t)0xFF) << ((offset - reg->base) * 8));
							*value |= ((uint32_t)data) << ((offset - reg->base) * 8);
							p->UpdateEXICON();
						},
						emulator);
				}
				else {
					reg_exicon.Setup((pbase += 4) - 4, 4, "PortN/ExI", this,
						[](MMURegion* reg, size_t offset) -> uint8_t {
							auto p = (ML620Port*)reg->userdata;
							uint32_t* value = (uint32_t*)(&p->dat_exicon);
							return (*value) >> ((offset - reg->base) * 8);
						},
						[](MMURegion* reg, size_t offset, uint8_t data) {
							auto p = (ML620Port*)reg->userdata;
							uint32_t* value = (uint32_t*)(&p->dat_exicon);
							*value &= ~(((uint32_t)0xFF) << ((offset - reg->base) * 8));
							*value |= ((uint32_t)data) << ((offset - reg->base) * 8);
							p->UpdateEXICON();
						},
						emulator);
				}
				// 启用 IS
				reg_ie.Setup(
					pbase++, 1, "PortN/Enable", this,
					[](MMURegion* reg, size_t off) -> uint8_t {
						auto p = (ML620Port*)reg->userdata;
						return p->dat_ie;
					},
					[](MMURegion* reg, size_t off, uint8_t dat) {
						auto p = (ML620Port*)reg->userdata;
						p->dat_ie |= dat;
						p->UpdateStatus();
					},
					emulator);
				// 这是只读寄存器 xd
				reg_is.Setup(pbase++, 1, "PortN/Status", &dat_is, MMURegion::DefaultRead<uint8_t>, MMURegion::IgnoreWrite, emulator);
				// 清除 IS
				reg_ic.Setup(
					pbase++, 1, "PortN/Clear", this, MMURegion::IgnoreRead<0>,
					[](MMURegion* reg, size_t off, uint8_t dat) {
						auto p = (ML620Port*)reg->userdata;
						p->dat_is &= ~dat;
						p->UpdateStatus();
					},
					emulator);
				reg_iu.Setup(
					pbase++, 1, "PortN/Update", this, MMURegion::IgnoreRead<0>,
					[](MMURegion* reg, size_t off, uint8_t dat) {
						auto p = (ML620Port*)reg->userdata;
						if (dat & 1)
							p->UpdateStatus();
					},
					emulator);
			}
		}
		void UpdateEXICON() {
			TriggerWhenRise = 0;
			TriggerWhenFall = 0;
			SamplingMode = 0;

			for (int n = 0; n < 8; ++n) {
				uint8_t im = (dat_exicon >> (n * 4)) & 0xF;
				uint8_t mask = 1 << n;

				switch (im & 0x7) {
				case 0: // Falling edge
					TriggerWhenFall |= mask;
					break;
				case 1: // Rising edge
					TriggerWhenRise |= mask;
					break;
				case 4: // Both edges
					TriggerWhenRise |= mask;
					TriggerWhenFall |= mask;
					break;
					// Other values are prohibited, so we ignore them
				}

				if (im & 0x8) {
					SamplingMode |= mask;
				}
			}
		}
		std::function<void(uint8_t)> output_callback;
		void UpdateStatus() {
			auto pi_tmp = PortInput & PortInputExists;
			auto con0 = dat_con & 0xff;
			auto con1 = dat_con >> 8;
			auto pulls = con0 ^ con1;
			pi_tmp |= (con1 & pulls) & ~PortInputExists;
			PortLevel = (dat_data & ~dat_dir) | (pi_tmp & dat_dir);
			if (output_callback)
				output_callback(dat_data & ~dat_dir);
			UpdateInterrupt(pi_tmp);
			PortInputOld = pi_tmp & dat_dir;
		}
		void SetOutputCallback(std::function<void(uint8_t)> callback) {
			output_callback = callback;
		}
		void UpdateInterrupt(int pi_tmp);
	};
	class Ports : public Peripheral, IPortProvider {
	public:
		ML620Port* ports[16]{};
		MMURegion ExiSelect{};
		uint8_t ExiSelect_d[8]{};
		MMURegion ExiCon{};
		uint8_t ExiCon_d[4]{};
		Ports(Emulator& emu) : Peripheral(emu) {
		}
		void Initialise() override {
			auto init = {0, 2, 3, 4, 5, 6, 7, 8};
			for (auto num : init) {
				ports[num] = new ML620Port{emulator, *this, num};
			}
			ExiSelect.Setup(
				0xF048, 8, "Ports/ExiSelect", this,
				[](MMURegion* reg, size_t offset) -> uint8_t {
					auto pt = (Ports*)reg->userdata;
					return pt->ExiSelect_d[offset & 0x7];
				},
				[](MMURegion* reg, size_t offset, uint8_t dat) {
					auto pt = (Ports*)reg->userdata;
					pt->ExiSelect_d[offset & 0x7] = dat;
				},
				emulator);
			ExiCon.Setup(
				0xF040, 4, "Ports/ExiCon", this,
				[](MMURegion* reg, size_t offset) -> uint8_t {
					auto pt = (Ports*)reg->userdata;
					return pt->ExiCon_d[offset & 0x3];
				},
				[](MMURegion* reg, size_t offset, uint8_t dat) {
					auto pt = (Ports*)reg->userdata;
					pt->ExiCon_d[offset & 0x3] = dat;
				},
				emulator);
		}
		bool PortExiSelect(int i) {
			for (size_t j = 0; j < 8; j++) {
				if (
					((ExiSelect_d[j] >> 4) == i) &&
					((ExiSelect_d[j] & 0xf) == 0x8)) {
					return 1;
				}
			}
			return 0;
		}
		void* QueryInterface(const char* name) override {
			if (strcmp(name, typeid(IPortProvider).name()) == 0) {
				return (IPortProvider*)this;
			}
			return 0;
		}
		void PortTriggerInterrupt(int i) {
			for (size_t j = 0; j < 8; j++) {
				if (((ExiSelect_d[j] >> 4) == i) &&
					((ExiSelect_d[j] & 0xf) == 0x8)) {
					emulator.chipset.MaskableInterrupts[7 + j].TryRaise();
				}
			}
		}
		void PortsTriggerInterrupt(int i, uint8_t Before, uint8_t After) {
			for (size_t j = 0; j < 8; j++) {
				if ((ExiSelect_d[j] >> 4) == i) {
					auto d = ExiSelect_d[j] & 0xf;
					if (d > 7)
						continue;
					auto rise = ExiCon_d[1] & (1 << j) ? (1 << d) : 0, fall = ExiCon_d[0] & (1 << j) ? (1 << d) : 0;
					auto it = ((After & (After ^ Before) & rise) | (Before & (After ^ Before) & fall));
					if (it)
						emulator.chipset.MaskableInterrupts[7 + j].TryRaise();
				}
			}
		}

		// 通过 IPortProvider 继承
		void SetPortOutputCallback(int port, std::function<void(uint8_t new_output)> callback) override {
			if (port >= 0 && port < 16 && ports[port]) {
				ports[port]->SetOutputCallback(callback);
			}
		}

		void SetPortInput(int port, uint8_t input, uint8_t input_mask) override {
			if (port >= 0 && port < 16 && ports[port]) {
				ports[port]->PortInput = input;
				ports[port]->PortInputExists = input_mask;
				ports[port]->UpdateStatus();
			}
		}
	};
	void ML620Port::UpdateInterrupt(int pi_tmp) {
		if (pt.PortExiSelect(ind)) {
			auto it = ((pi_tmp & (pi_tmp ^ PortInputOld) & TriggerWhenRise) | (PortInputOld & (pi_tmp ^ PortInputOld) & TriggerWhenFall)) & dat_ie;
			if (it) {
				dat_is = it;
				dat_ie = 0;
				pt.PortTriggerInterrupt(ind);
			}
		}
		else {
			pt.PortsTriggerInterrupt(ind, PortInputOld, pi_tmp);
		}
	}
	Peripheral* CreateML620Ports(Emulator& emu) {
		return new Ports(emu);
	}
} // namespace casioemu