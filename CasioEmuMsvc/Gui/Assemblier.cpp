#include "Assemblier/U8Emitter.h"
#include "U8Disas.h"
#include "Ui.hpp"
#include <sstream>
#include <string>
#include <vector>

class AssemblierUI : public UIWindow {
public:
	AssemblierUI() : UIWindow("Asm") {}
	char asmSrc[19937]{};
	std::vector<char> program{};
	std::string output{};

	void RenderCore() override {
		auto starty = ImGui::GetCursorPosY();
		ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.5f - 40);
		ImGui::InputTextMultiline("##source", asmSrc, 19937, {0,ImGui::GetWindowHeight()});

		ImGui::SetCursorPos({ImGui::GetWindowWidth() * 0.5f - 40, starty});
		if (ImGui::Button("Assemble")) {
			Assemble();
		}

		ImGui::SetCursorPos({ImGui::GetWindowWidth() * 0.5f + 40, starty});
		ImGui::InputTextMultiline("##disasm", (char*)output.c_str(), output.size(), {0, ImGui::GetWindowHeight()}, ImGuiInputTextFlags_ReadOnly);
	}

private:
	void Assemble() {
		std::stringstream ss{};
		output.clear();
		program.clear();

		// Split `asmSrc` into lines
		std::istringstream srcStream(asmSrc);
		std::string line;
		u8::Emitter emitter{};
		int lineNumber = 0;

		try {
			while (std::getline(srcStream, line)) {
				++lineNumber;
				if (line.empty())
					continue; // Skip empty lines

				try {
					emitter.Assembly(line.c_str());
				}
				catch (const std::exception& ex) {
					ss << "Error on line " << lineNumber << ": " << ex.what() << "\n";
				}
			}

			// Move program data from emitter
			program = std::move(emitter.Bytes);
			program.reserve(program.size() + 0x100);

			// Decode the program bytes
			uint8_t* u = (uint8_t*)program.data();
			while (u < (uint8_t*)(program.data() + program.size())) {
				decode(ss, u, 0x100);
				ss << "\n";
			}

			output = ss.str();
		}
		catch (const std::exception& ex) {
			output = "Critical error: " + std::string(ex.what());
		}
	}
};

UIWindow* MakeAssemblierUI() {
	return new AssemblierUI();
}
