#include "StartupUi.h"
#include "3rd_licenses.h"
#include "Binary.h"
#include "Config.hpp"
#include "Gui/imgui/imgui.h"
#include "Gui/imgui/imgui_impl_sdl2.h"
#include "Gui/imgui/imgui_impl_sdlrenderer2.h"
#include "Localization.h"
#include <SDL.h>
#include "ModelInfo.h"
#include "RomPackage.h"
#include "Romu.h"
#include "SysDialog.h"
#include "Ui.hpp"
#include <Gui.h>
#include <SDL.h>
#include <SDL_image.h>
#include <array>
#include <filesystem>
#include <imgui.h>
#include <iostream>
#include "sdl_win32_extra.h"
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>

#endif
#include "Ext/Random.hpp"
#include "DiscordRPC.h"

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
			std::filesystem::path configPath = path / "config.bin";
			std::error_code ec;
			if (!std::filesystem::exists(configPath) || !std::filesystem::is_regular_file(configPath, ec)) {
				throw std::runtime_error("Cannot open config.bin (not found or is a directory).");
			}
			std::ifstream ifs(configPath, std::ios::binary);
			if (!ifs)
				throw std::runtime_error("Cannot open config.bin.");
			Binary::Read(ifs, mi);
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
		if (sdl_t)
			SDL_free(sdl_t);
		if (mi.sprites.find("rsd_interface") != mi.sprites.end()) {
			SDL_Surface* surface = IMG_Load((pth / mi.interface_path).string().c_str());
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
		auto scaleFactor = (400.f / imgSp.src.w);
		if (sdl_t != 0) {
			ImGui::SetCursorPosX(0);
			RenderSprite2(imgSp, (ImTextureID)sdl_t, imgSz, {400, 400.0f * imgSp.dest.h / imgSp.dest.w});
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
		for (auto& btn : mi.buttons) {
			ImGui::SetCursorPos({scaleFactor * btn.rect.x, scaleFactor * btn.rect.y + y});
			ImGui::PushID(btn.kiko + 20);
			if (ImGui::Button(btn.keyname.c_str(), {scaleFactor * btn.rect.w, scaleFactor * btn.rect.h})) {
				btninfo = &btn;
				strncpy(buffer, btn.keyname.c_str(), sizeof(buffer) - 1);
				buffer[sizeof(buffer) - 1] = '\0';
				SDL_itoa(btn.kiko, buffer2, 16);
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
					if (btninfo) {
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
				if (ImGui::BeginTabItem("Sprites")) {
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
				ImGui::EndTabBar();
			}

			ImGui::Separator();
			if (ImGui::Button("Button.Save"_lc)) {
				std::ofstream ifs(pth / "config.bin", std::ios::binary);
				if (!ifs)
					PANIC("Cannot open.");
				Binary::Write(ifs, mi);
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
		StartupUi() {
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
					if (dir.is_directory()) {
						try {
							printf("[StartupUI][Info] Checking %s\n", dir.path().string().c_str());
							auto config = dir.path() / "config.bin";
							std::error_code ec;
							if (!std::filesystem::exists(config) || !std::filesystem::is_regular_file(config, ec)) {
								printf("[StartupUI][Info] Unable to open %s\n", config.string().c_str());
								continue;
							}

							std::ifstream ifs(config, std::ios::in | std::ios::binary);
							if (!ifs) {
								printf("[StartupUI][Info] Unable to open %s\n", config.string().c_str());
								continue;
							}
							ModelInfo mi{};
							Binary::Read(ifs, mi);
							ifs.close();
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
			ImGui::PopStyleVar();
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

			ImGui::SameLine();
			if (ImGui::Button("Button.Refresh"_lc)) {
				Reload();
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

std::string sui_loop() {
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
		SDL_Log("SDL_Init failed in StartupUI: %s", SDL_GetError());
		return "";
	}
	if (IMG_Init(IMG_INIT_PNG) != IMG_INIT_PNG) {
		SDL_Log("IMG_Init failed in StartupUI: %s", IMG_GetError());
		SDL_Quit();
		return "";
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
		return "";
	}
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	RebuildFont();
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
	return ui.selected_path.string();
}