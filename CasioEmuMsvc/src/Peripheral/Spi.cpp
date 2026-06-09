#include "Spi.h"
#include "Peripheral.hpp"
#include <MMURegion.hpp>
#include <coroutine>
#include <future>
#include <optional>
#include <queue>

namespace casioemu {

	class Spi : public Peripheral, public ISpiProvider {
	public:
		MMURegion reg_SIO0BUF, reg_SIO0CON, reg_SIO0MOD;
		uint8_t buffer;	 // SIO0BUF - transmit/receive buffer
		uint8_t control; // SIO0CON - control register
		uint8_t mode0;	 // SIO0MOD0 - mode register 0
		uint8_t mode1;	 // SIO0MOD1 - mode register 1

		std::deque<uint8_t> rx_queue;

		using Peripheral::Peripheral;

		void Initialise() {
			reg_SIO0BUF.Setup(0xF280, 1, "Spi/SIO0BUF", this, SpiRead, SpiWrite, emulator);
			reg_SIO0CON.Setup(0xF282, 1, "Spi/SIO0CON", this, SpiRead, SpiWrite, emulator);
			reg_SIO0MOD.Setup(0xF284, 2, "Spi/SIO0MOD", this, SpiRead, SpiWrite, emulator);

			Reset();
		}
		virtual void* QueryInterface(const char* str) {
			if (strcmp(str, typeid(ISpiProvider).name()) == 0)
				return (ISpiProvider*)this;
			return 0;
		}
		static uint8_t SpiRead(MMURegion* reg, size_t off) {
			return ((Spi*)reg->userdata)->SpiSfrRead(off);
		}

		static void SpiWrite(MMURegion* reg, size_t off, uint8_t dat) {
			((Spi*)reg->userdata)->SpiSfrWrite(off, dat);
		}

		uint8_t SpiSfrRead(size_t off) {
			switch (off) {
			case 0xF280: // SIO0BUF
				return buffer;
			case 0xF282: // SIO0CON
				return control;
			case 0xF284: // SIO0MOD0
				return mode0;
			case 0xF285: // SIO0MOD1
				return mode1;
			default:
				return 0xFF;
			}
		}
		std::function<void(uint8_t)> recvHandler; // function to handle receiving data

		void SetRecvHandler(std::function<void(uint8_t)> handler) override {
			recvHandler = handler;
		}
		void Send(uint8_t d) override {
			rx_queue.push_back(d);
		}

		void SpiSfrWrite(size_t off, uint8_t dat) {
			switch (off) {
			case 0xF280: // SIO0BUF
			{
				if (control && (mode0 & 0b100)) {
					if (recvHandler) {
						recvHandler(dat);
						control = 0;
					}
				}
			}
				break;
			case 0xF282: // SIO0CON
				control = dat & 0x01;
				break;
			case 0xF284: // SIO0MOD0
				mode0 = dat & 0x0F;
				break;
			case 0xF285: // SIO0MOD1
				mode1 = dat & 0x1F;
				break;
			}
		}

		void Reset() {
			buffer = 0x00;
			control = 0x00;
			mode0 = 0x00;
			mode1 = 0x00;
		}

		void Tick() {
			if (control && (mode0 & 0b010)) { // Recv
				if (!rx_queue.empty()) {
					buffer = rx_queue.front();
					rx_queue.pop_front();
					control = 0;
				}
			}
		}
	};

	Peripheral* CreateSpi(Emulator& emu) {
		return new Spi(emu);
	}

} // namespace casioemu