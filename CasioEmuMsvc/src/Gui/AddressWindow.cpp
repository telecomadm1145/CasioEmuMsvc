#include "AddressWindow.h"
#include <Hooks.h>
#include <Localization.h>
#include <mutex>
struct AddressInfo {
	uint32_t address;
	uint8_t value;
	bool locked;
	AddressInfo(uint32_t addr, uint8_t val, bool lock) : address(addr), value(val), locked(lock) {}
};

class AddressWindow : public UIWindow {
public:
	AddressWindow() : UIWindow("Addrs") {
		SetupHooks();
	}

	void RenderCore() override {
		std::lock_guard lock(addresses_mutex);
		ImGui::TextUnformatted("AddressWindow.Header"_lc);
		ImGui::Separator();

		RenderAddressTable();
		ImGui::Separator();

		RenderAddAddressControls();
	}

	std::vector<DebugAddressLockInfo> DebugList() const {
		std::lock_guard lock(addresses_mutex);
		std::vector<DebugAddressLockInfo> result;
		result.reserve(addresses.size());
		for (const auto& info : addresses)
			result.push_back({info.address, info.value, info.locked});
		return result;
	}

	void DebugSet(uint32_t address, uint8_t value, bool locked) {
		std::lock_guard lock(addresses_mutex);
		auto it = std::find_if(addresses.begin(), addresses.end(), [&](const AddressInfo& info) {
			return info.address == address;
		});
		if (it == addresses.end())
			addresses.emplace_back(address, value, locked);
		else {
			it->value = value;
			it->locked = locked;
		}
		if (!locked)
			UpdateMemoryValue(address, value);
	}

	bool DebugRemove(uint32_t address) {
		std::lock_guard lock(addresses_mutex);
		auto it = std::find_if(addresses.begin(), addresses.end(), [&](const AddressInfo& info) {
			return info.address == address;
		});
		if (it == addresses.end())
			return false;
		addresses.erase(it);
		return true;
	}

	void DebugClear() {
		std::lock_guard lock(addresses_mutex);
		addresses.clear();
	}

private:
	mutable std::recursive_mutex addresses_mutex;
	std::vector<AddressInfo> addresses;
	uint32_t newAddress = 0;

	void RenderAddressTable() {
		if (ImGui::BeginTable("Addresses", 3, pretty_table)) {
			ImGui::TableSetupColumn("AddressWindow.Address"_lc);
			ImGui::TableSetupColumn("AddressWindow.Value"_lc);
			ImGui::TableSetupColumn("AddressWindow.Fixed"_lc);
			ImGui::TableHeadersRow();

			for (size_t i = 0; i < addresses.size(); ++i) {
				ImGui::PushID(i + 1145);
				auto& info = addresses[i];

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				UIHelpers::ClickableAddress(info.address);

				ImGui::TableSetColumnIndex(1);
				uint8_t value = info.value;
				if (!info.locked)
					value = m_emu->chipset.mmu.ReadData(info.address);
				if (ImGui::InputScalar("##value", ImGuiDataType_U8, &value, 0, 0, "%x")) {
					info.value = value;
					UpdateMemoryValue(info.address, info.value);
				}

				ImGui::TableSetColumnIndex(2);
				bool locked = info.locked;
				if (ImGui::Checkbox("##lock", &locked)) {
					info.locked = locked;
				}
				ImGui::PopID();
			}

			ImGui::EndTable();
		}
	}

	void RenderAddAddressControls() {
		ImGui::TextUnformatted("AddressWindow.Add"_lc);

		ImGui::InputScalar("Address", ImGuiDataType_U32, &newAddress, 0, 0, "%x");
		// ImGui::InputScalar("Value", ImGuiDataType_U8, &newValue);

		if (ImGui::Button("AddressWindow.AddBtn"_lc)) {
			if (newAddress) {
				addresses.emplace_back(newAddress, 0, false);
				// UpdateMemoryValue(*newAddress, *newValue);
			}
		}
	}

	void UpdateMemoryValue(uint32_t address, uint8_t value) {
		m_emu->chipset.mmu.WriteData(address, value);
	}

	void SetupHooks() {
		SetupHook(on_memory_write, [this](casioemu::MMU& mmu, MemoryEventArgs& args) {
			std::lock_guard lock(addresses_mutex);
			for (const auto& info : addresses) {
				if (info.locked && info.address == args.offset) {
					args.handled = true;
				}
			}
		});
		SetupHook(on_memory_read, [this](casioemu::MMU& mmu, MemoryEventArgs& args) {
			std::lock_guard lock(addresses_mutex);
			for (const auto& info : addresses) {
				if (info.locked && info.address == args.offset) {
					args.value = info.value;
					args.handled = true;
				}
			}
		});
	}
};

static AddressWindow* g_addressWindow = nullptr;

UIWindow* CreateAddressWindow() {
	g_addressWindow = new AddressWindow();
	return g_addressWindow;
}

std::vector<DebugAddressLockInfo> DebugGetAddressLocks() {
	return g_addressWindow ? g_addressWindow->DebugList() : std::vector<DebugAddressLockInfo>{};
}

void DebugSetAddressLock(uint32_t address, uint8_t value, bool locked) {
	if (g_addressWindow)
		g_addressWindow->DebugSet(address, value, locked);
}

bool DebugRemoveAddressLock(uint32_t address) {
	return g_addressWindow && g_addressWindow->DebugRemove(address);
}

void DebugClearAddressLocks() {
	if (g_addressWindow)
		g_addressWindow->DebugClear();
}
