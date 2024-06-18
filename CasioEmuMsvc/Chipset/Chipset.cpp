#include "Chipset.hpp"

#include "../Data/HardwareId.hpp"
#include "../Emulator.hpp"
#include "../Logger.hpp"
#include "CPU.hpp"
#include "MMU.hpp"
#include "InterruptSource.hpp"

#include "../Peripheral/ROMWindow.hpp"
#include "../Peripheral/BatteryBackedRAM.hpp"
#include "../Peripheral/Screen.hpp"
#include "../Peripheral/Keyboard.hpp"
#include "../Peripheral/StandbyControl.hpp"
#include "../Peripheral/Miscellaneous.hpp"
#include "../Peripheral/Timer.hpp"
#include "../Peripheral/BCDCalc.hpp"

#include "../Gui/ui.hpp"

#include <fstream>
#include <algorithm>
#include <cstring>

namespace casioemu
{
	Chipset::Chipset(Emulator &_emulator) : emulator(_emulator), cpu(*new CPU(emulator)), mmu(*new MMU(emulator))
	{
	}

	void Chipset::Setup()
	{
		for (size_t ix = 0; ix != INT_COUNT; ++ix)
			interrupts_active[ix] = false;
		pending_interrupt_count = 0;

		cpu.SetMemoryModel(CPU::MM_LARGE);

		std::initializer_list<int> segments_es_plus{ 0, 1, 8 }, segments_classwiz{ 0, 1, 2, 3, 4, 5 }, segments_classwiz_ii{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
		for (auto segment_index : emulator.hardware_id == HW_ES_PLUS ? segments_es_plus : emulator.hardware_id == HW_CLASSWIZ ? segments_classwiz : segments_classwiz_ii)
			mmu.GenerateSegmentDispatch(segment_index);

		ConstructPeripherals();
	}

	Chipset::~Chipset()
	{
		DestructPeripherals();
		DestructInterruptSFR();

		delete &mmu;
		delete &cpu;
	}

	void Chipset::ConstructInterruptSFR()
	{
		EffectiveMICount = emulator.hardware_id == HW_ES_PLUS ? 12 : emulator.hardware_id == HW_CLASSWIZ ? 17 : 21;
		MaskableInterrupts = new InterruptSource[EffectiveMICount];
		for(size_t i = 0; i < EffectiveMICount; i++) {
			MaskableInterrupts[i].Setup(i + INT_MASKABLE, emulator);
		}
		isMIBlocked = false;

		//WDTINT is unused
		region_int_mask.Setup(0xF010, 4, "Chipset/InterruptMask", this, [](MMURegion *region, size_t offset) {
			offset -= region->base;
			Chipset *chipset = (Chipset*)region->userdata;
			return (uint8_t)((chipset->data_int_mask >> (offset * 8)) & 0xFF);
		}, [](MMURegion *region, size_t offset, uint8_t data) {
			offset -= region->base;
			Chipset *chipset = (Chipset*)region->userdata;
			size_t mask = (1 << (chipset->EffectiveMICount + 1)) - 2;
			chipset->data_int_mask = (chipset->data_int_mask & (~(0xFF << (offset * 8)))) | (data << (offset * 8));
			chipset->data_int_mask &= mask;
			for(size_t i = 0; i < chipset->EffectiveMICount; i++) {
				chipset->MaskableInterrupts[i].SetEnabled(chipset->data_int_mask & (1 << (i + 1)));
			}
			if(chipset->data_int_mask & 1) {
				if(chipset->GetInterruptPendingSFR(4))
					chipset->RaiseNonmaskable();
			} else {
				chipset->ResetNonmaskable();
			}
		}, emulator);

		region_int_pending.Setup(0xF014, 4, "Chipset/InterruptPending", this, [](MMURegion *region, size_t offset) {
			offset -= region->base;
			Chipset *chipset = (Chipset*)region->userdata;
			return (uint8_t)((chipset->data_int_pending >> (offset * 8)) & 0xFF);
		}, [](MMURegion *region, size_t offset, uint8_t data) {
			offset -= region->base;
			Chipset *chipset = (Chipset*)region->userdata;
			size_t mask = (1 << (chipset->EffectiveMICount + 1)) - 2;
			chipset->data_int_pending = (chipset->data_int_pending & (~(0xFF << (offset * 8)))) | (data << (offset * 8));
			chipset->data_int_pending &= mask;
			for(size_t i = 0; i < chipset->EffectiveMICount; i++) {
				if(chipset->data_int_pending & (1 << (i + 1)))
					chipset->MaskableInterrupts[i].TryRaise();
				else
					chipset->MaskableInterrupts[i].ResetInt();
			}
			if(chipset->data_int_pending & 1) {
				if(chipset->data_int_mask & 1)
					chipset->RaiseNonmaskable();
			} else {
				chipset->ResetNonmaskable();
			}
		}, emulator);
	}

	void Chipset::ResetInterruptSFR() {
		data_int_mask = 0;
		data_int_pending = 0;
		for(size_t i = 0; i < EffectiveMICount; i++) {
			MaskableInterrupts[i].SetEnabled(false);
			MaskableInterrupts[i].ResetInt();
		}
		ResetNonmaskable();
	}

	void Chipset::DestructInterruptSFR()
	{
		region_int_pending.Kill();
		region_int_mask.Kill();
	}

	void Chipset::ConstructPeripherals()
	{
		peripherals.push_front(new ROMWindow(emulator));
		peripherals.push_front(new BatteryBackedRAM(emulator));
		peripherals.push_front(CreateScreen(emulator));
		peripherals.push_front(new Keyboard(emulator));
		peripherals.push_front(new StandbyControl(emulator));
		peripherals.push_front(new Miscellaneous(emulator));
		peripherals.push_front(new Timer(emulator));
		if (emulator.hardware_id == HW_CLASSWIZ_II)
			peripherals.push_front(new BCDCalc(emulator));
	}

	void Chipset::DestructPeripherals()
	{
		for (auto &peripheral : peripherals)
		{
			peripheral->Uninitialise();
			delete peripheral;
		}
	}

	void Chipset::SetupInternals()
	{
		std::ifstream rom_handle(emulator.GetModelFilePath(emulator.GetModelInfo("rom_path")), std::ifstream::binary);
		if (rom_handle.fail())
			PANIC("std::ifstream failed: %s\n", std::strerror(errno));
		rom_data = std::vector<unsigned char>((std::istreambuf_iterator<char>(rom_handle)), std::istreambuf_iterator<char>());

		if (emulator.hardware_id == HW_5800P)
		{
			std::ifstream flash_handle(emulator.GetModelFilePath(emulator.GetModelInfo("flash_path")), std::ifstream::binary);
			if (flash_handle.fail())
				PANIC("std::ifstream failed: %s\n", std::strerror(errno));
			flash_data = std::vector<unsigned char>((std::istreambuf_iterator<char>(flash_handle)), std::istreambuf_iterator<char>());
		}

		for (auto &peripheral : peripherals)
			peripheral->Initialise();

		ConstructInterruptSFR();

		cpu.SetupInternals();
		mmu.SetupInternals();
	}

	void Chipset::Reset()
	{
		ResetInterruptSFR();
		isMIBlocked = false;

		for (auto &peripheral : peripherals)
			peripheral->Reset();

		cpu.Reset();

		interrupts_active[INT_RESET] = true;
		pending_interrupt_count = 1;

		run_mode = RM_RUN;
	}

	void Chipset::Break()
	{
		if (cpu.GetExceptionLevel() > 1)
		{
			Reset();
			return;
		}

		__debugbreak();

		if (interrupts_active[INT_BREAK])
			return;
		interrupts_active[INT_BREAK] = true;
		pending_interrupt_count++;
	}

	void Chipset::Halt()
	{
		run_mode = RM_HALT;
	}

	void Chipset::Stop()
	{
		run_mode = RM_STOP;
	}

	bool Chipset::GetRunningState()
	{
		if(run_mode == RM_RUN)
			return true;
		return false;
	}
	
	void Chipset::RaiseEmulator()
	{
		if (interrupts_active[INT_EMULATOR])
			return;
		interrupts_active[INT_EMULATOR] = true;
		pending_interrupt_count++;
	}

	void Chipset::RaiseNonmaskable()
	{
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

	void Chipset::RaiseMaskable(size_t index)
	{
		if (index < INT_MASKABLE || index >= INT_SOFTWARE)
			PANIC("%zu is not a valid maskable interrupt index\n", index);
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

	void Chipset::RaiseSoftware(size_t index)
	{
		index += 0x40;
		if (interrupts_active[index])
			return;
		interrupts_active[index] = true;
		pending_interrupt_count++;
	}

	void Chipset::AcceptInterrupt()
	{
		size_t old_exception_level = cpu.GetExceptionLevel();

		size_t index = 0;
		bool acceptable = true;
		// * Reset has priority over everything.
		if (interrupts_active[INT_RESET])
			index = INT_RESET;
		// * Software interrupts are immediately accepted.
		if (!index)
			for (size_t ix = INT_SOFTWARE; ix != INT_COUNT; ++ix)
				if (interrupts_active[ix])
				{
					if (old_exception_level > 1)
						logger::Info("software interrupt while exception level was greater than 1\n");//test on real hardware shows that SWI seems to be raised normally when ELEVEL=2
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
			if(old_exception_level > 2) {
				acceptable = false;
			}
		}
		if (!index){
			for (size_t ix = INT_MASKABLE; ix != INT_SOFTWARE; ++ix){
				if (interrupts_active[ix])
				{
					index = ix;
					if(old_exception_level > 1) {
						acceptable = false;
					}
					break;
				}
			}
		}

		size_t exception_level;
		switch (index)
		{
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

		if (index >= INT_MASKABLE && index < INT_SOFTWARE)
		{
			if (cpu.GetMasterInterruptEnable() && acceptable && (!isMIBlocked)) {
				SetInterruptPendingSFR(index, false);
				cpu.Raise(exception_level, index);

				interrupts_active[index] = false;
				pending_interrupt_count--;
			}
			
		}
		else if(index == INT_NONMASKABLE)
		{
			if(acceptable) {
				cpu.Raise(exception_level, index);
				SetInterruptPendingSFR(INT_NONMASKABLE, false);
				interrupts_active[index] = false;
				pending_interrupt_count--;
			}
		} else {
			cpu.Raise(exception_level, index);
			interrupts_active[index] = false;
			pending_interrupt_count--;
		}

		run_mode = RM_RUN;
	}

	bool Chipset::GetInterruptPendingSFR(size_t index)
	{
		return data_int_pending & (1 << (index - managed_interrupt_base));
	}

	void Chipset::SetInterruptPendingSFR(size_t index, bool val)
	{
		if(val)
			data_int_pending |= (1 << (index - managed_interrupt_base));
		else
			data_int_pending &= ~(1 << (index - managed_interrupt_base));
	}

	bool Chipset::GetRequireFrame()
	{
		return std::any_of(peripherals.begin(), peripherals.end(), [](Peripheral *peripheral){
			return peripheral->GetRequireFrame();
		});
	}

	void Chipset::Frame()
	{
		for (auto peripheral : peripherals)
			peripheral->Frame();
	}

	void Chipset::Tick()
	{
		// * TODO: decrement delay counter, return if it's not 0

		for (auto peripheral : peripherals)
			peripheral->Tick();

		if (pending_interrupt_count) {
			AcceptInterrupt();
			for (auto peripheral : peripherals)
				peripheral->TickAfterInterrupts();
		}

		if (run_mode == RM_RUN)
			cpu.Next();
	}

	void Chipset::UIEvent(SDL_Event &event)
	{
		for (auto peripheral : peripherals)
			peripheral->UIEvent(event);
	}
}

