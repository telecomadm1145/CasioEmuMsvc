// LibCompilerImGuiWindow.cpp
#include "Compiler.h"
#include "TextEditor.h"
#include "Ui.hpp"
#include "hex.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <imgui.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// ================================================================
// Bridge types between lc::details and the ImGui window
// ================================================================
struct CompileDiagnostic {
	std::string message;
	int line = 0;
	bool error = false;
};

struct CompileResult {
	std::vector<uint8_t> bytes;
	uint32_t baseAddress = 0x8154;
	std::vector<CompileDiagnostic> diagnostics;
};

// ================================================================
// LibCompiler ImGui Window
// ================================================================
class LibCompilerImGuiWindow : public UIWindow {
public:
	using CompileCallback = std::function<CompileResult(const std::string&)>;

	explicit LibCompilerImGuiWindow(CompileCallback cb = nullptr)
		: UIWindow("Rop Compiler"), compileCallback_(std::move(cb)) {
		setupTextEditor();
		setupMemoryEditor();
		editor_.SetText(
			"home:\n"
			"    hex 30 30 30 30\n"
			"msg:\n"
			"    str \"HELLO\"\n"
			"    0x1234\n");
		flags |= ImGuiWindowFlags_MenuBar;
	}

	void RenderCore() override {
		drawMenuBar();
		drawToolbar();
		ImGui::Separator();

		const float diagnosticsHeight = 135.0f;
		const float mainHeight =
			ImGui::GetContentRegionAvail().y - diagnosticsHeight - ImGui::GetStyle().ItemSpacing.y;

		if (ImGui::BeginTable("##compiler_layout", 2,
				ImGuiTableFlags_Resizable |
					ImGuiTableFlags_BordersInnerV |
					ImGuiTableFlags_SizingStretchProp,
				ImVec2(-1, mainHeight))) {
			ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 0.58f);
			ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthStretch, 0.42f);
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			drawSourcePanel(mainHeight);

			ImGui::TableSetColumnIndex(1);
			drawOutputPanel(mainHeight);

			ImGui::EndTable();
		}

		drawDiagnosticsPanel(diagnosticsHeight);
		handleShortcuts();
	}

	void SetSource(const std::string& source) {
		editor_.SetText(source);
	}

	std::string GetSource() const {
		return editor_.GetText();
	}

	const std::vector<uint8_t>& GetOutputBytes() const {
		return outputBytes_;
	}

private:
	TextEditor editor_;
	MemoryEditor memoryEditor_;
	std::vector<uint8_t> outputBytes_;
	uint32_t baseAddress_ = 0x8154;
	std::string diagnosticsText_;
	bool lastCompileOk_ = true;
	CompileCallback compileCallback_;

private:
	// ============================================================
	// Setup
	// ============================================================
	void setupTextEditor() {
		editor_.SetPalette(TextEditor::GetDarkPalette());
		editor_.SetLanguageDefinition(makeLibCompilerLanguageDefinition());
		editor_.SetShowWhitespaces(false);
	}

	void setupMemoryEditor() {
		memoryEditor_.ReadOnly = true;
		memoryEditor_.Cols = 16;
		memoryEditor_.OptShowAscii = true;
		memoryEditor_.OptShowOptions = true;
		memoryEditor_.OptShowDataPreview = true;
		memoryEditor_.OptUpperCaseHex = true;
	}

	static TextEditor::LanguageDefinition makeLibCompilerLanguageDefinition() {
		TextEditor::LanguageDefinition lang;
		lang.mName = "LIBCOMPILER DSL";
		lang.mCaseSensitive = false;
		lang.mSingleLineComment = "#";
		lang.mCommentStart = "/*";
		lang.mCommentEnd = "*/";

		const char* keywords[] = {
			"call", "goto", "adr_of", "adr_arith",
			"pr_length", "remaining_length", "org", "hex",
			"str", "backup", "is", "addrcopy",
			"loop880", "loop580", "backup880", "backup580",
			"setup_loop", "l_bytes", "home", "setlr",
			"di", "rt", "pop", "sp"};
		for (auto* kw : keywords) {
			lang.mKeywords.insert(kw);
		}

		lang.mTokenRegexStrings.push_back({R"(\b0x[0-9a-fA-F]+\b)",
			TextEditor::PaletteIndex::Number});
		lang.mTokenRegexStrings.push_back({R"(\b[0-9]+\b)",
			TextEditor::PaletteIndex::Number});
		lang.mTokenRegexStrings.push_back({R"("[^"]*")",
			TextEditor::PaletteIndex::String});
		lang.mTokenRegexStrings.push_back({R"([a-zA-Z_.$][a-zA-Z0-9_.$]*)",
			TextEditor::PaletteIndex::Identifier});
		lang.mTokenRegexStrings.push_back({R"([+\-*/=:\[\](),;])",
			TextEditor::PaletteIndex::Punctuation});

		return lang;
	}

	// ============================================================
	// UI
	// ============================================================
	void drawMenuBar() {
		if (!ImGui::BeginMenuBar())
			return;

		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("New")) {
				editor_.SetText("");
				outputBytes_.clear();
				diagnosticsText_.clear();
				lastCompileOk_ = true;
			}
			if (ImGui::MenuItem("Compile", "F5")) {
				compile();
			}
			if (ImGui::MenuItem("Copy Source")) {
				ImGui::SetClipboardText(editor_.GetText().c_str());
			}
			if (ImGui::MenuItem("Copy Output Hex")) {
				copyOutputHexToClipboard();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View")) {
			ImGui::MenuItem("Show ASCII", nullptr, &memoryEditor_.OptShowAscii);
			ImGui::MenuItem("Show Options", nullptr, &memoryEditor_.OptShowOptions);
			ImGui::MenuItem("Show Data Preview", nullptr, &memoryEditor_.OptShowDataPreview);
			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}

	void drawToolbar() {
		if (ImGui::Button("Compile")) {
			compile();
		}
		ImGui::SameLine();
		if (ImGui::Button("Load Rop data")) {
			compile();
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear Output")) {
			outputBytes_.clear();
			diagnosticsText_.clear();
			editor_.SetErrorMarkers({});
			lastCompileOk_ = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Copy Hex")) {
			copyOutputHexToClipboard();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();
		ImGui::Text("Base:");
		ImGui::SameLine();

		int base = static_cast<int>(baseAddress_);
		ImGui::SetNextItemWidth(100.0f);
		if (ImGui::InputInt("##base_address", &base, 0, 0, ImGuiInputTextFlags_CharsHexadecimal)) {
			if (base < 0)
				base = 0;
			baseAddress_ = static_cast<uint32_t>(base);
		}
		ImGui::SameLine();
		ImGui::Text("Length: 0x%04X / %zu bytes",
			static_cast<unsigned>(outputBytes_.size()),
			outputBytes_.size());
		ImGui::SameLine();

		if (lastCompileOk_) {
			ImGui::TextColored(ImVec4(0.25f, 0.95f, 0.35f, 1.0f), "OK");
		}
		else {
			ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "ERROR");
		}
	}

	void drawSourcePanel(float height) {
		ImGui::BeginChild("##source_panel", ImVec2(-1, height), true);
		ImGui::TextUnformatted("Source - ImGuiColorTextEdit");
		ImGui::Separator();
		const float footerHeight = 24.0f;
		editor_.Render("##source_editor",
			ImVec2(-1, ImGui::GetContentRegionAvail().y - footerHeight), true);
		auto cpos = editor_.GetCursorPosition();
		ImGui::Text("Line: %d  Column: %d | Total lines: %d",
			cpos.mLine + 1, cpos.mColumn + 1, editor_.GetTotalLines());
		ImGui::EndChild();
	}

	void drawOutputPanel(float height) {
		ImGui::BeginChild("##output_panel", ImVec2(-1, height), true);
		ImGui::TextUnformatted("Output - imgui_memory_editor: hexadecimal editor (2017-2024)");
		ImGui::TextDisabled("github/ocornut/imgui_club");
		ImGui::Separator();

		if (ImGui::BeginTabBar("##output_tabs")) {
			if (ImGui::BeginTabItem("Hex Editor")) {
				if (!outputBytes_.empty()) {
					memoryEditor_.DrawContents(
						outputBytes_.data(), outputBytes_.size(), baseAddress_);
				}
				else {
					ImGui::TextDisabled("No output. Press Compile.");
				}
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Hex Text")) {
				std::string hex = makeHexDumpText();
				ImGui::InputTextMultiline("##hex_text", hex.data(),
					hex.size() + 1, ImVec2(-1, -1),
					ImGuiInputTextFlags_ReadOnly);
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Raw Bytes")) {
				if (outputBytes_.empty()) {
					ImGui::TextDisabled("No bytes.");
				}
				else {
					for (size_t i = 0; i < outputBytes_.size(); ++i) {
						ImGui::Text("result[%04zu] = 0x%02X", i,
							static_cast<unsigned>(outputBytes_[i]));
					}
				}
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
		ImGui::EndChild();
	}

	void drawDiagnosticsPanel(float height) {
		ImGui::BeginChild("##diagnostics_panel", ImVec2(-1, height), true);
		ImGui::TextUnformatted("Diagnostics");
		ImGui::Separator();

		if (diagnosticsText_.empty()) {
			ImGui::TextDisabled("No diagnostics.");
		}
		else {
			ImGui::PushTextWrapPos(0.0f);
			if (lastCompileOk_) {
				ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.45f, 1.0f),
					"%s", diagnosticsText_.c_str());
			}
			else {
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
					"%s", diagnosticsText_.c_str());
			}
			ImGui::PopTextWrapPos();
		}
		ImGui::EndChild();
	}

	void handleShortcuts() {
		ImGuiIO& io = ImGui::GetIO();
		if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
			compile();
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
			compile();
		}
	}

	// ============================================================
	// Compile
	// ============================================================
	void compile() {
		editor_.SetErrorMarkers({});
		const std::string source = editor_.GetText();

		try {
			CompileResult result;

			if (compileCallback_) {
				result = compileCallback_(source);
			}
			else {
				result = defaultCompile(source);
			}

			outputBytes_ = std::move(result.bytes);
			baseAddress_ = result.baseAddress;
			applyDiagnostics(result.diagnostics);
			lastCompileOk_ = true;

			// Check if any diagnostic was an error
			for (const auto& d : result.diagnostics) {
				if (d.error) {
					lastCompileOk_ = false;
					break;
				}
			}

			if (diagnosticsText_.empty()) {
				std::ostringstream oss;
				oss << "Compile success. "
					<< outputBytes_.size()
					<< " bytes generated.";
				diagnosticsText_ = oss.str();
			}
		}
		catch (const std::exception& e) {
			lastCompileOk_ = false;
			diagnosticsText_ = e.what();
			TextEditor::ErrorMarkers markers;
			markers[1] = e.what();
			editor_.SetErrorMarkers(markers);
		}
	}

	void applyDiagnostics(const std::vector<CompileDiagnostic>& diagnostics) {
		diagnosticsText_.clear();
		TextEditor::ErrorMarkers markers;
		std::ostringstream oss;

		for (const auto& d : diagnostics) {
			if (d.line > 0) {
				oss << "line " << d.line << ": ";
			}
			if (d.error) {
				oss << "error: ";
				if (d.line > 0) {
					markers[d.line] = d.message;
				}
				lastCompileOk_ = false;
			}
			else {
				oss << "warning: ";
			}
			oss << d.message << "\n";
		}

		diagnosticsText_ = oss.str();
		editor_.SetErrorMarkers(markers);
	}

	// ============================================================
	// Default compiler using lc::details
	// ============================================================
	static CompileResult defaultCompile(const std::string& source) {
		CompileResult result;
		result.baseAddress = 0x8154;

		// Split source into lines
		std::vector<std::string> lines;
		std::stringstream ss(source);
		std::string line;
		while (std::getline(ss, line)) {
			lines.push_back(line);
		}

		lc::CommandDatabase db;
		lc::Compiler compiler(db);

		compiler.compileLines(lines);
		compiler.finish();

		auto& st = compiler.state();

		// Determine home address
		int home = 0;
		if (st.home.has_value()) {
			home = *st.home;
		}
		else {
			home = static_cast<int>(result.baseAddress);
		}

		compiler.resolveAdrOf(home);
		result.baseAddress = static_cast<uint32_t>(home);

		// Convert vector<int> -> vector<uint8_t>
		result.bytes.reserve(st.result.size());
		for (int b : st.result) {
			result.bytes.push_back(static_cast<uint8_t>(b & 0xff));
		}

		return result;
	}

	// ============================================================
	// Utility
	// ============================================================
	std::string makeHexDumpText() const {
		std::ostringstream oss;
		for (size_t i = 0; i < outputBytes_.size(); ++i) {
			if (i != 0)
				oss << ' ';
			oss << std::uppercase << std::hex
				<< std::setw(2) << std::setfill('0')
				<< static_cast<unsigned>(outputBytes_[i]);
		}
		return oss.str();
	}

	void copyOutputHexToClipboard() {
		const std::string hex = makeHexDumpText();
		ImGui::SetClipboardText(hex.c_str());
	}
};

UIWindow* CreateRopCompilerWindow() {
	return new LibCompilerImGuiWindow({});
}