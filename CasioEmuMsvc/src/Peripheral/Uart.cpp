#include "Peripheral.hpp"
#include <MMURegion.hpp>
#include <iostream>
#include <queue>

namespace casioemu {
	class Uart : public Peripheral {
	public:
		MMURegion region_UA0BUF, region_UA0CON, region_UA0MOD0, region_UA0MOD1, region_UA0BRTH, region_UA0STAT;
		uint8_t uart_control{}, uart_mod0{}, uart_mod1{};
		uint16_t uart_baud{};
		uint8_t uart_buf{};
		uint8_t uart_status{};

		using Peripheral::Peripheral;

		void Initialise() {
			region_UA0BUF.Setup(0xF290, 1, "Uart0/Buffer", this, UartRead, UartWrite, emulator);
			region_UA0CON.Setup(0xF291, 1, "Uart0/Control", &uart_control,
				MMURegion::DefaultRead<uint8_t, 0x1>, MMURegion::DefaultWrite<uint8_t, 0x1>, emulator);
			region_UA0MOD0.Setup(0xF292, 1, "Uart0/Mode0", &uart_mod0,
				MMURegion::DefaultRead<uint8_t, 0b10111>, MMURegion::DefaultWrite<uint8_t, 0b10111>, emulator);
			region_UA0MOD1.Setup(0xF293, 1, "Uart0/Mode1", &uart_mod1,
				MMURegion::DefaultRead<uint8_t, 0x7f>, MMURegion::DefaultWrite<uint8_t, 0x7f>, emulator);
			region_UA0BRTH.Setup(0xF294, 2, "Uart0/Baud", &uart_baud,
				MMURegion::DefaultRead<uint16_t, 0b111111111111>,
				MMURegion::DefaultWrite<uint16_t, 0b111111111111>, emulator);
			region_UA0STAT.Setup(
				0xF296, 1, "Uart0/Status", &uart_status,
				MMURegion::DefaultRead<uint8_t>,
				[](MMURegion* region, size_t, uint8_t data) {
					*static_cast<uint8_t*>(region->userdata) = 0;
				},
				emulator);
		}

		static uint8_t UartRead(MMURegion* reg, size_t off) {
			return static_cast<Uart*>(reg->userdata)->UartDataRead();
		}

		static void UartWrite(MMURegion* reg, size_t off, uint8_t dat) {
			static_cast<Uart*>(reg->userdata)->UartDataWrite(dat);
		}

		std::queue<char> input;

		uint8_t UartDataRead() {
			if ((uart_mod0 & 1) && uart_control) {
				if (!input.empty()) {
					uint8_t data = input.front();
					input.pop();
					return data;
				}
			}
			return 0;
		}

		void UartDataWrite(uint8_t dat) {
			if ((uart_mod0 & 1) == 0 && uart_control) {
				std::cout.put(static_cast<char>(dat));
				std::cout.flush(); // 确保立即输出
			}
		}

		void Reset() {
			while (!input.empty()) {
				input.pop();
			}
			uart_status = 0;
			uart_control = 0;
			uart_mod0 = 0;
			uart_mod1 = 0;
			uart_baud = 0;
			uart_buf = 0;
		}

		void Tick() {
			// 检查是否有标准输入
			if (std::cin.rdbuf()->in_avail() > 0) {
				char c;
				if (std::cin.get(c)) {
					input.push(c);
				}
			}

			// 更新状态位
			if (uart_mod0 & 1) {					  // 接收模式
				uart_status = !input.empty() ? 1 : 0; // 有数据可读时置位
			}
			else {				 // 发送模式
				uart_status = 0; // 始终可写
			}
		}
	};

	Peripheral* CreateUart(Emulator& emu) {
		return new Uart(emu);
	}
} // namespace casioemu