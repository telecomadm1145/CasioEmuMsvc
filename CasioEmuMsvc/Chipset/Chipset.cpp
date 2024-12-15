#include "Chipset.hpp"

#include "5800Flash.h"
#include "Audio.h"
#include "BCDCalc.hpp"
#include "BatteryBackedRAM.hpp"
#include "CPU.hpp"
#include "Dcl/ChipsetInfo.h"
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
#include <ML620Ports.h>
#include <Spi.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#include "Peripheral/SD/FakeSdCard.h"

namespace casioemu {
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

	void Chipset::Setup() {
		for (size_t ix = 0; ix != INT_COUNT; ++ix)
			interrupts_active[ix] = false;
		pending_interrupt_count = 0;

		cpu.SetMemoryModel(CPU::MM_LARGE);
		cpu.SetCPUModel(emulator.hardware_id == HW_CLASSWIZ || emulator.hardware_id == HW_CLASSWIZ_II || emulator.hardware_id == HW_TI ? CPU::CM_NX_U16 : CPU::CM_NX_U8);

		std::initializer_list<int> segments_es_plus{0, 1, 8}, segments_classwiz{0, 1, 2, 3, 4, 5}, segments_classwiz_ii{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
		for (auto segment_index : emulator.hardware_id == HW_ES_PLUS ? segments_es_plus : emulator.hardware_id == HW_CLASSWIZ ? segments_classwiz
																															  : segments_classwiz_ii)
			mmu.GenerateSegmentDispatch(segment_index);

		real_hardware = emulator.modeldef.real_hardware;

		ConstructPeripherals();
	}

	Chipset::~Chipset() {
		DestructPeripherals();
		DestructClockGenerator();
		DestructInterruptSFR();

		delete &mmu;
		delete &cpu;
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
			if(chipset->data_LTBADJ != 0)
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
			for(auto peripheral : chipset->peripherals) {
				int block_bit = peripheral->block_bit;
				if(block_bit == -1)
					continue;
				if((1 << block_bit) > chipset->BLKCON_mask)
					PANIC("Invalid BLKCON0 bit %d\n", block_bit);
				if(data & (1 << block_bit))
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
		auto spi = QueryInterface<ISpiProvider>();
		if (spi)
			new FakeSdCard(spi);
	}

	void Chipset::DestructPeripherals() {
		region_BLKCON.Kill();

		for (auto& peripheral : peripherals) {
			peripheral->Uninitialise();
			delete peripheral;
		}
	}

	void Chipset::SetupInternals() {
		std::ifstream rom_handle(emulator.GetModelFilePath(emulator.modeldef.rom_path), std::ifstream::binary);
		if (rom_handle.fail())
			PANIC("std::ifstream failed: %s\n", std::strerror(errno));
		rom_data = std::vector<unsigned char>((std::istreambuf_iterator<char>(rom_handle)), std::istreambuf_iterator<char>());

		if (emulator.hardware_id == HW_FX_5800P) {
			std::ifstream flash_handle(emulator.GetModelFilePath(emulator.modeldef.flash_path), std::ifstream::binary);
			if (flash_handle.fail())
				PANIC("std::ifstream failed: %s\n", std::strerror(errno));
			flash_data = std::vector<unsigned char>((std::istreambuf_iterator<char>(flash_handle)), std::istreambuf_iterator<char>());
			flash_data.resize(0x80000, 0xff);
			memset(&flash_data[0x20000], 0xff, 0x10000); // TODO: check clear ram flag
			memset(&flash_data[0x30000], 0, 0x8000);
			memset(&flash_data[0x38000], 0xff, 0x8000);
			flash_data[0x37FFE] = 0xff;
			flash_data[0x37FFF] = 0x44;
		}
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
		GetRamSize(emulator.hardware_id);
		for (auto& peripheral : peripherals)
			peripheral->Initialise();

		ConstructInterruptSFR();
		ConstructClockGenerator();

		cpu.SetupInternals();
		mmu.SetupInternals();
	}

	void Chipset::Reset() {
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
			PANIC("%zu is not a valid maskable interrupt index\n", index);

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
			PANIC("%zu is not a valid maskable interrupt index\n", index);
		if (!interrupts_active[index])
			return;
		interrupts_active[index] = false;
		pending_interrupt_count--;
	}

	void Chipset::RaiseSoftware(size_t index) {
		if (emulator.modeldef.hardware_id == HW_TI) {
			if ((tiDiagMode || !emulator.modeldef.real_hardware) && index == 0x02) {
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
			if (!emulator.modeldef.real_hardware) {
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

		if (run_mode == RM_RUN && SYSCLKTick) {
			cpu.Next();
		}

		LSCLKTick = false;
		LTBCReset = false;
		HSCLKTick = false;
		SYSCLKTick = false;
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

	void Chipset::UIEvent(SDL_Event& event) {
		for (auto peripheral : peripherals)
			peripheral->UIEvent(event);
	}
} // namespace casioemu
