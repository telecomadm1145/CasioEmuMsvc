#include "Editors.h"
#include "CPU.hpp"
#include "Chipset/Chipset.hpp"
#include "CodeViewer.hpp"
#include "Hooks.h"
#include "Localization.h"
#include "MemBreakPoint.hpp"
#include "Models.h"
#include "Ui.hpp"
#include "hex.hpp"
#include "ePSCpu.h"
#include "Chipset/T4xCore.hpp"

namespace {
	constexpr uint32_t kEpsFlashBaseWord = 0x18000;
	constexpr size_t kEpsFlashBaseByte = static_cast<size_t>(kEpsFlashBaseWord) * 2;
}

float ram_edit_ov[0x100000]{};
struct HexEditor : public UIWindow, public MemoryEditor {
	void* data{};
	size_t size{};
	size_t display_base{};
	bool open_popup = false;
	size_t popup_p = 0;
	HexEditor(const char* name, void* data, size_t size, size_t base) : UIWindow(name), data(data), size(size), display_base(base) {
		flags = ImGuiWindowFlags_NoScrollbar;
		this->ram_edit_ov = ::ram_edit_ov;
		contextmenuuserdata = this;
		ContextMenuFn = [](void* userdata, size_t where) {
			((HexEditor*)userdata)->open_popup = true;
			((HexEditor*)userdata)->popup_p = where;
		};
	}
	void RenderCore() override {
		this->DrawContents(data, size, display_base);
		if (open_popup) {
			ImGui::OpenPopup("ContextMenu");
			open_popup = false;
		}
		if (ImGui::BeginPopup("ContextMenu")) {
			UIHelpers::ClickableAddress(popup_p, UIHelpers::JumpTarget::Memory);
			if (ImGui::MenuItem("HexEditors.ContextMenu.MonitorWrite"_lc)) {
				SetMemBp(popup_p, true);
			}
			if (ImGui::MenuItem("HexEditors.ContextMenu.MonitorRead"_lc)) {
				SetMemBp(popup_p, false);
			}
			ImGui::EndPopup();
		}
	}
	void GotoMemoryAddress(uint32_t addr) override {
		if (m_emu->chipset.t4x) addr -= static_cast<uint32_t>(display_base);
		BringToFront();
		GotoAddrAndHighlight(addr, addr + 1);
	}
};
struct SpansHexEditor : public UIWindow, public MemoryEditor {
	void* data{};
	size_t size{};
	size_t display_base{};
	std::vector<MarkedSpan> spans{};
	bool open_popup = false;
	size_t popup_p = 0;
	SpansHexEditor(const char* name, void* data, size_t size, size_t base, std::vector<MarkedSpan> spans) : UIWindow(name), data(data), size(size), display_base(base), spans(spans) {
		flags = ImGuiWindowFlags_NoScrollbar;
		this->ram_edit_ov = ::ram_edit_ov;
		contextmenuuserdata = this;
		ContextMenuFn = [](void* userdata, size_t where) {
			((SpansHexEditor*)userdata)->open_popup = true;
			((SpansHexEditor*)userdata)->popup_p = where;
			// ImGui::OpenPopup("ContextMenu");
		};
	}
	void RenderCore() override {
		this->DrawContents(data, size, display_base, spans);
		if (open_popup) {
			ImGui::OpenPopup("ContextMenu");
			open_popup = false;
		}
		if (ImGui::BeginPopup("ContextMenu")) {
			UIHelpers::ClickableAddress(popup_p, UIHelpers::JumpTarget::Memory);
			if (ImGui::MenuItem("HexEditors.ContextMenu.MonitorWrite"_lc)) {
				SetMemBp(popup_p, true);
			}
			if (ImGui::MenuItem("HexEditors.ContextMenu.MonitorRead"_lc)) {
				SetMemBp(popup_p, false);
			}
			ImGui::EndPopup();
		}
	}
	void GotoMemoryAddress(uint32_t addr) override {
		BringToFront();
		GotoAddrAndHighlight(addr, addr + 1);
	}
};
inline auto MMU_Hex(auto he) {
	he->ReadFn = [](const ImU8* data, size_t off) -> ImU8 {
		return me_mmu->ReadData((size_t)data + off, 0);
	};
	he->WriteFn = [](ImU8* data, size_t off, ImU8 d) {
		return me_mmu->WriteData((size_t)data + off, d, 0);
	};
	return he;
}
inline auto Highlight_Default(auto he) {
	he->HighlightFn = [](const ImU8* data, size_t off) -> bool {
		if ((size_t)(data + off) == m_emu->chipset.cpu.reg_sp) {
			return true;
		}
		if (casioemu::HasInputArea(m_emu->hardware_id) &&
			(size_t)(data + off) == casioemu::GetInputAreaOffset(m_emu->hardware_id) + *((unsigned char*)n_ram_buffer - casioemu::GetRamBaseAddr(m_emu->hardware_id) + casioemu::GetCursorOffset(m_emu->hardware_id))) {
			return true;
		}
		return false;
	};
	return he;
}
inline auto EPS_ROM_Hex(auto he) {
	he->ReadFn = [](const ImU8*, size_t off) -> ImU8 {
		if (!m_emu->chipset.epscpu)
			return 0xff;
		const auto word = m_emu->chipset.epscpu->ReadCodeWord(static_cast<uint32_t>(off / 2));
		return static_cast<ImU8>((off & 1) ? word : (word >> 8));
	};
	he->WriteFn = [](ImU8*, size_t off, ImU8 value) {
		auto* eps = m_emu->chipset.epscpu;
		if (!eps)
			return;
		const uint32_t word_address = static_cast<uint32_t>(off / 2);
		auto word = eps->ReadCodeWord(word_address);
		word = (off & 1)
			? static_cast<uint16_t>((word & 0xff00) | value)
			: static_cast<uint16_t>((word & 0x00ff) | (static_cast<uint16_t>(value) << 8));
		if (!eps->WriteCodeWord(word_address, word))
			return;
		eps->WriteRomImageWord(m_emu->chipset.rom_data, word_address, word);
	};
	return he;
}
inline auto EPS_FLASH_Hex(auto he) {
	he->ReadFn = [](const ImU8*, size_t off) -> ImU8 {
		if (!m_emu->chipset.epscpu)
			return 0xff;
		const auto word = m_emu->chipset.epscpu->ReadCodeWord(
			kEpsFlashBaseWord + static_cast<uint32_t>(off / 2));
		return static_cast<ImU8>((off & 1) ? word : (word >> 8));
	};
	he->WriteFn = [](ImU8*, size_t off, ImU8 value) {
		auto* eps = m_emu->chipset.epscpu;
		if (!eps)
			return;
		const uint32_t flash_word_offset = static_cast<uint32_t>(off / 2);
		const uint32_t word_address = kEpsFlashBaseWord + flash_word_offset;
		auto word = eps->ReadCodeWord(word_address);
		word = (off & 1)
			? static_cast<uint16_t>((word & 0xff00) | value)
			: static_cast<uint16_t>((word & 0x00ff) | (static_cast<uint16_t>(value) << 8));
		if (!eps->WriteCodeWord(word_address, word))
			return;
		eps->WriteFlashImageWord(m_emu->chipset.flash_data, flash_word_offset, word);
	};
	return he;
}

inline auto EPS_VRAM_Hex(auto he) {
	he->ReadFn = [](const ImU8*, size_t off) -> ImU8 {
		return m_emu->chipset.epscpu ? m_emu->chipset.epscpu->ReadLcdMemory(off) : 0xff;
	};
	he->WriteFn = [](ImU8*, size_t off, ImU8 value) {
		if (m_emu->chipset.epscpu)
			m_emu->chipset.epscpu->WriteLcdMemory(off, value);
	};
	return he;
}

std::vector<UIWindow*> GetEditors() {
	std::vector<UIWindow*> windows;
	if (m_emu->chipset.t4x) {
		const char* names[] = {"T4x Registers (nibbles)", "T4x WRAM (nibbles)", "T4x DRAM (bytes)", "T4x ROM (bytes)"};
		const size_t sizes[] = {64, 1024, 4096, m_emu->chipset.rom_data.size()};
		const size_t bases[] = {0, 0x100, 0x1000, 0};
		for (unsigned space = 0; space < 4; ++space) {
			auto* editor = new HexEditor(names[space], reinterpret_cast<void*>(static_cast<uintptr_t>(space)), sizes[space], bases[space]);
			if (space == 3) editor->ContextMenuFn = nullptr; // ROM has execution breakpoints in CodeViewer.
			editor->ReadFn = [](const ImU8* data, size_t off) -> ImU8 {
				auto* core = m_emu->chipset.t4x;
				unsigned space = static_cast<unsigned>(reinterpret_cast<uintptr_t>(data));
				if (space != 3)
					return core->ReadMemory(space, static_cast<unsigned>(off));
				return core->ReadCodeByte(static_cast<unsigned>(off));
			};
			editor->WriteFn = [](ImU8* data, size_t off, ImU8 value) {
				if (!m_emu->GetPaused())
					return;
				auto* core = m_emu->chipset.t4x;
				unsigned space = static_cast<unsigned>(reinterpret_cast<uintptr_t>(data));
				if (space != 3)
					core->WriteMemory(space, static_cast<unsigned>(off), value);
				else {
					core->WriteCodeByte(static_cast<unsigned>(off), value);
					m_emu->chipset.rom_data[off] = value;
					code_viewer->PrepareDisasm();
				}
			};
			windows.push_back(editor);
		}
		return windows;
	}
	SetupHook(on_memory_write, [](casioemu::MMU& mmu, MemoryEventArgs& mea) {
		if (mea.offset < 0x80000)
			ram_edit_ov[mea.offset] = 255;
	});
	if (casioemu::IsEpsFamily(m_emu->hardware_id)) {
		const size_t rom_display_bytes = m_emu->chipset.epscpu->RomFormat() == casioemu::Eps6800RomFormat::UnpackedNibbles
											 ? m_emu->chipset.rom_data.size() / 2
											 : m_emu->chipset.rom_data.size();
		windows.push_back(EPS_ROM_Hex(new HexEditor{"Rom", nullptr, rom_display_bytes, 0}));
		if (!m_emu->chipset.flash_data.empty())
			windows.push_back(EPS_FLASH_Hex(new HexEditor{"Flash", nullptr, m_emu->chipset.flash_data.size(), kEpsFlashBaseByte}));
		windows.push_back(MMU_Hex(new HexEditor{"Ram", nullptr, 0x2080, 0}));
		windows.push_back(MMU_Hex(new HexEditor{"Regs", nullptr, 0x80, 0}));
		windows.push_back(EPS_VRAM_Hex(new HexEditor{"VRam", nullptr, m_emu->chipset.epscpu->LcdRawSize(), 0}));
	}
	else {
		windows.push_back(new HexEditor{"Rom", m_emu->chipset.rom_data.data(), m_emu->chipset.rom_data.size(), 0});
		windows.push_back(
			Highlight_Default(
				MMU_Hex(
					new SpansHexEditor{
						"Ram",
						(void*)casioemu::GetRamBaseAddr(m_emu->hardware_id),
						casioemu::GetRamEditorSize(m_emu->hardware_id),
						casioemu::GetRamBaseAddr(m_emu->hardware_id),
						GetCommonMemLabels(m_emu->hardware_id)})));
		if (m_emu->hardware_id == casioemu::HW_FX_5800P) {
			windows.push_back(MMU_Hex(new SpansHexEditor{"PRam", (void*)0x40000, 0x8000, 0x40000, GetCommonMemLabels(m_emu->hardware_id)}));
			windows.push_back(new HexEditor{"Flash", m_emu->chipset.flash_data.data(), m_emu->chipset.flash_data.size(), 0});
		}
		windows.push_back(MMU_Hex(new HexEditor{"All", 0, 0xfffff, 0}));
	}
	return windows;
}
