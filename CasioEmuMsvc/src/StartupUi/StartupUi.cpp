// ???
#include <SDL_rect.h>
#include "ModelConfig.h"
#include "ModelInfo.h"
// 

#include "StartupUi.h"
#include "OnlineModelClient.h"
#include "OnlineModelPackage.h"
#include "3rd_licenses.h"
#include "Binary.h"
#include "Config.hpp"
#include "Gui/imgui/imgui.h"
#include "Gui/imgui/imgui_impl_sdl2.h"
#include "Gui/imgui/imgui_impl_sdlrenderer2.h"
#include "Localization.h"
#include "RendererBackend.h"
#include "RomPackage.h"
#include "Romu.h"
#include "SysDialog.h"
#include "Ui.hpp"
#include "sdl_win32_extra.h"
#include <Gui.h>
#include <SDL.h>
#include <SDL_image.h>
#include <array>
#include <filesystem>
#include <imgui.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>

#ifdef _WIN32
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windows.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#endif
#include "Ext/Random.hpp"
#include "DiscordRPC.h"

#include <algorithm>
#include <cctype>

#ifdef __ANDROID__
#include "../Gui/ThemeManager.h"
#include <SDL.h>
#include <SDL_system.h>
#include <android/log.h>
#include <jni.h>

static JavaVM* g_VM = nullptr;

extern "C"
{
	JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
		g_VM = vm;
		return JNI_VERSION_1_6;
	}
}

// Helper function to get JNIEnv
bool GetJNIEnv(JNIEnv** env) {
	if (g_VM == nullptr) {
		return false;
	}

	jint result = g_VM->GetEnv((void**)env, JNI_VERSION_1_6);
	if (result == JNI_EDETACHED) {
		if (g_VM->AttachCurrentThread(env, nullptr) != 0) {
			return false;
		}
	}
	else if (result != JNI_OK) {
		return false;
	}
	return true;
}

// Helper function to detach current thread from JVM
void DetachCurrentThread() {
	if (g_VM != nullptr) {
		g_VM->DetachCurrentThread();
	}
}
#endif

inline SDL_Window* window2;
inline SDL_Renderer* renderer2;
inline std::vector<UIWindow*>* windows2;

void RenderClippedSprite(SDL_Renderer* renderer, SDL_Texture* texture, const casioemu::SpriteInfo& sprite) {
	SDL_Rect srcRect = sprite.src;
	SDL_Rect destRect = sprite.dest;

	// Calculate the aspect ratio of src and dest
	float aspectRatioSrc = (float)srcRect.w / (float)srcRect.h;
	float aspectRatioDest = (float)destRect.w / (float)destRect.h;

	// Adjust the srcRect to fit within the destRect, keeping the aspect ratio
	if (aspectRatioSrc > aspectRatioDest) {
		int newHeight = srcRect.w / aspectRatioDest;
		srcRect.y += (srcRect.h - newHeight) / 2;
		srcRect.h = newHeight;
	}
	else {
		int newWidth = srcRect.h * aspectRatioDest;
		srcRect.x += (srcRect.w - newWidth) / 2;
		srcRect.w = newWidth;
	}

	// Render the texture using the adjusted source and destination rectangles
	SDL_RenderCopy(renderer, texture, &srcRect, &destRect);
}

class ModelEditor : public UIWindow {
	std::filesystem::path pth;
	casioemu::ModelInfo mi;
	int v;
	int k;
	static constexpr const char* items[10] = {"##1", "##2", "##3", "ES(P)", "CWX", "CWII", "Fx5800p", "TI", "SolarII", "EPS6800"};
	char path1[260];
	char path2[260];
	char path3[260];
	char name[260];
	SDL_Texture* sdl_t = 0;
	ImVec2 imgSz;
	casioemu::SpriteInfo imgSp;
	float color[3];

	char buffer[260];
	char buffer2[12];

	casioemu::ButtonInfo* btninfo{};
	bool init_failed = false;
	std::string selected_sprite_key;

public:
	ModelEditor(std::filesystem::path path) : UIWindow("Model Editor##114514"), pth(path) {
		try {
			std::string error;
			if (!casioemu::LoadModelInfoFromFolder(path, mi, nullptr, &error)) {
				throw std::runtime_error(error);
			}
			v = mi.csr_mask;
			k = mi.pd_value;
			strncpy(path1, mi.interface_path.c_str(), sizeof(path1) - 1);
			path1[sizeof(path1) - 1] = '\0';
			strncpy(path2, mi.rom_path.c_str(), sizeof(path2) - 1);
			path2[sizeof(path2) - 1] = '\0';
			strncpy(path3, mi.flash_path.c_str(), sizeof(path3) - 1);
			path3[sizeof(path3) - 1] = '\0';
			strncpy(name, mi.model_name.c_str(), sizeof(name) - 1);
			name[sizeof(name) - 1] = '\0';
			color[0] = mi.ink_color.r / 255.0f;
			color[1] = mi.ink_color.g / 255.0f;
			color[2] = mi.ink_color.b / 255.0f;
			LoadInterface();
		}
		catch (const std::exception& e) {
			std::cerr << "Failed to load ModelEditor: " << e.what() << std::endl;
			init_failed = true;
		}
	}
	~ModelEditor() override {
		if (sdl_t)
			SDL_DestroyTexture(sdl_t);
	}
	bool IsBoardModel() const {
		return !mi.board_path.empty();
	}
	static bool HasSvgExtension(const std::filesystem::path& path) {
		auto ext = path.extension().string();
		for (auto& ch : ext)
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		return ext == ".svg";
	}
	SDL_Surface* LoadInterfaceSurface(const std::filesystem::path& path, const casioemu::SpriteInfo& interface_sprite) {
		if (!HasSvgExtension(path))
			return IMG_Load(path.string().c_str());

		std::ifstream stream(path, std::ios::binary);
		if (!stream)
			return nullptr;
		const std::string svg{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
		if (svg.empty())
			return nullptr;

		const int width = interface_sprite.src.w > 0 ? interface_sprite.src.w : 1;
		const int height = interface_sprite.src.h > 0 ? interface_sprite.src.h : 1;
		SDL_RWops* rw = SDL_RWFromConstMem(svg.data(), static_cast<int>(svg.size()));
		if (!rw)
			return nullptr;
		SDL_Surface* surface = IMG_LoadSizedSVG_RW(rw, width, height);
		SDL_RWclose(rw);
		if (!surface)
			SDL_Log("[StartupUI][Warn] IMG_LoadSizedSVG_RW failed for editor interface SVG: %s", IMG_GetError());
		return surface;
	}
	void RenderSprite(const casioemu::SpriteInfo& sprite, ImTextureID texture_id, const ImVec2& texture_size, const ImVec2& render_size) {

		// 计算 UV 坐标
		ImVec2 uv0(
			sprite.src.x / texture_size.x,
			sprite.src.y / texture_size.y);
		ImVec2 uv1(
			(sprite.src.x + sprite.src.w) / texture_size.x,
			(sprite.src.y + sprite.src.h) / texture_size.y);
		ImVec4 tint_clr{1, 1, 1, 1};
		if (!mi.enable_new_screen) {
			tint_clr.x = mi.ink_color.r / 255.0f;
			tint_clr.y = mi.ink_color.g / 255.0f;
			tint_clr.z = mi.ink_color.b / 255.0f;
		}
		// 渲染裁剪后的图像
		ImGui::Image(
			texture_id,
			render_size,
			uv0,
			uv1, tint_clr);
	}
	void RenderSprite2(const casioemu::SpriteInfo& sprite, ImTextureID texture_id, const ImVec2& texture_size, const ImVec2& render_size) {

		// 计算 UV 坐标
		ImVec2 uv0(
			sprite.src.x / texture_size.x,
			sprite.src.y / texture_size.y);
		ImVec2 uv1(
			(sprite.src.x + sprite.src.w) / texture_size.x,
			(sprite.src.y + sprite.src.h) / texture_size.y);
		// 渲染裁剪后的图像
		ImGui::Image(
			texture_id,
			render_size,
			uv0,
			uv1);
	}
	void RenderSprite3(const casioemu::SpriteInfo& sprite, ImTextureID texture_id, const ImVec2& texture_size, const ImVec2& render_size) {

		// 计算 UV 坐标
		ImVec2 uv0(
			sprite.src.x / texture_size.x,
			sprite.src.y / texture_size.y);
		ImVec2 uv1(
			(sprite.src.x + sprite.src.w) / texture_size.x,
			(sprite.src.y + sprite.src.h) / texture_size.y);
		ImVec4 tint_clr{1, 1, 1, 1};
		tint_clr.x = mi.ink_color.r / 255.0f;
		tint_clr.y = mi.ink_color.g / 255.0f;
		tint_clr.z = mi.ink_color.b / 255.0f;
		// 渲染裁剪后的图像
		ImGui::Image(
			texture_id,
			render_size,
			uv0,
			uv1, tint_clr);
	}
	void LoadInterface() {
		if (sdl_t) {
			SDL_DestroyTexture(sdl_t);
			sdl_t = nullptr;
		}
		if (mi.sprites.find("rsd_interface") != mi.sprites.end()) {
			SDL_Surface* surface = LoadInterfaceSurface(pth / mi.interface_path, mi.sprites["rsd_interface"]);
			if (surface) {
				sdl_t = SDL_CreateTextureFromSurface(renderer2, surface);
				imgSz = {(float)surface->w, (float)surface->h};
				SDL_FreeSurface(surface);
			}
			imgSp = mi.sprites["rsd_interface"];
		}
	}
	void RenderCore() override {
		if (init_failed) {
			ImGui::TextColored({1.0f, 0.0f, 0.0f, 1.0f}, "Failed to load model configuration. The file might be corrupted.");
			if (ImGui::Button("Close")) {
				open = false;
			}
			return;
		}

		auto y = ImGui::GetCursorPosY();
		const bool is_board_model = IsBoardModel();
		const int model_width = is_board_model ? imgSp.dest.w : imgSp.src.w;
		auto scaleFactor = model_width > 0 ? (400.f / model_width) : 1.0f;
		if (sdl_t != 0) {
			ImGui::SetCursorPosX(0);
			RenderSprite2(imgSp, (ImTextureID)sdl_t, imgSz, {400, 400.0f * imgSp.dest.h / imgSp.dest.w});
			if (!is_board_model) {
				for (auto& sp : mi.sprites) {
					if (sp.first != "rsd_pixel" && sp.first != "rsd_interface") {
						ImGui::SetCursorPos({(float)sp.second.dest.x * scaleFactor, (float)sp.second.dest.y * scaleFactor + y});
						RenderSprite(sp.second, (ImTextureID)sdl_t, imgSz, {scaleFactor * (float)sp.second.dest.w, scaleFactor * (float)sp.second.dest.h});
						if (sp.first == selected_sprite_key) {
							auto min_p = ImGui::GetItemRectMin();
							auto max_p = ImGui::GetItemRectMax();
							ImGui::GetWindowDrawList()->AddRect(min_p, max_p, IM_COL32(255, 0, 0, 255), 0.0f, 0, 2.0f);
						}
					}
				}
				auto sp2 = mi.sprites["rsd_pixel"];
				if (mi.hardware_id == casioemu::HW_ES_PLUS || mi.hardware_id == casioemu::HW_FX_5800P || mi.hardware_id == casioemu::HW_EPS6800) {
					for (size_t j = 0; j < 31; j++) {
						for (size_t i = 0; i < 96; i++) {
							ImGui::SetCursorPos({(float)(sp2.dest.x + i * sp2.dest.w) * scaleFactor, (float)(sp2.dest.y + j * sp2.dest.h) * scaleFactor + y});
							RenderSprite3(sp2, (ImTextureID)sdl_t, imgSz, {scaleFactor * (float)sp2.dest.w, scaleFactor * (float)sp2.dest.h});
						}
					}
				}
				else {
					for (size_t j = 0; j < 63; j++) {
						for (size_t i = 0; i < 192; i++) {
							ImGui::SetCursorPos({(float)(sp2.dest.x + i * sp2.dest.w) * scaleFactor, (float)(sp2.dest.y + j * sp2.dest.h) * scaleFactor + y});
							RenderSprite3(sp2, (ImTextureID)sdl_t, imgSz, {scaleFactor * (float)sp2.dest.w, scaleFactor * (float)sp2.dest.h});
						}
					}
				}
			}
		}
		else {
			ImGui::SetCursorPosX(0);
			ImGui::Dummy({400, 600});
			ImGui::SetCursorPos({0, y});
			ImGui::TextUnformatted("Failed to load interface preview.");
		}
		for (size_t button_index = 0; button_index < mi.buttons.size(); ++button_index) {
			auto& btn = mi.buttons[button_index];
			ImGui::SetCursorPos({scaleFactor * btn.rect.x, scaleFactor * btn.rect.y + y});
			ImGui::PushID(btn.kiko + 20);
			const ImVec2 button_size{scaleFactor * btn.rect.w, scaleFactor * btn.rect.h};
			const bool clicked = is_board_model
				? ImGui::InvisibleButton(btn.keyname.c_str(), button_size)
				: ImGui::Button(btn.keyname.c_str(), button_size);
			if (clicked) {
				btninfo = &btn;
				strncpy(buffer, btn.keyname.c_str(), sizeof(buffer) - 1);
				buffer[sizeof(buffer) - 1] = '\0';
				SDL_itoa(btn.kiko, buffer2, 16);
			}
			if (is_board_model && btninfo == &btn) {
				auto min_p = ImGui::GetItemRectMin();
				auto max_p = ImGui::GetItemRectMax();
				ImGui::GetWindowDrawList()->AddRect(min_p, max_p, IM_COL32(255, 0, 0, 255), 0.0f, 0, 2.0f);
			}
			ImGui::PopID();
		}
		ImGui::SetCursorPos({400, y});
		if (ImGui::BeginChild("Model Info")) {
			if (ImGui::BeginTabBar("ModelEditorTabs")) {
				if (ImGui::BeginTabItem("General")) {
					ImGui::TextUnformatted("ModelEditor.Name"_lc);
					if (ImGui::InputText("##name", name, 260)) {
						mi.model_name = name;
					}
					ImGui::TextUnformatted("ModelEditor.InterfacePath"_lc);
					if (ImGui::InputText("##path1", path1, 260)) {
						mi.interface_path = path1;
					}
					ImGui::TextUnformatted("ModelEditor.RomPath"_lc);
					if (ImGui::InputText("##path2", path2, 260)) {
						mi.rom_path = path2;
					}
					ImGui::TextUnformatted("ModelEditor.FlashDumpPath"_lc);
					if (ImGui::InputText("##path3", path3, 260)) {
						mi.flash_path = path3;
					}
					ImGui::TextUnformatted("ModelEditor.CsrMask"_lc);
					if (ImGui::SliderInt("##a", &v, 0, 15, "0x%X")) {
						mi.csr_mask = v;
					}
					ImGui::TextUnformatted("ModelEditor.PdValue"_lc);
					if (ImGui::SliderInt("##q", &k, 0, 15, "0x%X")) {
						mi.pd_value = k;
					}
					ImGui::TextUnformatted("ModelEditor.HardwareType"_lc);
					ImGui::SetNextItemWidth(80);
					if (ImGui::BeginCombo("##cb", items[mi.hardware_id])) {
						for (int n = 0; n < IM_ARRAYSIZE(items); n++) {
							bool is_selected = (mi.hardware_id == n);
							if (ImGui::Selectable(items[n], is_selected)) {
								mi.hardware_id = n;
							}
							if (is_selected)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					ImGui::Checkbox("ModelEditor.SampleRom"_lc, &mi.is_sample_rom);
					ImGui::Checkbox("ModelEditor.NewRenderMethod"_lc, &mi.enable_new_screen);
					ImGui::Checkbox("ModelEditor.RealRom"_lc, &mi.real_hardware);
					ImGui::Checkbox("ModelEditor.LegacyKO"_lc, &mi.legacy_ko);
					if (ImGui::ColorEdit3("ModelEditor.InkColor"_lc, color)) {
						mi.ink_color.r = color[0] * 255;
						mi.ink_color.g = color[1] * 255;
						mi.ink_color.b = color[2] * 255;
					}
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Buttons")) {
					if (is_board_model) {
						if (btninfo) {
							ImGui::TextUnformatted("Button geometry is read from board.svg.");
							if (ImGui::InputText("ModelEditor.KeyName"_lc, buffer, 260)) {
								btninfo->keyname = buffer;
							}
							if (ImGui::InputText("ModelEditor.KIKO"_lc, buffer2, 12)) {
								btninfo->kiko = SDL_strtol(buffer2, 0, 16);
							}
						}
						else {
							ImGui::Text("Select a button from the preview to edit.");
						}
					}
					else if (btninfo) {
						if (ImGui::InputText("ModelEditor.KeyName"_lc, buffer, 260)) {
							btninfo->keyname = buffer;
						}
						if (ImGui::InputText("ModelEditor.KIKO"_lc, buffer2, 12)) {
							btninfo->kiko = SDL_strtol(buffer2, 0, 16);
						}
						ImGui::InputInt("X", &btninfo->rect.x);
						ImGui::InputInt("Y", &btninfo->rect.y);
						ImGui::InputInt("W", &btninfo->rect.w);
						ImGui::InputInt("H", &btninfo->rect.h);
					}
					else {
						ImGui::Text("Select a button from the preview to edit.");
					}
					ImGui::EndTabItem();
				}
				if (!is_board_model && ImGui::BeginTabItem("Sprites")) {
					ImGui::Text("Sprite List");
					ImGui::BeginChild("SpriteList", {0, 150}, true);

					for (auto& [key, sprite] : mi.sprites) {
						if (ImGui::Selectable(key.c_str(), selected_sprite_key == key)) {
							selected_sprite_key = key;
						}
					}
					ImGui::EndChild();

					if (!selected_sprite_key.empty() && mi.sprites.count(selected_sprite_key)) {
						auto& sprite = mi.sprites[selected_sprite_key];
						ImGui::Text("Selected: %s", selected_sprite_key.c_str());
						ImGui::Separator();
						ImGui::Text("Source Rect");
						ImGui::InputInt("Src X", &sprite.src.x);
						ImGui::InputInt("Src Y", &sprite.src.y);
						ImGui::InputInt("Src W", &sprite.src.w);
						ImGui::InputInt("Src H", &sprite.src.h);

						ImGui::Text("Dest Rect");
						ImGui::InputInt("Dest X", &sprite.dest.x);
						ImGui::InputInt("Dest Y", &sprite.dest.y);
						ImGui::InputInt("Dest W", &sprite.dest.w);
						ImGui::InputInt("Dest H", &sprite.dest.h);
					}
					ImGui::EndTabItem();
				}
				if (is_board_model && ImGui::BeginTabItem("Status")) {
					if (mi.status_sprite_indexes.empty()) {
						ImGui::TextUnformatted("No status sprites configured.");
					}
					else {
						for (auto& [key, index] : mi.status_sprite_indexes) {
							ImGui::PushID(key.c_str());
							ImGui::TextUnformatted(key.c_str());
							ImGui::SameLine(160.0f);
							ImGui::SetNextItemWidth(80.0f);
							ImGui::InputInt("Index", &index);
							ImGui::PopID();
						}
					}
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}

			ImGui::Separator();
			if (ImGui::Button("Button.Save"_lc)) {
				std::set<int> used_kiko;
				bool duplicate_kiko = false;
				for (const auto& button : mi.buttons) {
					if (!used_kiko.insert(button.kiko).second) {
						duplicate_kiko = true;
						break;
					}
				}
				if (duplicate_kiko) {
					SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "Duplicate KIKO values are not allowed.", nullptr);
					return;
				}
				casioemu::SaveModelInfoJson(pth, mi);
				this->open = false;
			}
		}
		ImGui::EndChild();
	}
};

inline char* stristr(const char* str1, const char* str2) {
	const char* p1 = str1;
	const char* p2 = str2;
	const char* r = *p2 == 0 ? str1 : 0;

	while (*p1 != 0 && *p2 != 0) {
		if (tolower((unsigned char)*p1) == tolower((unsigned char)*p2)) {
			if (r == 0) {
				r = p1;
			}

			p2++;
		}
		else {
			p2 = str2;
			if (r != 0) {
				p1 = r + 1;
			}

			if (tolower((unsigned char)*p1) == tolower((unsigned char)*p2)) {
				r = p1;
				p2++;
			}
			else {
				r = 0;
			}
		}

		p1++;
	}

	return *p2 == 0 ? (char*)r : 0;
}
inline std::string tohex(int n, int len) {
	std::string retval = "";
	for (int x = 0; x < len; x++) {
		retval = "0123456789ABCDEF"[n & 0xF] + retval;
		n >>= 4;
	}
	return retval;
}
inline std::string tohex(unsigned long long n, int len) {
	std::string retval = "";
	for (int x = 0; x < len; x++) {
		retval = "0123456789ABCDEF"[n & 0xF] + retval;
		n >>= 4;
	}
	return retval;
}
inline std::string sanitize_path(const std::string& input) {
	std::string result;
	for (unsigned char c : input) {
		// Allow ASCII alphanumeric, underscore, hyphen, dot, and space
		if ((c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') ||
			c == '_' || c == '-' || c == '.' || c == ' ') {
			result += c;
		}
		// Replace non-ASCII characters and unsafe characters with underscore
		else if ((unsigned char)c > 127 || c < 32) {
			result += '_';
		}
	}
	// Remove leading/trailing spaces and dots
	size_t start = result.find_first_not_of(" .");
	size_t end = result.find_last_not_of(" .");
	if (start != std::string::npos) {
		result = result.substr(start, end - start + 1);
	}
	else {
		result = "unnamed"; // Fallback if string becomes empty
	}
	// Collapse consecutive underscores
	size_t pos = 0;
	while ((pos = result.find("__", pos)) != std::string::npos) {
		result.erase(pos, 1);
	}
	return result;
}
#ifdef _WIN32
static bool CreateDesktopShortcut(const std::filesystem::path& model_path, const std::string& shortcut_name, const std::string& icon_path_str) {
	// Get the desktop path
	wchar_t desktopPath[MAX_PATH];
	if (FAILED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath))) {
		std::cerr << "[Shortcut] Failed to get desktop path\n";
		return false;
	}

	// Get the executable path
	wchar_t exePath[MAX_PATH];
	GetModuleFileNameW(NULL, exePath, MAX_PATH);

	// Get the working directory (exe directory)
	std::filesystem::path exe_dir = std::filesystem::path(exePath).parent_path();

	// Build the argument: the model path (absolute)
	std::filesystem::path abs_model = std::filesystem::absolute(model_path);
	std::wstring arguments = L"\"" + abs_model.wstring() + L"\"";

	// Build shortcut file path
	std::wstring sanitized_name(shortcut_name.begin(), shortcut_name.end());
	std::wstring lnkPath = std::wstring(desktopPath) + L"\\" + sanitized_name + L".lnk";

	IShellLinkW* psl = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl);
	if (FAILED(hr)) {
		std::cerr << "[Shortcut] CoCreateInstance failed\n";
		return false;
	}

	psl->SetPath(exePath);
	psl->SetArguments(arguments.c_str());
	psl->SetWorkingDirectory(exe_dir.wstring().c_str());
	psl->SetDescription(L"CasioEmu Model Shortcut");

	// Set custom icon if provided
	if (!icon_path_str.empty()) {
		std::filesystem::path icon_abs = std::filesystem::absolute(icon_path_str);
		if (std::filesystem::exists(icon_abs)) {
			psl->SetIconLocation(icon_abs.wstring().c_str(), 0);
		}
	}

	IPersistFile* ppf = nullptr;
	hr = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
	if (SUCCEEDED(hr)) {
		hr = ppf->Save(lnkPath.c_str(), TRUE);
		ppf->Release();
	}
	psl->Release();

	if (FAILED(hr)) {
		std::cerr << "[Shortcut] Failed to save shortcut\n";
		return false;
	}
	return true;
}
#elif defined(__ANDROID__)
static bool CreateDesktopShortcut(const std::filesystem::path& model_path, const std::string& shortcut_name, const std::string& icon_path_str) {
	JNIEnv* env = nullptr;
	if (!GetJNIEnv(&env)) {
		std::cerr << "[Shortcut] Failed to get JNI environment\n";
		return false;
	}

	jobject activity = (jobject)SDL_AndroidGetActivity();
	if (!activity) {
		std::cerr << "[Shortcut] Failed to get Android activity\n";
		return false;
	}

	jclass gameClass = env->FindClass("com/tele/u8emulator/Game");
	if (!gameClass) {
		env->DeleteLocalRef(activity);
		std::cerr << "[Shortcut] Failed to find Game class\n";
		return false;
	}

	jmethodID method = env->GetStaticMethodID(gameClass, "createModelShortcut",
		"(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
	if (!method) {
		env->DeleteLocalRef(gameClass);
		env->DeleteLocalRef(activity);
		std::cerr << "[Shortcut] Failed to find createModelShortcut method\n";
		return false;
	}

	std::filesystem::path abs_model = std::filesystem::absolute(model_path);
	jstring jModelPath = env->NewStringUTF(abs_model.string().c_str());
	jstring jName = env->NewStringUTF(shortcut_name.c_str());
	jstring jIconPath = env->NewStringUTF(icon_path_str.c_str());

	env->CallStaticVoidMethod(gameClass, method, jModelPath, jName, jIconPath);

	env->DeleteLocalRef(jIconPath);
	env->DeleteLocalRef(jName);
	env->DeleteLocalRef(jModelPath);
	env->DeleteLocalRef(gameClass);
	env->DeleteLocalRef(activity);
	return true;
}
#elif defined(__linux__)
static bool CreateDesktopShortcut(const std::filesystem::path& model_path, const std::string& shortcut_name, const std::string& icon_path_str) {
	// Get the desktop directory
	std::filesystem::path desktop_dir;
	const char* xdg_desktop = std::getenv("XDG_DESKTOP_DIR");
	if (xdg_desktop) {
		desktop_dir = xdg_desktop;
	}
	else {
		const char* home = std::getenv("HOME");
		if (!home) {
			std::cerr << "[Shortcut] HOME environment variable not set\n";
			return false;
		}
		desktop_dir = std::filesystem::path(home) / "Desktop";
	}

	if (!std::filesystem::exists(desktop_dir)) {
		std::filesystem::create_directories(desktop_dir);
	}

	// Get executable path
	std::filesystem::path exe_path = std::filesystem::canonical("/proc/self/exe");
	std::filesystem::path abs_model = std::filesystem::absolute(model_path);

	std::string sanitized = shortcut_name;
	for (auto& c : sanitized) {
		if (c == '/' || c == '\\')
			c = '_';
	}

	std::filesystem::path shortcut_file = desktop_dir / (sanitized + ".desktop");

	std::ofstream ofs(shortcut_file);
	if (!ofs) {
		std::cerr << "[Shortcut] Failed to create .desktop file\n";
		return false;
	}

	ofs << "[Desktop Entry]\n";
	ofs << "Type=Application\n";
	ofs << "Name=" << shortcut_name << "\n";
	ofs << "Exec=\"" << exe_path.string() << "\" \"" << abs_model.string() << "\"\n";
	ofs << "Path=" << exe_path.parent_path().string() << "\n";
	if (!icon_path_str.empty()) {
		std::filesystem::path icon_abs = std::filesystem::absolute(icon_path_str);
		if (std::filesystem::exists(icon_abs)) {
			ofs << "Icon=" << icon_abs.string() << "\n";
		}
	}
	ofs << "Terminal=false\n";
	ofs << "Categories=Game;Emulator;\n";
	ofs.close();

	// Make executable
	std::filesystem::permissions(shortcut_file,
		std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
		std::filesystem::perm_options::add);

	return true;
}
#elif defined(__APPLE__)
static bool CreateDesktopShortcut(const std::filesystem::path& model_path, const std::string& shortcut_name, const std::string& icon_path_str) {
	std::cerr << "[Shortcut] macOS shortcut creation not implemented yet\n";
	return false;
}
#endif

namespace casioemu {
	class StartupUi {
	public:
		std::map<std::array<char, 8>, std::string> RomNames;
		struct Model {
		public:
			std::filesystem::path path;
			std::string name;
			std::string version;
			std::string type;
			std::string id;
			std::string checksum;
			std::string checksum2;
			std::string sum_good;
			bool realhw;
			bool show_sum = true;
		};
		std::vector<Model> models;
		std::filesystem::path selected_path{};
		std::shared_ptr<ModelResourceStore> selected_resources;
		char online_api[512] = "http://127.0.0.1:8788";
		bool show_online_popup = false;
		OnlineAuthRequest online_auth{};
		std::string online_access_token;
		bool online_authorization_pending = false;
		std::string online_status;
		std::vector<OnlineModelEntry> online_models;
		int selected_online_model = -1;
		StartupUi() {
			std::ifstream api_settings{"online_api.cfg"};
			if (api_settings) {
				std::string value;
				std::getline(api_settings, value);
				if (!value.empty())
					std::snprintf(online_api, sizeof(online_api), "%s", value.c_str());
			}
			try {
				OnlineModelClient client{online_api};
				online_access_token = LoadOnlineToken(client.ApiBase());
			}
			catch (...) {
				online_access_token.clear();
			}
			std::ifstream ifs2{"roms.db", std::ifstream::binary};
			if (ifs2) {
				try {
					Binary::Read(ifs2, RomNames);
				}
				catch (const std::exception& e) {
					std::cerr << "[StartupUI][Warn] Failed to read \"roms.db\": " << e.what() << ". Names may be inaccurate!\n";
				}
			}
			else {
				printf("[StartupUI][Warn] \"roms.db\" not found. Names may be inaccurate!\n");
			}
			Reload();
		}
		void Reload() {
			loading = true;
			std::filesystem::create_directory("models");
			std::thread thd([&]() {
				models.clear();
				for (auto& dir : std::filesystem::directory_iterator("models")) {
					if (dir.path().filename() == ".online") continue;
					if (dir.is_directory()) {
						try {
							printf("[StartupUI][Info] Checking %s\n", dir.path().string().c_str());
							std::error_code ec;
							std::string load_error;
							ModelInfo mi{};
							if (!LoadModelInfoFromFolder(dir.path(), mi, nullptr, &load_error)) {
								printf("[StartupUI][Info] Unable to load model configuration for %s: %s\n", dir.path().string().c_str(), load_error.c_str());
								continue;
							}
							Model mod{};
							mod.path = dir;
							mod.name = mi.model_name;
							mod.realhw = mi.real_hardware;
							switch (mi.hardware_id) {
							case HW_ES_PLUS:
								mod.type = "ESP";
								break;
							case HW_CLASSWIZ:
								mod.type = "CWX";
								break;
							case HW_CLASSWIZ_II:
								mod.type = "CWII";
								break;
							case HW_FX_5800P:
								mod.type = "Fx5800p";
								break;
							case HW_TI:
								mod.type = "TI";
								break;
							case HW_SOLARII:
								mod.type = "SolarII";
								break;
							default:
								mod.type = "Unknown";
								break;
							}
							{
								std::filesystem::path romPath = dir.path() / mi.rom_path;
								if (mi.rom_path.empty() || !std::filesystem::exists(romPath) || !std::filesystem::is_regular_file(romPath, ec))
									continue;
								std::ifstream ifs2(romPath, std::ios::in | std::ios::binary);
								if (!ifs2)
									continue;
								std::vector<byte> rom{std::istreambuf_iterator<char>{ifs2.rdbuf()}, std::istreambuf_iterator<char>{}};
								ifs2.close();
								std::vector<byte> flash{};

								if (!mi.flash_path.empty()) {
									std::filesystem::path flashPath = dir.path() / mi.flash_path;
									if (std::filesystem::exists(flashPath) && std::filesystem::is_regular_file(flashPath, ec)) {
										std::ifstream ifs3(flashPath, std::ios::in | std::ios::binary);
										if (ifs3)
											flash = {std::istreambuf_iterator<char>{ifs3.rdbuf()}, std::istreambuf_iterator<char>{}};
									}
								}
								else if (mi.hardware_id == HW_FX_5800P && rom.size() > 0x20000) {
									flash.assign(rom.begin() + 0x20000, rom.end());
									rom.resize(0x20000);
								}
								auto ri = rom_info(rom, flash, mi.real_hardware);
								if (ri.type != 0) {
									switch (ri.type) {
									case RomInfo::ES:
										mod.type = "ES";
										break;
									case RomInfo::ESP:
										mod.type = "ESP";
										break;
									case RomInfo::ESP2nd:
										mod.type = "ESP2nd";
										break;
									case RomInfo::CWX:
										mod.type = "CWX";
										break;
									case RomInfo::CWII:
										mod.type = "CWII";
										break;
									case RomInfo::Fx5800p:
										mod.type = "Fx5800p";
										break;
									default:
										mod.type = "???";
										break;
									}
								}
								if (ri.ok) {
									mod.version = ri.ver;
									std::array<char, 8> key{};
									memcpy(key.data(), mod.version.data(), 6);
									auto iter = RomNames.find(key);
									if (iter != RomNames.end())
										mod.name = iter->second;
									mod.checksum = tohex(ri.real_sum, 4);
									mod.checksum2 = tohex(ri.desired_sum, 4);
									mod.sum_good = ri.real_sum == ri.desired_sum ? "OK" : "NG";
									// Safely form version key and id
									std::array<char, 8> key2{};
									std::memset(key2.data(), 0, key2.size());
									std::memcpy(key2.data(), mod.version.data(), std::min<std::size_t>(6, mod.version.size()));
									mod.id = tohex(*(unsigned long long*)ri.cid, 8);
									if (ri.type == RomInfo::ES) {
										auto a = get_pd(mi.pd_value);
										mod.version += std::string(" (P") + a + ")";
									}
								}
								else {
									mod.show_sum = false;
								}
								printf("[StartupUI][Debug] Model Summary\n"
									   "[StartupUI][Debug] Name: %s\n"
									   "[StartupUI][Debug] Type: %s\n",
									mod.name.c_str(), mod.type.c_str());
							}
							models.push_back(mod);
						}
						catch (const std::exception& e) {
							std::cerr << "[StartupUI][Error] Failed to load model from " << dir.path().string() << ": " << e.what() << std::endl;
							continue;
						}
					}
				}
				loading = false;
			});
			thd.detach();
		}
		std::vector<std::string> recently_used{};
		char search_txt[200]{};
		const char* current_filter = "##";
		bool not_show_emu = false;
		bool loading = false;
		bool show_shortcut_popup = false;
		std::filesystem::path shortcut_model_path{};
		char shortcut_name[260]{};
		char shortcut_icon[260]{};

		inline std::string generate_random_string(size_t length) {
			return util::Random::random_string(length);
		}
		inline std::string create_unique_directory(const std::string& base_name) {
			std::string sanitized_name = sanitize_path(base_name);
			std::string dir_name = sanitized_name;
			while (std::filesystem::exists("./models/" + dir_name)) {
				dir_name = sanitized_name + "." + generate_random_string(6); // TODO: WTF is this dot for? Just add random string directly
			}
			return dir_name;
		}

		void SaveOnlineApiAddress() {
			std::ofstream settings{"online_api.cfg", std::ios::trunc};
			if (settings) settings << online_api;
		}

		void LoadOnlineModels() {
			OnlineModelClient client{online_api};
			online_models = client.ListModels(online_access_token);
			selected_online_model = online_models.empty() ? -1 : 0;
			online_status = std::to_string(online_models.size()) + " " + std::string("StartupUI.OnlineModelsLoaded"_lc);
		}

		void InvalidateOnlineLogin() {
			online_access_token.clear();
			online_authorization_pending = false;
			online_auth = {};
			online_models.clear();
			selected_online_model = -1;
			ClearOnlineToken(OnlineModelClient{online_api}.ApiBase());
			online_status = "StartupUI.OnlineLoginExpired"_lc;
		}

		void RefreshOnlineSession() {
			if (online_access_token.empty()) return;
			try {
				OnlineModelClient client{online_api};
				client.CheckStatus(online_access_token);
				LoadOnlineModels();
			}
			catch (const OnlineAuthenticationError&) {
				InvalidateOnlineLogin();
			}
			catch (const std::exception& error) {
				online_status = error.what();
			}
		}

		void StartOnlineLogin() {
			SaveOnlineApiAddress();
			OnlineModelClient client{online_api};
			ClearOnlineToken(client.ApiBase());
			online_auth = client.StartAuthorization();
			online_access_token.clear();
			online_models.clear();
			selected_online_model = -1;
			online_authorization_pending = true;
			online_status = std::string("StartupUI.OnlineBrowserOpened"_lc) + " " + online_auth.user_code;
			if (SDL_OpenURL(online_auth.verification_uri.c_str()) != 0)
				throw std::runtime_error(std::string("SDL_OpenURL failed: ") + SDL_GetError());
		}

		void RenderOnlineModels() {
			if (show_online_popup) {
				ImGui::OpenPopup("StartupUI.OnlineModels"_lc);
				show_online_popup = false;
				RefreshOnlineSession();
			}
			ImGui::SetNextWindowSize(ImVec2(680, 540), ImGuiCond_Appearing);
			if (!ImGui::BeginPopupModal("StartupUI.OnlineModels"_lc, nullptr,
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) return;

			ImGui::TextUnformatted("StartupUI.OnlineApiAddress"_lc);
			ImGui::SetNextItemWidth(-1);
			if (!online_access_token.empty() || online_authorization_pending) ImGui::BeginDisabled();
			ImGui::InputText("##OnlineApiAddress", online_api, sizeof(online_api));
			if (!online_access_token.empty() || online_authorization_pending) ImGui::EndDisabled();

			if (online_access_token.empty() && !online_authorization_pending && ImGui::Button("StartupUI.OnlineLogin"_lc)) {
				try {
					StartOnlineLogin();
				}
				catch (const std::exception& e) { online_status = e.what(); }
			}
			if (online_authorization_pending) {
				if (ImGui::Button("StartupUI.OnlineRelogin"_lc)) {
					try { StartOnlineLogin(); }
					catch (const std::exception& e) { online_status = e.what(); }
				}
				ImGui::SameLine();
			}
			if (online_authorization_pending && ImGui::Button("StartupUI.OnlineCompleteLogin"_lc)) {
				try {
					OnlineModelClient client{online_api};
					if (client.PollAuthorization(online_auth.device_code, online_access_token)) {
						online_authorization_pending = false;
						SaveOnlineToken(client.ApiBase(), online_access_token);
						online_status = OnlineTokenPersistenceAvailable()
							? std::string("StartupUI.OnlineLoginSuccess"_lc)
							: std::string("StartupUI.OnlineLoginSessionOnly"_lc);
						LoadOnlineModels();
					}
					else online_status = "StartupUI.OnlineAuthorizationPending"_lc;
				}
				catch (const std::exception& e) { online_status = e.what(); }
			}
			if (!online_access_token.empty() && ImGui::Button("Button.Refresh"_lc)) {
				try {
					OnlineModelClient client{online_api};
					client.CheckStatus(online_access_token);
					LoadOnlineModels();
				}
				catch (const OnlineAuthenticationError&) { InvalidateOnlineLogin(); }
				catch (const std::exception& e) { online_status = e.what(); }
			}

			if (!online_status.empty()) ImGui::TextWrapped("%s", online_status.c_str());
			ImGui::Separator();
			const float list_height = (std::max)(180.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.0f);
			if (ImGui::BeginChild("OnlineModelList", ImVec2(0, list_height), true)) {
				for (int index = 0; index < static_cast<int>(online_models.size()); ++index) {
					const auto& item = online_models[index];
					ImGui::PushID(index);
					const std::string label = item.name + "  [" + item.id + "]  " + item.model_type;
					if (ImGui::Selectable(label.c_str(), selected_online_model == index)) selected_online_model = index;
					ImGui::PopID();
				}
			}
			ImGui::EndChild();

			if (ImGui::Button("StartupUI.OnlineLaunch"_lc)) {
				try {
					if (selected_online_model < 0 || selected_online_model >= static_cast<int>(online_models.size()))
						throw std::runtime_error("Select an online model first.");
					const auto& item = online_models[selected_online_model];
					OnlineModelClient client{online_api};
					online_status = "StartupUI.OnlineDownloading"_lc;
					const auto archive = client.DownloadModel(online_access_token, item.id);
					selected_resources = LoadOnlineModelPackage(archive, item.id);
					selected_path = "memory-online-" + item.id;
					ImGui::CloseCurrentPopup();
				}
				catch (const OnlineAuthenticationError&) { InvalidateOnlineLogin(); }
				catch (const std::exception& e) { online_status = e.what(); }
			}
			ImGui::SameLine();
			if (ImGui::Button("Button.Negative"_lc)) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		void Render() {
			auto& io = ImGui::GetIO();

#ifdef __ANDROID__
			ThemeManager::Instance().UpdateUIScale();
			auto& tm = ThemeManager::Instance();
			float scaledWidth = tm.windowWidth;
			float scaledHeight = tm.windowHeight;
			float fontScale = tm.fontScale;
			float padding = tm.padding;
			float buttonHeight = tm.buttonHeight;

			float contentWidth = scaledWidth * 0.95f;
			float searchBarWidth = contentWidth * 0.45f;
			float filterWidth = contentWidth * 0.25f;
			float tableHeight = scaledHeight * 0.35f;
			float buttonWidth = contentWidth * 0.3f;
#else
			float scaledWidth = io.DisplaySize.x;
			float scaledHeight = io.DisplaySize.y;
			float fontScale = 1.0f;
			float padding = 8.0f;
			float buttonHeight = 40.0f;
			float contentWidth = scaledWidth;
			float searchBarWidth = 200.0f;
			float filterWidth = 80.0f;
			float tableHeight = 300.0f;
			float buttonWidth = 200.0f;
#endif
			ImGui::Begin("StartupUI.Title"_lc);
#ifdef __ANDROID__
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding, padding));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(padding * 1.5f, padding));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(padding, padding * 0.5f));
#endif

			static char password[256] = "";
			static bool show_password_input = false;
			static std::filesystem::path current_file;
			static RomPackage current_rp;
			static bool password_error = false;

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding, buttonHeight * 0.25f));
			if (ImGui::Button("StartupUI.ImportRomPackage"_lc, ImVec2(buttonWidth, 0))) {
				SystemDialogs::OpenFileDialog([&](std::filesystem::path f) {
					try {
						std::ifstream ifs{f, std::ios::binary};
						if (!ifs) {
							throw std::runtime_error("Cannot open selected file.");
						}
						RomPackage rp{};
						Binary::Read(ifs, rp);
						if (rp.IsEncrypted) {
							show_password_input = true;
							current_file = f;
							current_rp = std::move(rp);
							password_error = false;
							std::fill((volatile char*)password, (volatile char*)password + 256, 0);
						}
						else {
							std::string filename = f.stem().string();
							std::string unique_dirname = create_unique_directory(filename);
							std::filesystem::path extract_path = "./models/" + unique_dirname;
							rp.ExtractTo(extract_path);
							Reload();
						}
					}
					catch (const std::exception& e) {
						std::cerr << "Failed to import package: " << e.what() << std::endl;
						SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Import Error", "The selected file is not a valid or supported ROM package.", nullptr);
					}
				});
			}
			ImGui::SameLine();
			if (ImGui::Button("Button.Refresh"_lc, ImVec2(buttonWidth, 0))) Reload();
			ImGui::SameLine(0.0f, padding * 4.0f);
			if (ImGui::Button("StartupUI.OnlineModels"_lc, ImVec2(buttonWidth, 0))) show_online_popup = true;
			ImGui::PopStyleVar();
			RenderOnlineModels();
			if (show_password_input) {
				ImGui::OpenPopup("StartupUI.EnterPassword"_lc);
			}

#ifdef __ANDROID__
			ImGui::SetNextWindowSize(ImVec2(contentWidth * 0.8f, 0));
#endif

			if (ImGui::BeginPopupModal("StartupUI.EnterPassword"_lc, NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::TextUnformatted("StartupUI.PasswordPopupHint"_lc);

#ifdef __ANDROID__
				float inputWidth = ImGui::GetContentRegionAvail().x - padding * 2;
				ImGui::PushItemWidth(inputWidth);
#endif

				ImGui::InputText("##password", password, IM_ARRAYSIZE(password), ImGuiInputTextFlags_Password);

#ifdef __ANDROID__
				ImGui::PopItemWidth();
#endif

				if (password_error) {
					ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Password incorrect. Please try again.");
				}

#ifdef __ANDROID__
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding, buttonHeight * 0.25f));
#endif
				if (ImGui::Button("Button.Positive"_lc)) {
					try {
						if (password[0] == '\0') {
							password_error = true;
						}
						else {
							current_rp.Decrypt(password);
							std::string filename = current_file.stem().string();
							std::string unique_dirname = create_unique_directory(filename);
							std::filesystem::path extract_path = "./models/" + unique_dirname;
							current_rp.ExtractTo(extract_path);
							Reload();
							show_password_input = false;
							password_error = false;
							ImGui::CloseCurrentPopup();
						}
					}
					catch (const std::exception& e) {
						SDL_Log("Decryption failed: %s", e.what());
						password_error = true;
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("Button.Negative"_lc)) {
					show_password_input = false;
					password_error = false;
					ImGui::CloseCurrentPopup();
				}

#ifdef __ANDROID__
				ImGui::PopStyleVar();
#endif

				ImGui::EndPopup();
			}

			if (loading) {
#ifdef __ANDROID__
				ImGui::PopStyleVar(3);
#endif
				ImGui::End();
				return;
			}

			ImGui::TextUnformatted("StartupUI.ChooseModelHint"_lc);
			ImGui::Separator();
			ImGui::TextUnformatted("StartupUI.RecentlyUsed"_lc);

			if (ImGui::BeginTable("Recently", 4, pretty_table | ImGuiTableFlags_ScrollY, ImVec2(0, tableHeight))) {
				RenderHeaders();
				auto i = 114;
				auto ru = recently_used;
				for (auto& s : ru) {
					auto iter = std::find_if(models.begin(), models.end(), [&](const Model& x) {
						return x.path == s;
					});
					if (iter != models.end()) {
						auto& model = *iter;
						RenderModel(model, i);
					}
				}
				ImGui::EndTable();
			}

			if (ImGui::CollapsingHeader("StartupUI.AllModel"_lc)) {
				ImGui::SetNextItemWidth(searchBarWidth);
				ImGui::InputText("StartupUI.SearchBoxHeader"_lc, search_txt, 200);
				ImGui::SameLine();

				const char* items[] = {"##", "ES", "ESP", "ESP2nd", "CWX", "CWII", "Fx5800p", "TI", "SolarII"};
				ImGui::SetNextItemWidth(filterWidth);
				if (ImGui::BeginCombo("##cb", current_filter)) {
					for (int n = 0; n < IM_ARRAYSIZE(items); n++) {
						bool is_selected = (current_filter == items[n]);
						if (ImGui::Selectable(items[n], is_selected))
							current_filter = items[n];
						if (is_selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				ImGui::SameLine();
				ImGui::Checkbox("StartupUI.DontShowEmuRom"_lc, &not_show_emu);

				if (ImGui::BeginTable("All", 4, pretty_table | ImGuiTableFlags_ScrollY, ImVec2(0, tableHeight))) {
					RenderHeaders();
					auto i = 114;
					for (auto& model : models) {
						bool matches_filter = (strcmp(current_filter, "##") == 0) || (current_filter == model.type);
						bool matches_search = (stristr(model.name.c_str(), search_txt) != nullptr || stristr(model.version.c_str(), search_txt) != nullptr);
						if (matches_filter && matches_search && (not_show_emu ? model.realhw : 1)) {
							RenderModel(model, i);
						}
					}
					ImGui::EndTable();
				}
			}

			// Shortcut creation popup
			if (show_shortcut_popup) {
				ImGui::OpenPopup("StartupUI.CreateShortcutTitle"_lc);
				show_shortcut_popup = false;
			}

			ImVec2 center = ImGui::GetMainViewport()->GetCenter();
			ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (ImGui::BeginPopupModal("StartupUI.CreateShortcutTitle"_lc, NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::TextUnformatted("StartupUI.ShortcutNameHint"_lc);
				ImGui::SetNextItemWidth(300);
				ImGui::InputText("##shortcut_name", shortcut_name, sizeof(shortcut_name));

				ImGui::TextUnformatted("StartupUI.ShortcutIconHint"_lc);
				ImGui::SetNextItemWidth(300);
				ImGui::InputText("##shortcut_icon", shortcut_icon, sizeof(shortcut_icon));
				ImGui::SameLine();
				if (ImGui::Button("...##icon_browse")) {
					SystemDialogs::OpenFileDialog([&](std::filesystem::path f) {
						strncpy(shortcut_icon, f.string().c_str(), sizeof(shortcut_icon) - 1);
						shortcut_icon[sizeof(shortcut_icon) - 1] = '\0';
					});
				}
				ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", "StartupUI.ShortcutIconOptional"_lc); // Make Clang happy.

				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				if (ImGui::Button("Button.Positive"_lc, ImVec2(120, 0))) {
					std::string name_str = shortcut_name;
					std::string icon_str = shortcut_icon;
					if (!name_str.empty()) {
						try {
							bool ok = CreateDesktopShortcut(shortcut_model_path, name_str, icon_str);
							if (ok) {
								SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,
									"StartupUI.CreateShortcutTitle"_lc,
									"StartupUI.ShortcutCreated"_lc, nullptr);
							}
							else {
								SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
									"StartupUI.CreateShortcutTitle"_lc,
									"StartupUI.ShortcutFailed"_lc, nullptr);
							}
						}
						catch (const std::exception& e) {
							std::cerr << "[Shortcut] Error: " << e.what() << std::endl;
							SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
								"StartupUI.CreateShortcutTitle"_lc,
								"StartupUI.ShortcutFailed"_lc, nullptr);
						}
						ImGui::CloseCurrentPopup();
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("Button.Negative"_lc, ImVec2(120, 0))) {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

#ifdef __ANDROID__
			ImGui::PopStyleVar(3);
#endif
			ImGui::End();
		}
		void RenderHeaders() {
			ImGui::TableSetupColumn("StartupUI.RomName"_lc, ImGuiTableColumnFlags_WidthStretch, 200);
			ImGui::TableSetupColumn("StartupUI.RomVer"_lc, ImGuiTableColumnFlags_WidthFixed, 120);
			ImGui::TableSetupColumn("StartupUI.RomSum"_lc, ImGuiTableColumnFlags_WidthFixed, 130);
			ImGui::TableSetupColumn("StartupUI.RomType"_lc, ImGuiTableColumnFlags_WidthFixed, 70);
			// ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80);
			ImGui::TableHeadersRow();
		}

		void RenderModel(const Model& model, int& i) {
			static char password[60]{};
			static bool pwd_op = true;
			ImGui::TableNextRow();
			ImGui::PushID(i++);
			ImGui::TableNextColumn();
			if (ImGui::Selectable(model.name.c_str())) {
				ImGui::OpenPopup("ContextMenu");
				pwd_op = false;
			}
			if (ImGui::BeginPopup("ContextMenu")) {
				if (pwd_op) {
					ImGui::InputText("##input_pwd", password, 60);

#ifdef __ANDROID__
					if (ImGui::MenuItem("StartupUI.Export"_lc)) {
						RomPackage rp{};
						try {
							rp.Load(model.path);
							if (*password != 0) {
								rp.Encrypt(password);
							}
							else {
								rp.Encrypt("0x0d000721");
								std::cout << "Using default password 0x0d000721\n";
							}
							memset(password, 0, 60);

							SystemDialogs::SaveFileDialog(model.name + ".package",
								[rp](std::filesystem::path fl) {
									JNIEnv* env = nullptr;
									if (!GetJNIEnv(&env)) {
										std::cerr << "Failed to get JNI environment" << std::endl;
										return;
									}

									try {
										std::vector<unsigned char> buffer;
										{
											std::stringstream ss;
											Binary::Write(ss, rp);
											std::string str = ss.str();
											buffer.assign(str.begin(), str.end());
										}

										jobject activity = (jobject)SDL_AndroidGetActivity();
										if (!activity) {
											throw std::runtime_error("Failed to get Android activity");
										}

										jclass gameClass = env->FindClass("com/tele/u8emulator/Game");
										if (!gameClass) {
											env->DeleteLocalRef(activity);
											throw std::runtime_error("Failed to find Game class");
										}

										jmethodID exportMethod = env->GetStaticMethodID(gameClass, "exportData", "([BLandroid/net/Uri;)V");
										if (!exportMethod) {
											env->DeleteLocalRef(gameClass);
											env->DeleteLocalRef(activity);
											throw std::runtime_error("Failed to find exportData method");
										}

										jbyteArray jdata = env->NewByteArray(buffer.size());
										env->SetByteArrayRegion(jdata, 0, buffer.size(), (jbyte*)buffer.data());

										jclass uriClass = env->FindClass("android/net/Uri");
										if (!uriClass) {
											env->DeleteLocalRef(jdata);
											env->DeleteLocalRef(gameClass);
											env->DeleteLocalRef(activity);
											throw std::runtime_error("Failed to find Uri class");
										}

										jmethodID parseMethod = env->GetStaticMethodID(uriClass, "parse",
											"(Ljava/lang/String;)Landroid/net/Uri;");
										if (!parseMethod) {
											env->DeleteLocalRef(jdata);
											env->DeleteLocalRef(uriClass);
											env->DeleteLocalRef(gameClass);
											env->DeleteLocalRef(activity);
											throw std::runtime_error("Failed to find parse method");
										}

										jstring jpath = env->NewStringUTF(fl.string().c_str());
										jobject uri = env->CallStaticObjectMethod(uriClass, parseMethod, jpath);

										// Export the data
										env->CallStaticVoidMethod(gameClass, exportMethod, jdata, uri);

										// Cleanup
										env->DeleteLocalRef(jdata);
										env->DeleteLocalRef(jpath);
										env->DeleteLocalRef(uri);
										env->DeleteLocalRef(uriClass);
										env->DeleteLocalRef(gameClass);
										env->DeleteLocalRef(activity);

										// Close popup after successful export
										ImGui::CloseCurrentPopup();
									}
									catch (const std::exception& e) {
										std::cerr << "Export failed: " << e.what() << std::endl;
										SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Export Error", e.what(), nullptr);
									}
								});
						}
						catch (const std::exception& e) {
							std::cerr << "Export preparation failed: " << e.what() << std::endl;
							SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Export Error", e.what(), nullptr);
						}
					}
#else
					if (ImGui::MenuItem("StartupUI.Export"_lc)) {
						RomPackage rp{};
						try {
							rp.Load(model.path);
							if (*password != 0) {
								rp.Encrypt(password);
							}
							else {
								rp.Encrypt("0x0d000721");
								std::cout << "Using default password 0x0d000721\n";
							}
							memset(password, 0, 60);

							SystemDialogs::SaveFileDialog(model.name + ".package",
								[rp](std::filesystem::path fl) {
									try {
										std::ofstream ofs(fl, std::ios::binary);
										if (!ofs) {
											throw std::runtime_error("Cannot create output file");
										}
										Binary::Write(ofs, rp);
										ofs.close();
									}
									catch (const std::exception& e) {
										std::cerr << "Export failed: " << e.what() << std::endl;
										SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Export Error", e.what(), nullptr);
									}
								});
						}
						catch (const std::exception& e) {
							std::cerr << "Export preparation failed: " << e.what() << std::endl;
							SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Export Error", e.what(), nullptr);
						}
					}
#endif
				}
				else {
					if (ImGui::MenuItem("StartupUI.Launch"_lc)) {
						selected_path = model.path;
						auto iter = std::find_if(recently_used.begin(), recently_used.end(),
							[&](auto& x) {
								return x == model.path.string();
							});
						if (iter != recently_used.end())
							recently_used.erase(iter);
						recently_used.insert(recently_used.begin(), model.path.string());
						if (recently_used.size() > 5) {
							recently_used.resize(5);
						}
					}
#ifdef _WIN32
					if (ImGui::MenuItem("StartupUI.Reveal"_lc)) {
						std::wstring wpath = std::filesystem::path(model.path).wstring();
						ShellExecuteW(NULL, L"open", L"explorer.exe", wpath.c_str(), NULL, SW_SHOWNORMAL);
					}
#endif
					if (ImGui::MenuItem("StartupUI.Edit"_lc)) {
						try {
							windows2->push_back(new ModelEditor(model.path));
						}
						catch (const std::exception& e) {
							SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "Failed to open model editor.", nullptr);
						}
					}
					if (ImGui::MenuItem("StartupUI.CreateShortcut"_lc)) {
						strncpy(shortcut_name, model.name.c_str(), sizeof(shortcut_name) - 1);
						shortcut_name[sizeof(shortcut_name) - 1] = '\0';
						shortcut_icon[0] = '\0';
						shortcut_model_path = model.path;
						show_shortcut_popup = true;
					}
					if (ImGui::MenuItem("StartupUI.Export"_lc)) {
						ImGui::EndPopup();
						ImGui::OpenPopup("ContextMenu");
						pwd_op = true;
						goto ed;
					}
				}
				ImGui::EndPopup();
			}
		ed:
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(model.version.c_str());
			ImGui::TableNextColumn();
			if (model.realhw) {
				if (model.show_sum) {
					ImGui::Text("%s (%s) %s", model.checksum.c_str(), model.checksum2.c_str(), model.sum_good.c_str());
				}
				else {
					ImGui::TextUnformatted("Table.NotAvailable"_lc);
				}
			}
			else {
				ImGui::TextUnformatted("StartupUI.EmulatorRom"_lc);
			}
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(model.type.c_str());
			ImGui::SameLine();
			ImGui::Dummy({0, ImGui::GetTextLineHeightWithSpacing()});
			ImGui::PopID();
		}
	};
} // namespace casioemu
class LicenseWindow : public UIWindow {
public:
	LicenseWindow() : UIWindow("License") {
	}
	void RenderCore() override {
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(licenses_str);
		ImGui::PopTextWrapPos();
	}
	void Render() override {
		if (!open)
			return;
		auto& io = ImGui::GetIO();
		ImGui::SetNextWindowSize({io.DisplaySize.x, io.DisplaySize.y}, ImGuiCond_Appearing);
		ImGui::SetNextWindowPos({});
		if (ImGui::Begin(name, &open, flags)) {
			RenderCore();
		}
		ImGui::End();
	}
};
class CopyrightWatermark : public UIWindow {
	std::vector<UIWindow*>* windows3;

public:
	CopyrightWatermark(std::vector<UIWindow*>* windows3) : UIWindow("Copyright Warning"), windows3(windows3) {
	}
	void RenderCore() override {
		ImGui::TextWrapped(startup_copyright_warn);
		if (ImGui::Button("CopyrightWatermark.Dismiss"_lc))
			open = false;
		if (ImGui::Button("CopyrightWatermark.Licenses"_lc)) {
			windows3->push_back(new LicenseWindow());
		}
		if (ImGui::Button("CopyrightWatermark.VisitOfficialRepo"_lc)) {
			SDL_OpenURL("https://github.com/telecomadm1145/CasioEmuMsvc");
		}
	}
	void Render() override {
		if (!open)
			return;
		auto& io = ImGui::GetIO();
		ImGui::SetNextWindowSize({io.DisplaySize.x, io.DisplaySize.y}, ImGuiCond_Appearing);
		ImGui::SetNextWindowPos({});
		if (ImGui::Begin(name, &open, flags)) {
			RenderCore();
		}
		ImGui::End();
	}
};

void HandleStartupEvent(const SDL_Event& event) {
	ImGui_ImplSDL2_ProcessEvent(&event);
}

StartupSelection sui_loop() {
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
		SDL_Log("SDL_Init failed in StartupUI: %s", SDL_GetError());
		return {};
	}
	if (IMG_Init(IMG_INIT_PNG) != IMG_INIT_PNG) {
		SDL_Log("IMG_Init failed in StartupUI: %s", IMG_GetError());
		SDL_Quit();
		return {};
	}
	SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
	windows2 = new std::vector<UIWindow*>();
	casioemu::StartupUi ui;
	{
		std::ifstream ifs1{"recent.bin", std::ifstream::binary};
		if (ifs1) {
			try {
				Binary::Read(ifs1, ui.recently_used);
			}
			catch (const std::exception& e) {
				std::cerr << "[StartupUI][Warn] Failed to read \"recent.bin\": " << e.what() << ". Starting with no recent files.\n";
				ui.recently_used.clear();
			}
		}
	}
	window2 = SDL_CreateWindow(
		"CasioEmuMsvc",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		1200, 800,
		SDL_WINDOW_SHOWN | (SDL_WINDOW_RESIZABLE));
	casioemu::SetPreferredRendererDriverHint();
	renderer2 = SDL_CreateRenderer(window2, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
#ifdef _WIN32
	EnableDarkTitleBar(GetSDLWindowHandle(window2));
#endif
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
	if (renderer2 == nullptr) {
		SDL_Log("Error creating SDL_Renderer!");
		if (window2)
			SDL_DestroyWindow(window2);
		IMG_Quit();
		SDL_Quit();
		return {};
	}
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	// RebuildFont();
	io.WantCaptureKeyboard = true;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	ThemeManager::Instance().ApplyDefaultTheme();
	ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 1.0f;
#ifdef __ANDROID__
	windows2->push_back(new CopyrightWatermark(windows2));
#endif
	ImGui_ImplSDL2_InitForSDLRenderer(window2, renderer2);
	ImGui_ImplSDLRenderer2_Init(renderer2);
	ThemeManager::Instance().RequestFontRebuild();
	ThemeManager::Instance().ProcessFontRebuild();
	auto frame_event = SDL_RegisterEvents(1);
	std::atomic<bool> exited = {false};
	std::thread t3([&]() {
		SDL_Event se{};
		se.type = frame_event;
		if (window2)
			se.user.windowID = SDL_GetWindowID(window2);
		while (!exited) {
			if (window2)
				SDL_PushEvent(&se);
			SDL_Delay(24); // ~40fps
		}
	});
	bool done = false;
	bool once = true;
	std::vector<std::string> languages;
	int selected_language_index = 0;
	bool needs_render = true;
	while (!done) {
		SDL_Event event;
		if (SDL_WaitEvent(&event)) {
			ImGui_ImplSDL2_ProcessEvent(&event);
			if (event.type == SDL_QUIT) {
				done = true;
			}
			if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window2)) {
				done = true;
			}
			if (event.type == frame_event) {
				needs_render = true;
			}
			while (SDL_PollEvent(&event)) {
				ImGui_ImplSDL2_ProcessEvent(&event);
				if (event.type == SDL_QUIT) {
					done = true;
				}
				if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window2)) {
					done = true;
				}
				if (event.type == frame_event) {
					needs_render = true;
				}
			}
		}
		if (needs_render) {
			ImGui_ImplSDLRenderer2_NewFrame();
			ImGui_ImplSDL2_NewFrame();
			ImGui::NewFrame();
			ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
			ImGui::SetNextWindowDockID(ImGui::GetCurrentContext()->DockContext.Nodes.Data[0].key, ImGuiCond_FirstUseEver); // TODO: ????????
			ui.Render();
			// static std::once_flag size_once;
			// std::call_once(size_once, []() {
			//	ImGui::DockBuilderSetNodeSize(ImGui::GetWindowDockID(), ImGui::GetMainViewport()->Size);
			// });
			for (auto& wind : *windows2) {
				wind->Render();
			}
			if (once && !std::filesystem::exists("locale.txt")) {
				if (languages.empty()) {
					const std::string locales_dir = "./locales/";
					if (std::filesystem::exists(locales_dir)) {
						for (const auto& entry : std::filesystem::directory_iterator(locales_dir)) {
							if (entry.is_regular_file()) {
								languages.push_back(entry.path().stem().string());
							}
						}
					}
					if (languages.empty()) {
						languages.push_back("en_US");
					}
				}
				ImGui::OpenPopup("LanguageChooser");
				once = false;
			}
			ImVec2 center = ImGui::GetMainViewport()->GetCenter();
			ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (ImGui::BeginPopupModal("LanguageChooser", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::Text("Please choose your language to continue");
				ImGui::Separator();
				ImGui::Spacing();
				if (!languages.empty()) {
					const char* preview_value = languages[selected_language_index].c_str();
					if (ImGui::BeginCombo("Language", preview_value)) {
						for (int n = 0; n < languages.size(); n++) {
							const bool is_selected = (selected_language_index == n);
							if (ImGui::Selectable(languages[n].c_str(), is_selected)) {
								selected_language_index = n;
							}
							if (is_selected) {
								ImGui::SetItemDefaultFocus();
							}
						}
						ImGui::EndCombo();
					}
				}
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
				if (ImGui::Button("Ok", ImVec2(120, 0))) {
					if (!languages.empty() && selected_language_index < languages.size()) {
						const std::string& selected_lang = languages[selected_language_index];
						g_local.ChangeLanguage(selected_lang);
						if ("Localization.EnableCJK"_l == "1" || "Localization.EnableCJK"_l == "true")
							ThemeManager::Instance().RequestFontRebuild();
						std::ofstream outfile("locale.txt");
						outfile << selected_lang;
						outfile.close();
						ImGui::CloseCurrentPopup();
						DiscordRPC::UpdatePresence("");
					}
				}
				ImGui::EndPopup();
			}
			ImGui::EndFrame();
			ImGui::Render();
			SDL_RenderSetScale(renderer2, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
			SDL_SetRenderDrawColor(renderer2, 0, 0, 0, 255);
			SDL_RenderClear(renderer2);
			ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
			SDL_RenderPresent(renderer2);

			needs_render = false;
		}
		DiscordRPC::Update();
		ThemeManager::Instance().ProcessFontRebuild();
		if (!ui.selected_path.empty()) {
			done = true;
		}
	}
	exited = true;
	if (t3.joinable()) {
		t3.join();
	}
	ImGui_ImplSDLRenderer2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
	for (auto& wind : *windows2) {
		delete wind;
	}
	delete windows2;
	SDL_DestroyRenderer(renderer2);
	SDL_DestroyWindow(window2);
	std::ofstream ofs{"recent.bin", std::ofstream::binary};
	if (ofs)
		Binary::Write(ofs, ui.recently_used);
	else {
		std::cout << "[Warn] Cannot write to recent.bin.\n";
	}
	IMG_Quit();
	SDL_Quit();
	return {ui.selected_path.string(), std::move(ui.selected_resources)};
}
