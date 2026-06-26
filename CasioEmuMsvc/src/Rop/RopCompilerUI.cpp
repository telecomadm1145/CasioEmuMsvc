// LibCompilerImGuiWindow.cpp
#include "Compiler.h"
#include "Localization.h"
#include "Models.h"
#ifdef CASIOEMU_CORE_WEB
#include "WebDebuggerGui.h"
#else
#include "SysDialog.h"
#endif
#include "TextEditor.h"
#include "Ui.hpp"
#include "hex.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
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
#ifdef CASIOEMU_CORE_WEB
		checkWebDatabaseImportResult();
#endif
		drawMenuBar();
		drawToolbar();
		ImGui::Separator();

		// Show database prompt banner if no database loaded
		if (!databaseLoaded_) {
			drawNoDatabaseBanner();
		}

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

	// Database state
	bool databaseLoaded_ = false;
	std::string databasePath_;
#ifdef CASIOEMU_CORE_WEB
	std::filesystem::path webDatabaseImportPath_;
#endif
	lc::CommandDatabase db_;
	int dbCommandCount_ = 0;
	int dbDataLabelCount_ = 0;
	std::string dbLoadError_;

	// Inject feedback (transient banner)
	double injectFeedbackTimer_ = 0.0;
	std::string injectFeedbackText_;

private:
	// ============================================================
	// RAM injection
	// ============================================================
	void injectToRam() {
		if (outputBytes_.empty() || !n_ram_buffer || !me_mmu)
			return;

		// Write bytes via MMU at the virtual address baseAddress_
		for (size_t i = 0; i < outputBytes_.size(); ++i) {
			uint32_t dest = baseAddress_ + static_cast<uint32_t>(i);
			me_mmu->WriteData(dest, outputBytes_[i], 0);
		}

		injectFeedbackText_ = g_local.Format("RopCompiler.InjectSuccess",
			(int)outputBytes_.size(), baseAddress_);
		injectFeedbackTimer_ = ImGui::GetTime();
	}

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
	// Database loading
	// ============================================================
	void openDatabaseDialog() {
#ifdef CASIOEMU_CORE_WEB
		const auto dir = std::filesystem::path("/tmp/imports");
		std::filesystem::create_directories(dir);
		webDatabaseImportPath_ = dir / "rop_database.txt";
		WebDebuggerQueueOpenFile(webDatabaseImportPath_.string().c_str(), "rop_database.txt");
#else
		SystemDialogs::OpenFileDialog([this](std::filesystem::path f) {
			loadDatabase(f.string());
		});
#endif
	}

#ifdef CASIOEMU_CORE_WEB
	void checkWebDatabaseImportResult() {
		if (webDatabaseImportPath_.empty())
			return;
		int result = 0;
		if (!WebDebuggerConsumeFileResult(webDatabaseImportPath_.string().c_str(), &result))
			return;
		const auto importPath = webDatabaseImportPath_;
		webDatabaseImportPath_.clear();
		if (result == 0) {
			loadDatabase(importPath.string());
		}
	}
#endif

	void loadDatabase(const std::string& path) {
		db_ = lc::CommandDatabase();
		dbCommandCount_ = 0;
		dbDataLabelCount_ = 0;
		dbLoadError_.clear();

		try {
			std::ifstream file(path);
			if (!file.is_open()) {
				dbLoadError_ = g_local.Format("RopCompiler.DbOpenFailed", path.c_str());
				databaseLoaded_ = false;
				return;
			}

			std::string line;
			int lineNum = 0;
			bool inBlockComment = false;

			while (std::getline(file, line)) {
				lineNum++;
				// Trim whitespace
				auto ltrim = [](std::string s) {
					s.erase(s.begin(), std::find_if(s.begin(), s.end(),
						[](unsigned char c) { return !std::isspace(c); }));
					return s;
				};
				auto rtrim = [](std::string s) {
					s.erase(std::find_if(s.rbegin(), s.rend(),
						[](unsigned char c) { return !std::isspace(c); }).base(),
						s.end());
					return s;
				};
				auto trim = [&](std::string s) { return rtrim(ltrim(std::move(s))); };

				line = trim(line);

				if (line.empty())
					continue;

				// Handle block comments
				if (inBlockComment) {
					if (line.find("*/") != std::string::npos)
						inBlockComment = false;
					continue;
				}
				if (line.find("/*") != std::string::npos) {
					if (line.find("*/") == std::string::npos)
						inBlockComment = true;
					continue;
				}

				// Handle line comments
				size_t commentPos = line.find('#');
				if (commentPos != std::string::npos) {
					line = line.substr(0, commentPos);
					line = trim(line);
					if (line.empty())
						continue;
				}

				// Parse address and name
				// Format: address<whitespace>name  or  address<tab>name
				std::istringstream iss(line);
				std::string addrStr;
				std::string name;
				iss >> addrStr;
				std::getline(iss, name);
				addrStr = trim(addrStr);
				name = trim(name);

				if (addrStr.empty() || name.empty())
					continue;

				// Labels starting with '.' are sub-labels, skip
				if (name.front() == '.')
					continue;

				// Parse address
				uint32_t address = 0;
				if (addrStr.starts_with("0x") || addrStr.starts_with("0X")) {
					address = static_cast<uint32_t>(std::strtoul(addrStr.c_str() + 2, nullptr, 16));
				}
				else {
					address = static_cast<uint32_t>(std::strtoul(addrStr.c_str(), nullptr, 16));
				}

				// Data labels start with "d_"
				if (name.starts_with("d_")) {
					try {
						db_.addDataLabel(name.substr(2), static_cast<int>(address));
						dbDataLabelCount_++;
					}
					catch (...) {
						// Skip duplicates silently
					}
				}
				else {
					try {
						db_.addCommand(static_cast<int>(address), name);
						dbCommandCount_++;
					}
					catch (...) {
						// Skip entries with illegal prefixes or duplicates
					}
				}
			}

			databasePath_ = path;
			databaseLoaded_ = true;
		}
		catch (const std::exception& e) {
			dbLoadError_ = e.what();
			databaseLoaded_ = false;
		}
	}

	// ============================================================
	// UI
	// ============================================================
	void drawNoDatabaseBanner() {
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.12f, 0.0f, 1.0f));
		ImGui::BeginChild("##no_db_banner", ImVec2(-1, 50), true);

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
		ImGui::TextUnformatted("RopCompiler.NoDatabaseWarning"_lc);
		ImGui::PopStyleColor();

		ImGui::SameLine();
		if (ImGui::SmallButton("RopCompiler.SelectDatabase"_lc)) {
			openDatabaseDialog();
		}

		ImGui::EndChild();
		ImGui::PopStyleColor();
	}

	void drawMenuBar() {
		if (!ImGui::BeginMenuBar())
			return;

		if (ImGui::BeginMenu("RopCompiler.MenuFile"_lc)) {
			if (ImGui::MenuItem("RopCompiler.New"_lc)) {
				editor_.SetText("");
				outputBytes_.clear();
				diagnosticsText_.clear();
				lastCompileOk_ = true;
			}
			if (ImGui::MenuItem("RopCompiler.Compile"_lc, "F5")) {
				compile();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("RopCompiler.OpenDatabase"_lc)) {
				openDatabaseDialog();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("RopCompiler.CopySource"_lc)) {
				ImGui::SetClipboardText(editor_.GetText().c_str());
			}
			if (ImGui::MenuItem("RopCompiler.CopyOutputHex"_lc)) {
				copyOutputHexToClipboard();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("RopCompiler.MenuView"_lc)) {
			ImGui::MenuItem("RopCompiler.ShowAscii"_lc, nullptr, &memoryEditor_.OptShowAscii);
			ImGui::MenuItem("RopCompiler.ShowOptions"_lc, nullptr, &memoryEditor_.OptShowOptions);
			ImGui::MenuItem("RopCompiler.ShowDataPreview"_lc, nullptr, &memoryEditor_.OptShowDataPreview);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("RopCompiler.MenuDatabase"_lc)) {
			if (ImGui::MenuItem("RopCompiler.OpenDatabase"_lc)) {
				openDatabaseDialog();
			}
			if (databaseLoaded_) {
				ImGui::Separator();
				auto dbInfo = g_local.Format("RopCompiler.DbInfo", dbCommandCount_, dbDataLabelCount_);
				ImGui::TextUnformatted(dbInfo.c_str());

				// Show shortened path
				std::filesystem::path p(databasePath_);
				auto filename = p.filename().string();
				auto pathLabel = g_local.Format("RopCompiler.DbFile", filename.c_str());
				ImGui::TextDisabled("%s", pathLabel.c_str());

				ImGui::Separator();
				if (ImGui::MenuItem("RopCompiler.ReloadDatabase"_lc)) {
					loadDatabase(databasePath_);
				}
				if (ImGui::MenuItem("RopCompiler.UnloadDatabase"_lc)) {
					db_ = lc::CommandDatabase();
					databaseLoaded_ = false;
					databasePath_.clear();
					dbCommandCount_ = 0;
					dbDataLabelCount_ = 0;
				}
			}
			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}

	void drawToolbar() {
		// ── Primary actions ──────────────────────────────────────
		// Compile button: green tint on OK, red tint on error
		if (lastCompileOk_) {
			ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.55f, 0.15f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.70f, 0.20f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.10f, 0.45f, 0.10f, 1.0f));
		} else {
			ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.12f, 0.12f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.18f, 0.18f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.45f, 0.08f, 0.08f, 1.0f));
		}
		if (ImGui::Button("RopCompiler.Compile"_lc)) {
			compile();
		}
		ImGui::PopStyleColor(3);

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
			ImGui::BeginTooltip();
			ImGui::TextDisabled("Shortcut: F5 / Ctrl+Enter");
			ImGui::EndTooltip();
		}

		ImGui::SameLine();

		// Inject to RAM button (only enabled when there is output)
		bool canInject = !outputBytes_.empty() && n_ram_buffer != nullptr;
		if (!canInject) ImGui::BeginDisabled();
		if (ImGui::Button("RopCompiler.InjectToRam"_lc)) {
			injectToRam();
		}
		if (!canInject) ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
			ImGui::BeginTooltip();
			ImGui::Text("RopCompiler.InjectToRamTooltip"_lc,
				static_cast<unsigned>(outputBytes_.size()), baseAddress_);
			ImGui::EndTooltip();
		}

		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();

		// ── Database badge ────────────────────────────────────────
		if (databaseLoaded_) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.95f, 0.35f, 1.0f));
			ImGui::TextUnformatted("[DB]");
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) {
				ImGui::BeginTooltip();
				auto tipText = g_local.Format("RopCompiler.DbTooltip",
					std::filesystem::path(databasePath_).filename().string().c_str(),
					dbCommandCount_, dbDataLabelCount_);
				ImGui::TextUnformatted(tipText.c_str());
				ImGui::EndTooltip();
			}
		} else {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
			ImGui::TextUnformatted("[No DB]");
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("RopCompiler.NoDbTooltip"_lc);
				ImGui::EndTooltip();
			}
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("RopCompiler.LoadDatabase"_lc)) {
			openDatabaseDialog();
		}

		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();

		// ── Base address + length ─────────────────────────────────
		ImGui::TextUnformatted("RopCompiler.Base"_lc);
		ImGui::SameLine();
		int base = static_cast<int>(baseAddress_);
		ImGui::SetNextItemWidth(90.0f);
		if (ImGui::InputInt("##base_address", &base, 0, 0, ImGuiInputTextFlags_CharsHexadecimal)) {
			if (base < 0) base = 0;
			baseAddress_ = static_cast<uint32_t>(base);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%s 0x%04X / %zu bytes",
			"RopCompiler.Length"_lc,
			static_cast<unsigned>(outputBytes_.size()),
			outputBytes_.size());

		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();

		// ── Secondary actions ─────────────────────────────────────
		if (ImGui::SmallButton("RopCompiler.CopyHex"_lc)) {
			copyOutputHexToClipboard();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("RopCompiler.ClearOutput"_lc)) {
			outputBytes_.clear();
			diagnosticsText_.clear();
			editor_.SetErrorMarkers({});
			lastCompileOk_ = true;
		}

		// Show database load error if any (inline red badge)
		if (!dbLoadError_.empty()) {
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, UIHelpers::kColorError);
			ImGui::TextUnformatted(dbLoadError_.c_str());
			ImGui::PopStyleColor();
		}

		// Show inject feedback
		if (injectFeedbackTimer_ > 0.0) {
			double elapsed = ImGui::GetTime() - injectFeedbackTimer_;
			if (elapsed < 3.0) {
				ImGui::SameLine();
				float alpha = (float)std::max(0.0, 1.0 - elapsed / 1.5);
				ImGui::PushStyleColor(ImGuiCol_Text,
					ImVec4(UIHelpers::kColorSuccess.x, UIHelpers::kColorSuccess.y,
						UIHelpers::kColorSuccess.z, alpha));
				ImGui::TextUnformatted(injectFeedbackText_.c_str());
				ImGui::PopStyleColor();
			} else {
				injectFeedbackTimer_ = 0.0;
			}
		}
	}


	void drawSourcePanel(float height) {
		ImGui::BeginChild("##source_panel", ImVec2(-1, height), true);
		ImGui::TextUnformatted("RopCompiler.SourceTitle"_lc);
		ImGui::Separator();
		const float footerHeight = 24.0f;
		editor_.Render("##source_editor",
			ImVec2(-1, ImGui::GetContentRegionAvail().y - footerHeight), true);
		auto cpos = editor_.GetCursorPosition();
		auto statusText = g_local.Format("RopCompiler.CursorStatus",
			cpos.mLine + 1, cpos.mColumn + 1, editor_.GetTotalLines());
		ImGui::TextUnformatted(statusText.c_str());
		ImGui::EndChild();
	}

	void drawOutputPanel(float height) {
		ImGui::BeginChild("##output_panel", ImVec2(-1, height), true);
		ImGui::TextUnformatted("RopCompiler.OutputTitle"_lc);
		ImGui::TextDisabled("github/ocornut/imgui_club");
		ImGui::Separator();

		if (ImGui::BeginTabBar("##output_tabs")) {
			if (ImGui::BeginTabItem("RopCompiler.TabHexEditor"_lc)) {
				if (!outputBytes_.empty()) {
					memoryEditor_.DrawContents(
						outputBytes_.data(), outputBytes_.size(), baseAddress_);
				}
				else {
					ImGui::TextDisabled("%s", "RopCompiler.NoOutputHint"_lc);
				}
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("RopCompiler.TabHexText"_lc)) {
				std::string hex = makeHexDumpText();
				ImGui::InputTextMultiline("##hex_text", hex.data(),
					hex.size() + 1, ImVec2(-1, -1),
					ImGuiInputTextFlags_ReadOnly);
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("RopCompiler.TabRawBytes"_lc)) {
				if (outputBytes_.empty()) {
					ImGui::TextDisabled("%s", "RopCompiler.NoBytes"_lc);
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
		ImGui::TextUnformatted("RopCompiler.DiagnosticsTitle"_lc);
		ImGui::Separator();

		if (diagnosticsText_.empty()) {
			ImGui::TextDisabled("%s", "RopCompiler.NoDiagnostics"_lc);
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
				diagnosticsText_ = g_local.Format("RopCompiler.CompileSuccess",
					(int)outputBytes_.size());
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
				oss << g_local.Format("RopCompiler.DiagLine", d.line) << " ";
			}
			if (d.error) {
				oss << g_local["RopCompiler.DiagError"] << ": ";
				if (d.line > 0) {
					markers[d.line] = d.message;
				}
				lastCompileOk_ = false;
			}
			else {
				oss << g_local["RopCompiler.DiagWarning"] << ": ";
			}
			oss << d.message << "\n";
		}

		diagnosticsText_ = oss.str();
		editor_.SetErrorMarkers(markers);
	}

	// ============================================================
	// Default compiler using lc::details
	// ============================================================
	CompileResult defaultCompile(const std::string& source) {
		CompileResult result;
		result.baseAddress = 0x8154;

		// Split source into lines
		std::vector<std::string> lines;
		std::stringstream ss(source);
		std::string line;
		while (std::getline(ss, line)) {
			lines.push_back(line);
		}

		lc::Compiler compiler(db_);

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
