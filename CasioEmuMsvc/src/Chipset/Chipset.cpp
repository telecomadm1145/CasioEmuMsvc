#include "Chipset.hpp"

#include "5800Flash.h"
#include "Audio.h"
#include "BCDCalc.hpp"
#include "BatteryBackedRAM.hpp"
#include "CPU.hpp"
#include "Emulator.hpp"
#include "ExternalInterrupts.hpp"
#include "Flash.hpp"
#include "Hooks.h"
#include "IOPorts.hpp"
#include "InterruptSource.hpp"
#include "Keyboard.hpp"
#include "Logger.hpp"
#include "MMU.hpp"
#include "Miscellaneous.hpp"
#include "ModelInfo.h"
#include "Models.h"
#include "Peripheral/SD/FakeSdCard.h"
#include "PowerSupply.hpp"
#include "ROMWindow.hpp"
#include "RealTimeClock.hpp"
#include "Romu.h"
#include "Screen.hpp"
#include "StandbyControl.hpp"
#include "Timer.hpp"
#include "TimerBaseCounter.hpp"
#include "Uart.h"
#include "WatchdogTimer.hpp"
#include "ePSCpu.h"
#include <ML620Ports.h>
#include <Spi.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>

namespace casioemu {
	constexpr uint32_t EPS_RAM_SAVE_INTERVAL_MS = 10 * 1000;

	static bool ShouldPersistEpsRam(HardwareId hardware_id) {
		return hardware_id == HW_EPS6800;
	}

	void* Chipset::QueryInterface(const char* name) {
		auto d = (void*)0;
		for (auto& phe : peripherals) {
			if ((d = phe->QueryInterface(name)))
				return d;
		}
		return nullptr;
	}
	Chipset::Chipset(Emulator& _emulator) : emulator(_emulator), cpu(*new CPU(emulator)), mmu(*new MMU(emulator)) {
		tiDiagMode = false;
		tiKey = 0;
	}

	void Chipset::SetupEpsCpu() {
		epscpu = new ePSCPU(emulator.hardware_id == HW_EPS6009 ? EpsVariant::Eps6009 : EpsVariant::Eps6800);
		auto parse_byte_extra = [&](const char* name, uint8_t fallback) {
			const auto item = emulator.ModelDefinition.extra.find(name);
			if (item == emulator.ModelDefinition.extra.end())
				return fallback;
			try {
				size_t consumed = 0;
				const auto value = std::stoul(item->second, &consumed, 0);
				if (consumed != item->second.size() || value > 0xff)
					throw std::invalid_argument("invalid byte");
				return static_cast<uint8_t>(value);
			}
			catch (const std::exception&) {
				PANIC("Invalid EPS6800 %s value: %s\n", name, item->second.c_str());
			}
			return fallback;
		};
		const uint8_t port_c_input_mask = parse_byte_extra("port_c_input_mask", 0);
		const uint8_t port_c_input_value = parse_byte_extra("port_c_input_value", 0);
		epscpu->SetPortCInput(port_c_input_mask, port_c_input_value);
		const auto ice_timer_entry = emulator.ModelDefinition.extra.find("ice_timer_scheduling");
		const bool ice_timer_scheduling = ice_timer_entry != emulator.ModelDefinition.extra.end() &&
			ice_timer_entry->second != "0" && ice_timer_entry->second != "false";
		epscpu->SetIceTimerScheduling(ice_timer_scheduling);
		const auto timer_divisor = emulator.ModelDefinition.extra.find("timer_cycle_divisor");
		if (timer_divisor != emulator.ModelDefinition.extra.end()) {
			try {
				size_t consumed = 0;
				const auto value = std::stoul(timer_divisor->second, &consumed, 0);
				if (consumed != timer_divisor->second.size() || value == 0)
					throw std::invalid_argument("invalid timer divisor");
				epscpu->SetTimerCycleDivisor(static_cast<uint32_t>(value));
			}
			catch (const std::exception&) {
				PANIC("Invalid EPS6800 timer_cycle_divisor value: %s\n", timer_divisor->second.c_str());
			}
		}
		else if (ice_timer_scheduling && emulator.eps_timer1_source_hz != 0) {
			const auto divisor = std::max(1u,
				static_cast<uint32_t>((emulator.cycles_per_second + (emulator.eps_timer1_source_hz / 2)) /
					emulator.eps_timer1_source_hz));
			epscpu->SetTimerCycleDivisor(divisor);
		}
		epscpu->SetDebugHooks(
			[](uint32_t pc_before, uint32_t pc_after, uint8_t stack_pointer) {
				InstructionEventArgs args{pc_before, pc_after};
				args.stack_pointer = stack_pointer;
				RaiseEvent(on_eps_instruction, args);
				return args.should_break;
			},
			[](uint32_t pc, uint32_t lr, bool call, uint32_t accumulator, const std::string& backtrace) {
				EpsFunctionEventArgs args{{pc, lr}, accumulator, backtrace};
				if (call) {
					RaiseEvent(on_eps_call_function, args);
				}
				else {
					RaiseEvent(on_eps_function_return, args);
				}
			},
			[](uint32_t address, uint8_t& value, bool write) {
				MemoryEventArgs args{address, false, value};
				if (write) {
					RaiseEvent(on_eps_memory_write, args);
				}
				else {
					RaiseEvent(on_eps_memory_read, args);
				}
				value = args.value;
				return args.handled;
			},
			[this](uint8_t index) {
				InterruptEventArgs args{index};
				RaiseEvent(on_eps_interrupt, *this, args);
			});
	}

	void Chipset::Setup() {
		for (size_t ix = 0; ix != INT_COUNT; ++ix)
			interrupts_active[ix] = false;
		pending_interrupt_count = 0;

		real_hardware = emulator.ModelDefinition.real_hardware;

		if (!IsEpsFamily(emulator.hardware_id)) {
			cpu.SetMemoryModel(emulator.hardware_id == HW_SOLARII ? CPU::MM_SMALL : CPU::MM_LARGE);
			cpu.SetCPUModel(emulator.hardware_id == HW_CLASSWIZ || emulator.hardware_id == HW_CLASSWIZ_II || emulator.hardware_id == HW_TI ? CPU::CM_NX_U16 : CPU::CM_NX_U8);

            std::initializer_list<int> segments_solar{0}, segments_es_plus{0, 1, 2, 8}, segments_fx_5800p{
                0, 1, 4, 8, 9, 10, 11, 12, 13, 14, 15
            }, segments_classwiz{0, 1, 2, 3, 4, 5}, segments_classwiz_ii{
                0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
            };
            for (auto segment_index: emulator.hardware_id == HW_SOLARII
                                         ? segments_solar
                                         : emulator.hardware_id == HW_ES_PLUS
                                               ? segments_es_plus
                                               : emulator.hardware_id == HW_FX_5800P
                                                     ? segments_fx_5800p
                                                     : emulator.hardware_id == HW_CLASSWIZ
                                                           ? segments_classwiz
                                                           : segments_classwiz_ii)
				mmu.GenerateSegmentDispatch(segment_index);
		}
		else {
			SetupEpsCpu();
		}
		ConstructPeripherals();
	}

	Chipset::~Chipset() {
		if (eps_ram_save_timer_id) {
			SDL_RemoveTimer(eps_ram_save_timer_id);
			eps_ram_save_timer_id = 0;
		}
		PersistEpsRam();
		DestructPeripherals();
		if (!IsEpsFamily(emulator.hardware_id)) {
			DestructClockGenerator();
			DestructInterruptSFR();
		}
		{
			/* SDL_RemoveTimer does not wait for a callback that is already
			 * running; acquire the save mutex so an in-flight PersistEpsRam can
			 * never outlive epscpu. */
			const std::lock_guard lock(eps_ram_save_mutex);
			delete epscpu;
			epscpu = nullptr;
		}
		delete& mmu;
		delete& cpu;
	}

	void Chipset::PersistEpsRam() {
	#ifdef CASIOEMU_DISABLE_RAM_IMAGE
		return;
	#else
		if (!ShouldPersistEpsRam(emulator.hardware_id))
			return;
		const std::lock_guard lock(eps_ram_save_mutex);
		if (!epscpu)
			return;
		try {
			emulator.WriteModelSessionResource("ram.dmp", epscpu->ExportRam());
			logger::Info("[EPS6800][Info] RAM image saved to ram.dmp\n");
		}
		catch (const std::exception& error) {
			logger::Info("[EPS6800][Warn] Failed to save RAM image: %s\n", error.what());
		}
	#endif
	}

	bool Chipset::ReloadRom(std::string& error) {
		std::vector<unsigned char> data;
		try {
			data = emulator.ReadModelResource(emulator.ModelDefinition.rom_path);
		}
		catch (const std::exception&) {
			error = "Failed to open ROM file";
			return false;
		}

		if (epscpu) {
			if (!epscpu->LoadRom(data, epscpu->RomFormat())) {
				error = "Invalid EPS6800 ROM image";
				return false;
			}
			rom_data = std::move(data);
		}
		else {
			std::copy_n(data.begin(), std::min(data.size(), rom_data.size()), rom_data.begin());
		}

		Reset();
		error.clear();
		return true;
	}

	void Chipset::ConstructInterruptSFR() {
		if (emulator.hardware_id == HW_TI) {
			WDT_enabled = true;
			EffectiveMICount = 59;
			MaskableInterrupts = new InterruptSource[59];
			// ML620Q418A EXInINT
			for (size_t i = 0; i < 7; i++)
				MaskableInterrupts[i].Setup(5, emulator);
			for (size_t i = 0; i < 8; i++)
				MaskableInterrupts[7 + i].Setup(5 + 3 + i, emulator);
			for (size_t i = 15; i < 55; i++)
				MaskableInterrupts[i].Setup(5, emulator);
			MaskableInterrupts[55].Setup(53 + 3, emulator);
			MaskableInterrupts[56].Setup(54 + 3, emulator);
			MaskableInterrupts[57].Setup(55 + 3, emulator);
			for (size_t i = 58; i < 59; i++)
				MaskableInterrupts[i].Setup(5, emulator);
			region_int_mask.Setup(
				0xF010, 8, "Chipset/InterruptMask", this,
				[](MMURegion* region, size_t offset) {
					offset -= region->base;
					Chipset* chipset = (Chipset*)region->userdata;
					return (uint8_t)((chipset->data_int_mask >> (offset * 8)) & 0xFF);
				},
				[](MMURegion* region, size_t offset, uint8_t data) {
					offset -= region->base;
					Chipset* chipset = (Chipset*)region->userdata;
					size_t mask = (static_cast<size_t>(1) << (chipset->EffectiveMICount + 1)) - (chipset->WDT_enabled ? 1 : 2);
					chipset->data_int_mask = (chipset->data_int_mask & (~(static_cast<unsigned long long>(0xFF) << (offset * 8)))) | (static_cast<unsigned long long>(data) << (offset * 8));
					chipset->data_int_mask &= mask;
					for (size_t i = 0; i < chipset->EffectiveMICount; i++) {
						chipset->MaskableInterrupts[i].SetEnabled(chipset->data_int_mask & (static_cast<unsigned long long>(1) << (i + 1)));
					}
					if (chipset->data_int_mask & 1) {
						if (chipset->GetInterruptPendingSFR(4))
							chipset->RaiseNonmaskable();
					}
					else {
						chipset->ResetNonmaskable();
					}
				},
				emulator);
			region_int_pending.Setup(
				0xF010 + 0x8, 0x8, "Chipset/InterruptPending", this,
				[](MMURegion* region, size_t offset) {
					offset -= region->base;
					Chipset* chipset = (Chipset*)region->userdata;
					return (uint8_t)((chipset->data_int_pending >> (offset * 8)) & 0xFF);
				},
				[](MMURegion* region, size_t offset, uint8_t data) {
					offset -= region->base;
					Chipset* chipset = (Chipset*)region->userdata;
					size_t mask = (1 << (chipset->EffectiveMICount + 1)) - (chipset->WDT_enabled ? 1 : 2);
					chipset->data_int_pending = (chipset->data_int_pending & (~(0xFF << (offset * 8)))) | (data << (offset * 8));
					chipset->data_int_pending &= mask;
					for (size_t i = 0; i < chipset->EffectiveMICount; i++) {
						if (chipset->data_int_pending & (static_cast<unsigned long long>(1) << (i + 1)))
							chipset->MaskableInterrupts[i].TryRaise();
						else
							chipset->MaskableInterrupts[i].ResetInt();
					}
					if (chipset->data_int_pending & 1) {
						if (chipset->data_int_mask & 1)
							chipset->RaiseNonmaskable();
					}
					else {
						chipset->ResetNonmaskable();
					}
				},
				emulator);
			return;
		}
		EffectiveMICount = emulator.hardware_id == HW_ES_PLUS ? 12 : emulator.hardware_id == HW_CLASSWIZ ? 17
			: 21;
		MaskableInterrupts = new InterruptSource[EffectiveMICount];
		for (size_t i = 0; i < EffectiveMICount; i++) {
			MaskableInterrupts[i].Setup(i + INT_MASKABLE, emulator);
		}
		isMIBlocked = false;

		// WDTINT is unused
		auto mask_len = 4;
		region_int_mask.Setup(
			0xF010, mask_len, "Chipset/InterruptMask", this,
			[](MMURegion* region, size_t offset) {
				offset -= region->base;
				Chipset* chipset = (Chipset*)region->userdata;
				return (uint8_t)((chipset->data_int_mask >> (offset * 8)) & 0xFF);
			},
			[](MMURegion* region, size_t offset, uint8_t data) {
				offset -= region->base;
				Chipset* chipset = (Chipset*)region->userdata;
				size_t mask = (static_cast<size_t>(1) << (chipset->EffectiveMICount + 1)) - (chipset->WDT_enabled ? 1 : 2);
				chipset->data_int_mask = (chipset->data_int_mask & (~(static_cast<unsigned long long>(0xFF) << (offset * 8)))) | (static_cast<unsigned long long>(data) << (offset * 8));
				chipset->data_int_mask &= mask;
				for (size_t i = 0; i < chipset->EffectiveMICount; i++) {
					chipset->MaskableInterrupts[i].SetEnabled(chipset->data_int_mask & (static_cast<unsigned long long>(1) << (i + 1)));
				}
				if (chipset->data_int_mask & 1) {
					if (chipset->GetInterruptPendingSFR(4))
						chipset->RaiseNonmaskable();
				}
				else {
					chipset->ResetNonmaskable();
				}
			},
			emulator);

		region_int_pending.Setup(
			0xF010 + mask_len, mask_len, "Chipset/InterruptPending", this,
			[](MMURegion* region, size_t offset) {
				offset -= region->base;
				Chipset* chipset = (Chipset*)region->userdata;
				return (uint8_t)((chipset->data_int_pending >> (offset * 8)) & 0xFF);
			},
			[](MMURegion* region, size_t offset, uint8_t data) {
				offset -= region->base;
				Chipset* chipset = (Chipset*)region->userdata;
				size_t mask = (1 << (chipset->EffectiveMICount + 1)) - (chipset->WDT_enabled ? 1 : 2);
				chipset->data_int_pending = (chipset->data_int_pending & (~(0xFF << (offset * 8)))) | (data << (offset * 8));
				chipset->data_int_pending &= mask;
				for (size_t i = 0; i < chipset->EffectiveMICount; i++) {
					if (chipset->data_int_pending & (static_cast<unsigned long long>(1) << (i + 1)))
						chipset->MaskableInterrupts[i].TryRaise();
					else
						chipset->MaskableInterrupts[i].ResetInt();
				}
				if (chipset->data_int_pending & 1) {
					if (chipset->data_int_mask & 1)
						chipset->RaiseNonmaskable();
				}
				else {
					chipset->ResetNonmaskable();
				}
			},
			emulator);
	}

	void Chipset::ResetInterruptSFR() {
		data_int_mask = 0;
		data_int_pending = 0;
		for (size_t i = 0; i < EffectiveMICount; i++) {
			MaskableInterrupts[i].SetEnabled(false);
			MaskableInterrupts[i].ResetInt();
		}
		ResetNonmaskable();
	}

	void Chipset::DestructInterruptSFR() {
		region_int_pending.Kill();
		region_int_mask.Kill();
	}

	void Chipset::ConstructClockGenerator() {
		LSCLKFreq = 16384;

		ResetClockGenerator();
		if (emulator.hardware_id == HW_TI) {
			region_FCON.Setup(
				0xF002, 1, "ClockGenerator/FCON0", this,
				[](MMURegion* region, size_t) {
					Chipset* chipset = (Chipset*)region->userdata;
					return chipset->data_FCON;
				},
				[](MMURegion* region, size_t, uint8_t data) {
					Chipset* chipset = (Chipset*)region->userdata;
					uint8_t OSCLK = data & 0x7;
					chipset->data_FCON = data & 0b11111;
					chipset->ClockDiv = static_cast<int>(std::pow(2, OSCLK == 0 ? OSCLK : OSCLK - 1));
					// chipset->LSCLKMode = (chipset->data_FCON & 0x03) == 1 ? true : false;
				},
				emulator);
			region_FCON1.Setup(
				0xF003, 1, "ClockGenerator/FCON1", this,
				[](MMURegion* region, size_t) {
					Chipset* chipset = (Chipset*)region->userdata;
					return chipset->data_FCON1;
				},
				[](MMURegion* region, size_t, uint8_t data) {
					Chipset* chipset = (Chipset*)region->userdata;
					chipset->data_FCON1 = data & 0b11010111;
					chipset->LSCLKMode = chipset->data_FCON & 0x1;
				},
				emulator);
			region_LTBR.Setup(
				0xf060, 1, "TimerBaseCounter/LTBR", this,
				[](MMURegion* region, size_t) {
					Chipset* chipset = (Chipset*)region->userdata;
					return chipset->data_LTBR;
				},
				[](MMURegion* region, size_t, uint8_t data) {
					Chipset* chipset = (Chipset*)region->userdata;
					chipset->data_LTBR = 0;
					chipset->LTBCReset = true;
					chipset->LSCLKTick = true;
					chipset->LSCLKTickCounter = 0;
					chipset->LSCLKTimeCounter = 0;
					chipset->LSCLKFreqAddition = 0;
				},
				emulator);
			region_LTBADJ.Setup(
				0xF062, 2, "TimerBaseCounter/LTBADJ", this,
				[](MMURegion* region, size_t offset) {
					Chipset* chipset = (Chipset*)region->userdata;
					offset -= region->base;
					return (uint8_t)((chipset->data_LTBADJ & 0x7FF) >> offset * 8);
				},
				[](MMURegion* region, size_t offset, uint8_t data) {
					Chipset* chipset = (Chipset*)region->userdata;
					offset -= region->base;
					chipset->data_LTBADJ = (chipset->data_LTBADJ & (~(0xFF << offset * 8))) | (data << offset * 8);
					chipset->data_LTBADJ &= 0x7FF;
					if (chipset->data_LTBADJ != 0)
						chipset->LSCLKThresh = (chipset->LSCLKFreq * (1 + 2097152 / (short)chipset->data_LTBADJ)) / chipset->emulator.GetCyclesPerSecond();
					else
						chipset->LSCLKThresh = 0;
				},
				emulator);
		}
		else {
			region_FCON.Setup(
				0xF00A, 1, "ClockGenerator/FCON", this, [](MMURegion* region, size_t) {
					Chipset* chipset = (Chipset*)region->userdata;
					return chipset->data_FCON; }, [](MMURegion* region, size_t, uint8_t data) {
						Chipset* chipset = (Chipset*)region->userdata;
						uint8_t OSCLK = (data & 0x70) >> 4;
						chipset->data_FCON = data & 0x73;
						chipset->ClockDiv = static_cast<int>(std::pow(2, OSCLK == 0 ? OSCLK : OSCLK - 1));
						chipset->LSCLKMode = (chipset->data_FCON & 0x03) == 1 ? true : false; }, emulator);
			region_LTBR.Setup(
				0xF00C, 1, "TimerBaseCounter/LTBR", this, [](MMURegion* region, size_t) {
					Chipset* chipset = (Chipset*)region->userdata;
					return chipset->data_LTBR; }, [](MMURegion* region, size_t, uint8_t data) {
						Chipset* chipset = (Chipset*)region->userdata;
						chipset->data_LTBR = 0;
						chipset->LTBCReset = true;
						chipset->LSCLKTick = true;
						chipset->LSCLKTickCounter = 0;
						chipset->LSCLKTimeCounter = 0;
						chipset->LSCLKFreqAddition = 0; }, emulator);
			region_HTBR.Setup(
				0xF00D, 1, "ClockGenerator/HTBR", this, [](MMURegion* region, size_t) {
					Chipset* chipset = (Chipset*)region->userdata;
					return chipset->data_HTBR; }, [](MMURegion* region, size_t, uint8_t data) {
						Chipset* chipset = (Chipset*)region->userdata;
						chipset->data_HTBR = 0;
						chipset->HSCLK_output = 0xFF;
						chipset->HTBCReset = true;
						chipset->HSCLKTick = true;
						chipset->HSCLKTickCounter = 0; }, emulator);
			region_LTBADJ.Setup(
				0xF006, 2, "TimerBaseCounter/LTBADJ", this, [](MMURegion* region, size_t offset) {
					Chipset* chipset = (Chipset*)region->userdata;
					offset -= region->base;
					return (uint8_t)((chipset->data_LTBADJ & 0x7FF) >> offset * 8); }, [](MMURegion* region, size_t offset, uint8_t data) {
						Chipset* chipset = (Chipset*)region->userdata;
						offset -= region->base;
						chipset->data_LTBADJ = (chipset->data_LTBADJ & (~(0xFF << offset * 8))) | (data << offset * 8);
						chipset->data_LTBADJ &= 0x7FF;
						if (chipset->data_LTBADJ != 0)
							chipset->LSCLKThresh = (chipset->LSCLKFreq * (1 + 2097152 / (short)chipset->data_LTBADJ)) / chipset->emulator.GetCyclesPerSecond();
						else
							chipset->LSCLKThresh = 0; }, emulator);
		}
	}

	void Chipset::GenerateTickForClock() {
		// if (!real_hardware) {
		// if (++SYSCLKTickCounter >= 2) {
		//	SYSCLKTick = true;
		//	SYSCLKTickCounter = 0;
		// }
		// HSCLKTick = LSCLKTick = SYSCLKTick;
		// return;
		//}

		// Generate HSCLK Tick
		if (run_mode != RM_STOP) {
			if (++HSCLKTickCounter >= ClockDiv) {
				HSCLKTick = true;
				HSCLKTickCounter = 0;
				if (++SYSCLKTickCounter >= 2) {
					SYSCLKTick = true;
					SYSCLKTickCounter = 0;
				}
				if (HTBCReset) {
					HTBCReset = false;
				}
				else {
					HSCLK_output = 0;
					if (++HSCLKTimeCounter >= HTBROutputCount) {
						data_HTBR++;
						HSCLK_output = (data_HTBR - 1) & (~data_HTBR);
						HSCLKTimeCounter = 0;
					}
				}
			}
		}

		// Generate LSCLK Tick
		if (LSCLKMode) {
			if (++LSCLKTickCounter >= emulator.GetCyclesPerSecond() / LSCLKFreq + LSCLKFreqAddition) {
				LSCLKTick = true;
				LSCLKTickCounter = 0;
				if (LSCLKFreqAddition != 0) {
					LSCLKFreqAddition = 0;
				}
				if (LSCLKThresh > 0) {
					if (++LSCLKTimeCounter >= LSCLKThresh)
						LSCLKFreqAddition = 1;
				}
				else if (LSCLKThresh < 0) {
					if (++LSCLKTimeCounter >= -LSCLKThresh)
						LSCLKFreqAddition = -1;
				}
			}
		}
	}

	void Chipset::ResetClockGenerator() {
		data_FCON = 0;
		data_LTBR = 0;
		data_HTBR = 0;
		LSCLK_output = 0;
		HSCLK_output = 0;
		data_LTBADJ = 0;

		ClockDiv = 1;
		LSCLKMode = false;

		LSCLKTick = false;
		HSCLKTick = false;
		SYSCLKTick = false;
		LTBCReset = false;
		HTBCReset = false;

		LSCLKTickCounter = 0;
		LSCLKTimeCounter = 0;
		LSCLKFreqAddition = 0;
		LSCLKThresh = 0;
		HSCLKTickCounter = 0;
		HSCLKTimeCounter = 0;
		SYSCLKTickCounter = 0;
	}

	void Chipset::DestructClockGenerator() {
		region_FCON.Kill();
		region_LTBR.Kill();
		region_HTBR.Kill();
		region_LTBADJ.Kill();
	}

	void Chipset::ConstructPeripherals() {
		if (IsEpsFamily(emulator.hardware_id)) {
			peripherals.push_front(CreateScreen(emulator));
			peripherals.push_front(CreateKeyboard(emulator));
			return;
		}
		// Only tested on fx-991cnx
		if (emulator.hardware_id != HW_TI) {
			BLKCON_mask = emulator.hardware_id == HW_CLASSWIZ ? 0x1F : 0xFF;
			region_BLKCON.Setup(
				0xF028, 1, "Chipset/BLKCON0", this, [](MMURegion* region, size_t) {
					Chipset* chipset = (Chipset*)region->userdata;
					return (uint8_t)(chipset->data_BLKCON & chipset->BLKCON_mask); }, [](MMURegion* region, size_t, uint8_t data) {
						Chipset* chipset = (Chipset*)region->userdata;
						data &= chipset->BLKCON_mask;
						chipset->data_BLKCON = data;
						for (auto peripheral : chipset->peripherals) {
							int block_bit = peripheral->block_bit;
							if (block_bit == -1)
								continue;
							if ((1 << block_bit) > chipset->BLKCON_mask)
								PANIC("Invalid BLKCON0 bit %d\n", block_bit);
							if (data & (1 << block_bit))
								peripheral->Uninitialise();
							else
								peripheral->Initialise();
						} }, emulator);
		}

		ioport = new IOPorts(emulator);
		EXIhandle = new ExternalInterrupts(emulator);
		if (emulator.hardware_id != HW_TI) {
			peripherals.push_front(ioport);
			peripherals.push_front(EXIhandle);
		}
		peripherals.push_front(CreateRomWindow(emulator));
		peripherals.push_front(CreateBatteryBackedRAM(emulator));
		peripherals.push_front(CreateScreen(emulator));
		peripherals.push_front(CreateKeyboard(emulator));
		peripherals.push_front(CreateStbCtrl(emulator));
		peripherals.push_front(CreateMiscellaneous(emulator));
		if (emulator.hardware_id == HW_TI) {
			peripherals.push_front(CreateTimer(emulator));
			peripherals.push_front(CreateWatchdog(emulator));
			peripherals.push_front(CreateTimerBaseCounter(emulator));
			peripherals.push_front(CreateML620Ports(emulator));
		}
		else {
			peripherals.push_front(CreateTimer(emulator));
			if (emulator.hardware_id != HW_FX_5800P) // 0x100000
				peripherals.push_front(CreatePowerSupply(emulator));
			if (emulator.hardware_id == HW_FX_5800P)
				peripherals.push_front(CreateFx5800Flash(emulator));
			if (emulator.hardware_id == HW_CLASSWIZ_II) {
				peripherals.push_front(CreateUart(emulator));
			}
			peripherals.push_front(CreateBuzzerDriver(emulator));
			peripherals.push_front(CreateTimerBaseCounter(emulator));
			peripherals.push_front(CreateRtc(emulator));
			peripherals.push_front(CreateWatchdog(emulator));
			if (emulator.hardware_id == HW_CLASSWIZ_II) {
				peripherals.push_front(CreateBcdCalc(emulator));
				peripherals.push_front(CreateSpi(emulator));
			}
			if (emulator.hardware_id == HW_CLASSWIZ)
				peripherals.push_front(CreateFlash(emulator));
		}
		// auto spi = QueryInterface<ISpiProvider>();
		// if (spi)
		//	new FakeSdCard(spi);
	}

	void Chipset::DestructPeripherals() {
		region_BLKCON.Kill();

		for (auto& peripheral : peripherals) {
			peripheral->Uninitialise();
			delete peripheral;
		}
	}

	void Chipset::SetupInternals() {
		try {
			rom_data = emulator.ReadModelResource(emulator.ModelDefinition.rom_path);
		}
		catch (const std::exception& error) {
			PANIC("Failed to read ROM: %s\n", error.what());
		}
		if (IsEpsFamily(emulator.hardware_id)) {
			const auto unpacked_entry = emulator.ModelDefinition.extra.find("is_unpacked_nibbles");
			const bool is_unpacked_nibbles = unpacked_entry == emulator.ModelDefinition.extra.end() ||
				(unpacked_entry->second != "0" && unpacked_entry->second != "false");
			const auto rom_format = is_unpacked_nibbles ? Eps6800RomFormat::UnpackedNibbles :
				Eps6800RomFormat::PackedLittleEndian;
			if (!epscpu || !epscpu->LoadRom(rom_data, rom_format))
				printf("Invalid EPS6800 ROM for configured format %s\n", Eps6800RomFormatName(rom_format));
		#ifndef CASIOEMU_DISABLE_RAM_IMAGE
			if (ShouldPersistEpsRam(emulator.hardware_id) && emulator.HasModelResource("ram.dmp")) {
				try {
					const auto saved_ram = emulator.ReadModelResource("ram.dmp");
					if (!epscpu->ImportRam(saved_ram))
						logger::Info("[EPS6800][Warn] Ignoring ram.dmp with size %zu (expected 8192 or 8219)\n", saved_ram.size());
					else
						logger::Info("[EPS6800][Info] RAM image loaded from ram.dmp\n");
				}
				catch (const std::exception& error) {
					logger::Info("[EPS6800][Warn] Failed to load RAM image: %s\n", error.what());
				}
			}
			if (ShouldPersistEpsRam(emulator.hardware_id)) {
				eps_ram_save_timer_id = SDL_AddTimer(
					EPS_RAM_SAVE_INTERVAL_MS,
					[](Uint32 interval, void* param) -> Uint32 {
						static_cast<Chipset*>(param)->PersistEpsRam();
						return interval;
					},
					this);
			}
		#endif
			for (auto& peripheral : peripherals)
				peripheral->Initialise();
			// The EPS core owns CPU-visible memory, but the debugger and plugins
			// still discover their memory bridge through the project MMU object.
			mmu.SetupInternals();
			return;
		}
		if (emulator.hardware_id == HW_FX_5800P) {
			if (emulator.ModelDefinition.flash_path.empty()) {
				if (rom_data.size() > 0x20000) {
					flash_data.assign(rom_data.begin() + 0x20000, rom_data.end());
					rom_data.resize(0x20000);
				}
				else {
					flash_data.clear();
				}
			}
			else {
				try {
					flash_data = emulator.ReadModelResource(emulator.ModelDefinition.flash_path);
				}
				catch (const std::exception& error) {
					PANIC("Failed to read flash: %s\n", error.what());
				}
			}
			flash_data.resize(0x80000, 0xff);
			//memset(&flash_data[0x20000], 0xff, 0x10000); // TODO: check clear ram flag
			//memset(&flash_data[0x30000], 0, 0x8000);
			//memset(&flash_data[0x38000], 0xff, 0x8000);
			//flash_data[0x37FFE] = 0xff;
			//flash_data[0x37FFF] = 0x44;
		}
#ifndef TEST_BUILD
		{
			auto ri = rom_info(rom_data, flash_data);
			if (ri.ok) {
				printf("[Chipset][Info] Model:       %s\n", ri.ver);
				printf("[Chipset][Info] CalcID:      %llx\n", *(unsigned long long*)ri.cid);
				printf("[Chipset][Info] Target SUM:  %02x ,Calculated SUM: %02x\n", ri.desired_sum, ri.real_sum);
				auto res = (ri.real_sum == ri.desired_sum);
				if (res != real_hardware)
					printf("[Chipset][Warn] SUM %s!\n", res ? "OK" : "NG");
			}
		}
#endif
		GetRamSize(emulator.hardware_id);
		for (auto& peripheral : peripherals)
			peripheral->Initialise();

		ConstructInterruptSFR();
		ConstructClockGenerator();

		cpu.SetupInternals();
		mmu.SetupInternals();
	}

	void Chipset::Reset() {
		if (IsEpsFamily(emulator.hardware_id)) {
			RaiseEvent(on_reset, *this);
			for (auto& peripheral : peripherals)
				peripheral->Reset();
			epscpu->Reset();
			run_mode = RM_RUN;
			emulator.qr_code.Reset(false);
			return;
		}
		ResetInterruptSFR();
		isMIBlocked = false;

		ResetClockGenerator();

		SegmentAccess = false;
		data_BLKCON = 0;

		RaiseEvent(on_reset, *this);

		for (auto& peripheral : peripherals)
			peripheral->Reset();
		cpu.Reset();

		interrupts_active[INT_RESET] = true;
		pending_interrupt_count = 1;

		run_mode = RM_RUN;
		emulator.qr_code.Reset(false);
	}

	void Chipset::Break() {
		if (cpu.GetExceptionLevel() > 1) {
			Reset();
			return;
		}

		InterruptEventArgs iea{};
		iea.index = INT_BREAK;
		RaiseEvent(on_brk, *this, iea);
		if (iea.handled)
			return;

		if (interrupts_active[INT_BREAK])
			return;
		interrupts_active[INT_BREAK] = true;
		pending_interrupt_count++;
	}

	void Chipset::Halt() {
		run_mode = RM_HALT;
	}

	void Chipset::Stop() {
		run_mode = RM_STOP;
		emulator.qr_code.HandleStop(emulator);
	}

	bool Chipset::GetRunningState() {
		if (run_mode == RM_RUN)
			return true;
		return false;
	}

	void Chipset::RaiseEmulator() {
		if (interrupts_active[INT_EMULATOR])
			return;
		interrupts_active[INT_EMULATOR] = true;
		pending_interrupt_count++;
	}

	void Chipset::RequestNonmaskable() {
		SetInterruptPendingSFR(INT_NONMASKABLE, true);
		if (data_int_mask & 1)
			RaiseNonmaskable();
	}

	void Chipset::RaiseNonmaskable() {

		InterruptEventArgs iea{};
		iea.index = INT_MASKABLE;
		RaiseEvent(on_interrupt, *this, iea);
		if (iea.handled)
			return;

		if (interrupts_active[INT_NONMASKABLE])
			return;
		interrupts_active[INT_NONMASKABLE] = true;
		pending_interrupt_count++;
	}

	void Chipset::ResetNonmaskable() {
		if (!interrupts_active[INT_NONMASKABLE])
			return;
		interrupts_active[INT_NONMASKABLE] = false;
		pending_interrupt_count--;
	}

	void Chipset::RaiseMaskable(size_t index) {
		if (index < INT_MASKABLE || index >= INT_SOFTWARE)
			printf("[Chipset][Warn] %zu is not a valid maskable interrupt index\n", index);

		InterruptEventArgs iea{};
		iea.index = static_cast<uint8_t>(index); // this conversion is guaranteed
		RaiseEvent(on_interrupt, *this, iea);
		if (iea.handled)
			return;

		if (interrupts_active[index])
			return;

		interrupts_active[index] = true;
		pending_interrupt_count++;
	}

	void Chipset::ResetMaskable(size_t index) {
		if (index < INT_MASKABLE || index >= INT_SOFTWARE)
			printf("[Chipset][Warn] %zu is not a valid maskable interrupt index\n", index);
		if (!interrupts_active[index])
			return;
		interrupts_active[index] = false;
		pending_interrupt_count--;
	}

	void Chipset::RaiseSoftware(size_t index) {
		if (emulator.ModelDefinition.hardware_id == HW_TI) {
			if ((tiDiagMode || !emulator.ModelDefinition.real_hardware) && index == 0x02) {
				int dl = 500;
				while (dl > 0 && tiKey == 0) {
					SDL_Delay(24);
					dl -= 24;
				}
				emulator.chipset.cpu.reg_r[1] = 0;
				emulator.chipset.cpu.reg_r[0] = tiKey;
				tiKey = 0;
				return;
			}
			if (!emulator.ModelDefinition.real_hardware) {
				emulator.chipset.cpu.reg_r[1] = 0;
				emulator.chipset.cpu.reg_r[0] = 0;
				return;
			}
		}
		index += 0x40;
		if (interrupts_active[index])
			return;
		interrupts_active[index] = true;
		pending_interrupt_count++;
	}

	void Chipset::AcceptInterrupt() {
		size_t old_exception_level = cpu.GetExceptionLevel();

		size_t index = 0;
		bool acceptable = true;
		// * Reset has priority over everything.
		if (interrupts_active[INT_RESET])
			index = INT_RESET;
		// * Software interrupts are immediately accepted.
		if (!index)
			for (size_t ix = INT_SOFTWARE; ix != INT_COUNT; ++ix)
				if (interrupts_active[ix]) {
					if (old_exception_level > 1)
						logger::Info("software interrupt while exception level was greater than 1\n"); // test on real hardware shows that SWI seems to be raised normally when ELEVEL=2
					index = ix;
					break;
				}
		// * No need to check the old exception level as NMICI has an exception level of 3.
		if (!index && interrupts_active[INT_EMULATOR])
			index = INT_EMULATOR;
		// * No need to check the old exception level as BRK initiates a reset if
		//   the currect exception level is greater than 1.
		if (!index && interrupts_active[INT_BREAK])
			index = INT_BREAK;
		if (!index && interrupts_active[INT_NONMASKABLE]) {
			index = INT_NONMASKABLE;
			if (old_exception_level > 2) {
				acceptable = false;
			}
		}
		if (!index) {
			for (size_t ix = INT_MASKABLE; ix != INT_SOFTWARE; ++ix) {
				if (interrupts_active[ix]) {
					index = ix;
					if (old_exception_level > 1) {
						acceptable = false;
					}
					break;
				}
			}
		}

		size_t exception_level;
		switch (index) {
		case INT_RESET:
			exception_level = 0;
			break;

		case INT_BREAK:
		case INT_NONMASKABLE:
			exception_level = 2;
			break;

		case INT_EMULATOR:
			exception_level = 3;
			break;

		default:
			exception_level = 1;
			break;
		}

		if (index >= INT_MASKABLE && index < INT_SOFTWARE) {
			if (cpu.GetMasterInterruptEnable() && acceptable && (!isMIBlocked)) {
				SetInterruptPendingSFR(index, false);
				cpu.Raise(exception_level, index);

				interrupts_active[index] = false;
				pending_interrupt_count--;
			}
		}
		else if (index == INT_NONMASKABLE) {
			if (acceptable) {
				cpu.Raise(exception_level, index);
				SetInterruptPendingSFR(INT_NONMASKABLE, false);
				interrupts_active[index] = false;
				pending_interrupt_count--;
			}
		}
		else {
			cpu.Raise(exception_level, index);
			interrupts_active[index] = false;
			pending_interrupt_count--;
		}

		run_mode = RM_RUN;
	}

	bool Chipset::GetInterruptPendingSFR(size_t index) {
		return data_int_pending & (static_cast<unsigned long long>(1) << (index - managed_interrupt_base));
	}

	void Chipset::SetInterruptPendingSFR(size_t index, bool val) {
		if (val)
			data_int_pending |= (static_cast<unsigned long long>(1) << (index - managed_interrupt_base));
		else
			data_int_pending &= ~(static_cast<unsigned long long>(1) << (index - managed_interrupt_base));
	}

	void Chipset::InputToPort(int port, int pin, bool value) {
		if (port == 0) {
			if (pin < 1 || pin > 3)
				PANIC("Trying to input to invalid pin %d of Port0!", pin);
			UserInput_level_Port0[pin - 1] = value;
			UserInput_state_Port0[pin - 1] = true;
			ioport->AcceptInput(0, pin - 1);
		}
		else if (port == 1) {
			if (pin < 0 || pin > 6)
				PANIC("Trying to input to invalid pin %d of Port1!", pin);
			UserInput_level_Port1[pin] = value;
			UserInput_state_Port1[pin] = true;
			ioport->AcceptInput(1, pin);
		}
		else {
			PANIC("Trying to input to invalid port %d!", port);
		}
	}

	void Chipset::RemovePortInput(int port, int pin) {
		if (port == 0) {
			if (pin < 1 || pin > 3)
				PANIC("Trying to remove input from invalid pin %d of Port0!", pin);
			UserInput_level_Port0[pin - 1] = false;
			UserInput_state_Port0[pin - 1] = false;
			ioport->AcceptInput(0, pin - 1);
		}
		else if (port == 1) {
			if (pin < 0 || pin > 6)
				PANIC("Trying to remove input from invalid pin %d of Port1!", pin);
			UserInput_level_Port1[pin] = false;
			UserInput_state_Port1[pin] = false;
			ioport->AcceptInput(1, pin);
		}
		else {
			PANIC("Trying to remove input from invalid port %d!", port);
		}
	}

	void Chipset::Frame() {
		for (auto peripheral : peripherals)
			peripheral->Frame();
	}

	void Chipset::Tick() {
		if (IsEpsFamily(emulator.hardware_id)) {
			if (run_mode == RM_RUN && RunEpsFrame())
				emulator.SetPaused(true);
			return;
		}
		// * TODO: decrement delay counter, return if it's not 0

		if (real_hardware) {
			GenerateTickForClock();

			for (auto& peripheral : peripherals) {
				switch (peripheral->clock_type) {
				case CLOCK_UNDEFINED:
					peripheral->Tick();
					break;
				case CLOCK_LSCLK:
					if (LTBCReset)
						peripheral->ResetLSCLK();
					if (LSCLKTick)
						peripheral->Tick();
					break;
				case CLOCK_HSCLK:
					if (HSCLKTick)
						peripheral->Tick();
					break;
				case CLOCK_SYSCLK:
					if (SYSCLKTick)
						peripheral->Tick();
					break;
				default:
					break;
				}
			}
		}
		else {
			for (auto& peripheral : peripherals) {
				switch (peripheral->clock_type) {
				case CLOCK_UNDEFINED:
				case CLOCK_HSCLK:
				case CLOCK_SYSCLK:
					peripheral->Tick();
					break;
				default:
					break;
				}
			}
			HSCLKTick = SYSCLKTick = true;
		}

		if (pending_interrupt_count) {
			AcceptInterrupt();
			for (auto peripheral : peripherals)
				peripheral->TickAfterInterrupts();
		}

		if (run_mode == RM_RUN && SYSCLKTick)
			cpu.Next();

		LSCLKTick = false;
		LTBCReset = false;
		HSCLKTick = false;
		SYSCLKTick = false;
	}

	bool Chipset::RunEpsFrame(uint32_t idle_timer_cycles) {
		if (IsEpsFamily(emulator.hardware_id) && run_mode == RM_RUN && epscpu)
			return epscpu->RunFrame(idle_timer_cycles);
		return false;
	}

	void Chipset::EmulatorTick() {
		for (auto& peripheral : peripherals) {
			switch (peripheral->clock_type) {
			case CLOCK_LSCLK:
			case CLOCK_EMUCLK:
				peripheral->Tick();
				break;
			default:
				break;
			}
		}
	}

	void Chipset::UIEvent(SDL_Event event) {
		for (auto peripheral : peripherals)
			peripheral->UIEvent(event);
	}

	void Chipset::SaveStateAll(std::ostream& os) {
		if (IsEpsFamily(emulator.hardware_id)) {
			epscpu->SaveState(os);
			return;
		}
		for (auto& peripheral : peripherals)
			peripheral->SaveState(os);
		Binary::Write(os, cpu.reg_r);
		Binary::Write(os, cpu.reg_cr);
		Binary::Write(os, cpu.reg_psw);
		Binary::Write(os, cpu.reg_pc);
		Binary::Write(os, cpu.reg_csr);
		Binary::Write(os, cpu.reg_epsw);
		Binary::Write(os, cpu.reg_elr);
		Binary::Write(os, cpu.reg_ecsr);
		Binary::Write(os, cpu.reg_sp);
		Binary::Write(os, cpu.reg_ea);
		Binary::Write(os, cpu.reg_dsr);
	}

	void Chipset::LoadStateAll(std::istream& is) {
		if (IsEpsFamily(emulator.hardware_id)) {
			epscpu->LoadState(is);
			return;
		}
		for (auto& peripheral : peripherals)
			peripheral->LoadState(is);
		Binary::Read(is, cpu.reg_r);
		Binary::Read(is, cpu.reg_cr);
		Binary::Read(is, cpu.reg_psw);
		Binary::Read(is, cpu.reg_pc);
		Binary::Read(is, cpu.reg_csr);
		Binary::Read(is, cpu.reg_epsw);
		Binary::Read(is, cpu.reg_elr);
		Binary::Read(is, cpu.reg_ecsr);
		Binary::Read(is, cpu.reg_sp);
		Binary::Read(is, cpu.reg_ea);
		Binary::Read(is, cpu.reg_dsr);
	}
} // namespace casioemu
