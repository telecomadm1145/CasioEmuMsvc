#include "AddressWindow.h"
#include <Hooks.h>

struct AddressInfo {
	uint32_t address;
	uint8_t value;
	bool locked;
	AddressInfo(uint32_t addr, uint8_t val, bool lock) : address(addr), value(val), locked(lock) {}
};

class AddressWindow : public UIWindow {
public:
	AddressWindow() : UIWindow("Address Manager") {
		SetupHooks();
	}

	void RenderCore() override {
		ImGui::Text("Manage Addresses");
		ImGui::Separator();

		RenderAddressTable();
		ImGui::Separator();

		RenderAddAddressControls();
	}

private:
	std::vector<AddressInfo> addresses;
	uint32_t newAddress = 0;

	void RenderAddressTable() {
		if (ImGui::BeginTable("Addresses", 3, pretty_table)) {
			ImGui::TableSetupColumn("Address");
			ImGui::TableSetupColumn("Value");
			ImGui::TableSetupColumn("Lock");
			ImGui::TableHeadersRow();

			for (size_t i = 0; i < addresses.size(); ++i) {
				ImGui::PushID(i + 1145);
				auto& info = addresses[i];

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("0x%08X", info.address);

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
		ImGui::Text("Add New Address");

		ImGui::InputScalar("Address", ImGuiDataType_U32, &newAddress, 0, 0, "%x");
		// ImGui::InputScalar("Value", ImGuiDataType_U8, &newValue);

		if (ImGui::Button("Add Address")) {
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
			for (const auto& info : addresses) {
				if (info.locked && info.address == args.offset) {
					args.handled = true;
				}
			}
		});
		SetupHook(on_memory_read, [this](casioemu::MMU& mmu, MemoryEventArgs& args) {
			for (const auto& info : addresses) {
				if (info.locked && info.address == args.offset) {
					args.value = info.value;
					args.handled = true;
				}
			}
		});
	}
};

UIWindow* CreateAddressWindow() {
	return new AddressWindow();
}
