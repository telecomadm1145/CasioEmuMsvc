#include "StartupUi.h"
#include "Binary.h"
#include "Config.hpp"
#include "Gui/imgui/imgui.h"
#include "Gui/imgui/imgui_impl_sdl2.h"
#include "Gui/imgui/imgui_impl_sdlrenderer2.h"
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
	static constexpr const char* items[9] = {"##1", "##2", "##3", "ES(P)", "CWX", "CWII", "Fx5800p", "TI", "SolarII"};
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

public:
	ModelEditor(std::filesystem::path path) : UIWindow("Model Editor##114514"), pth(path) {

		std::ifstream ifs(path / "config.bin", std::ios::binary);
		if (!ifs)
			PANIC("Cannot open.");
		Binary::Read(ifs, mi);
		v = mi.csr_mask;
		k = mi.pd_value;
		strcpy(path1, mi.interface_path.c_str());
		strcpy(path2, mi.rom_path.c_str());
		strcpy(path3, mi.flash_path.c_str());
		strcpy(name, mi.model_name.c_str());
		color[0] = mi.ink_color.r / 255.0f;
		color[1] = mi.ink_color.g / 255.0f;
		color[2] = mi.ink_color.b / 255.0f;
		LoadInterface();
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
		auto y = ImGui::GetCursorPosY();
		auto scaleFactor = (400.f / imgSp.src.w);
		if (sdl_t != 0) {
			ImGui::SetCursorPosX(0);
			RenderSprite2(imgSp, (ImTextureID)sdl_t, imgSz, {400, 400.0f * imgSp.dest.h / imgSp.dest.w});
			for (auto sp : mi.sprites) {
				if (sp.first != "rsd_pixel" && sp.first != "rsd_interface") {
					ImGui::SetCursorPos({(float)sp.second.dest.x * scaleFactor, (float)sp.second.dest.y * scaleFactor + y});
					RenderSprite(sp.second, (ImTextureID)sdl_t, imgSz, {scaleFactor * (float)sp.second.dest.w, scaleFactor * (float)sp.second.dest.h});
				}
			}
			auto sp2 = mi.sprites["rsd_pixel"];
			if (mi.hardware_id == casioemu::HW_ES_PLUS || mi.hardware_id == casioemu::HW_FX_5800P) {
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
				strcpy(buffer, btn.keyname.c_str());
				SDL_itoa(btn.kiko, buffer2, 16);
			}
			ImGui::PopID();
		}
		ImGui::SetCursorPos({400, y});
		if (ImGui::BeginChild("Model Info")) {
			ImGui::Text("Name");
			if (ImGui::InputText("##name", name, 260)) {
				mi.model_name = name;
			}
			ImGui::Text("Interface path");
			if (ImGui::InputText("##path1", path1, 260)) {
				mi.interface_path = path1;
			}
			ImGui::Text("ROM path");
			if (ImGui::InputText("##path2", path2, 260)) {
				mi.rom_path = path2;
			}
			ImGui::Text("Flash path");
			if (ImGui::InputText("##path3", path3, 260)) {
				mi.flash_path = path3;
			}
			ImGui::Text("Csr Mask");
			if (ImGui::SliderInt("##a", &v, 0, 15, "0x%X")) {
				mi.csr_mask = v;
			}
			ImGui::Text("Pd value(ES)");
			if (ImGui::SliderInt("##q", &k, 0, 15, "0x%X")) {
				mi.pd_value = k;
			}
			ImGui::Text("Hardware Id");
			ImGui::SetNextItemWidth(80);
			if (ImGui::BeginCombo("##cb", items[mi.hardware_id])) {
				for (int n = 0; n < IM_ARRAYSIZE(items); n++) {
					bool is_selected = (mi.hardware_id == n); // You can store your selection however you want, outside or inside your objects
					if (ImGui::Selectable(items[n], is_selected)) {
						mi.hardware_id = n;
					}
					if (is_selected)
						ImGui::SetItemDefaultFocus(); // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
				}
				ImGui::EndCombo();
			}
			ImGui::Checkbox("Sample ROM", &mi.is_sample_rom);
			ImGui::Checkbox("New render method", &mi.enable_new_screen);
			ImGui::Checkbox("Real ROM", &mi.real_hardware);
			ImGui::Checkbox("Legacy KO(ES Rom)", &mi.legacy_ko);
			if (ImGui::ColorEdit3("Ink Color", color)) {
				mi.ink_color.r = color[0] * 255;
				mi.ink_color.g = color[1] * 255;
				mi.ink_color.b = color[2] * 255;
			}
			if (ImGui::Button("Save")) {
				std::ofstream ifs(pth / "config.bin", std::ios::binary);
				if (!ifs)
					PANIC("Cannot open.");
				Binary::Write(ifs, mi);
				this->open = false;
			}
			ImGui::Separator();
			if (btninfo) {
				if (ImGui::InputText("Keyname", buffer, 260)) {
					btninfo->keyname = buffer;
				}
				if (ImGui::InputText("KiKo", buffer2, 12)) {
					btninfo->kiko = SDL_strtol(buffer2, 0, 16);
				}
				ImGui::InputInt("X", &btninfo->rect.x);
				ImGui::InputInt("Y", &btninfo->rect.y);
				ImGui::InputInt("W", &btninfo->rect.w);
				ImGui::InputInt("H", &btninfo->rect.h);
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
			if (ifs2)
				Binary::Read(ifs2, RomNames);
			else {
				printf("[StartupUI][Warn] \"rom.db\" not found. Names may be inaccurate!\n");
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
						printf("[StartupUI][Info] Checking %s\n", dir.path().string().c_str());
						auto config = dir.path() / "config.bin";
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
							std::ifstream ifs2(dir.path() / mi.rom_path, std::ios::in | std::ios::binary);
							if (!ifs2)
								continue;
							std::vector<byte> rom{std::istreambuf_iterator<char>{ifs2.rdbuf()}, std::istreambuf_iterator<char>{}};
							ifs2.close();
							std::vector<byte> flash{};
							std::ifstream ifs3(dir.path() / mi.flash_path, std::ios::in | std::ios::binary);
							if (ifs3)
								flash = {std::istreambuf_iterator<char>{ifs3.rdbuf()}, std::istreambuf_iterator<char>{}};
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
        void Render() {
            auto& io = ImGui::GetIO();
            ImGui::SetNextWindowSize({io.DisplaySize.x, io.DisplaySize.y});
            ImGui::SetNextWindowPos({});
            ImGui::Begin(
#if LANGUAGE == 2
                    "启动"
#else
                    "Startup"
#endif
                    ,
                    0, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking);
            static char password[256] = "";
            static bool show_password_input = false;
            static std::filesystem::path current_file;
            static RomPackage current_rp;
            static bool password_error = false;

            if (ImGui::Button("Import...")) {
                auto f = SystemDialogs::OpenFileDialog();
                std::ifstream ifs{f, std::ios::binary};
                if (ifs) {
                    RomPackage rp{};
                    Binary::Read(ifs, rp);
                    if (rp.IsEncrypted) {
                        show_password_input = true;
                        current_file = f;
                        current_rp = std::move(rp);
                        password_error = false;  // 重置错误状态
                        memset(password, 0, sizeof(password));  // 清空密码输入框
                    } else {
                        rp.ExtractTo("./models");
                    }
                }
            }

            if (show_password_input) {
                ImGui::OpenPopup("Enter Password");
            }

            if (ImGui::BeginPopupModal("Enter Password", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Please enter the password for the encrypted ROM package:");
                ImGui::InputText("##password", password, IM_ARRAYSIZE(password), ImGuiInputTextFlags_Password);

                if (password_error) {
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Password incorrect. Please try again.");
                }

                if (ImGui::Button("OK")) {
                    try {
                        current_rp.Decrypt(password);
                        current_rp.ExtractTo("./models");
                        show_password_input = false;
                        password_error = false;
                        ImGui::CloseCurrentPopup();
                    }
                    catch (...) {
                        password_error = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    show_password_input = false;
                    password_error = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Refresh")) {
                Reload();
            }
            if (loading)
                return;
            ImGui::Text(
#if LANGUAGE == 2
                    "选择你的英雄:"
#else
                    "Choose a model:"
#endif
            );
            ImGui::Separator();
            ImGui::Text(
#if LANGUAGE == 2
                    "最近使用"
#else
                    "Recently used"
#endif
            );
            if (ImGui::BeginTable("Recently", 4, pretty_table)) {
                RenderHeaders();
                auto i = 114;
                for (auto& s : recently_used) {
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
            if (ImGui::CollapsingHeader(
#if LANGUAGE == 2
                    "全部"
#else
                    "All"
#endif
            )) {
                ImGui::SetNextItemWidth(200);
                ImGui::InputText(
#if LANGUAGE == 2
                        "搜索"
#else
                        "##search"
#endif
                        ,
                        search_txt, 200);
                ImGui::SameLine();
                const char* items[] = {"##", "ES", "ESP", "ESP2nd", "CWX", "CWII", "Fx5800p", "TI", "SolarII"};
                ImGui::SetNextItemWidth(80);
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
                ImGui::Checkbox(
#if LANGUAGE == 2
                        "不要显示模拟器 Rom"
#else
                        "Don't show emulator roms"
#endif
                        ,
                        &not_show_emu);
                if (ImGui::BeginTable("All", 4, pretty_table)) {
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
            ImGui::End();
        }
		void RenderHeaders() {
			ImGui::TableSetupColumn(
#if LANGUAGE == 2
				"名称"
#else
				"Name"
#endif
				,
				ImGuiTableColumnFlags_WidthStretch, 200);
			ImGui::TableSetupColumn(
#if LANGUAGE == 2
				"版本"
#else
				"Version"
#endif
				,
				ImGuiTableColumnFlags_WidthFixed, 120);
			ImGui::TableSetupColumn(
#if LANGUAGE == 2
				"校验和"
#else
				"Sum"
#endif
				,
				ImGuiTableColumnFlags_WidthFixed, 130);
			ImGui::TableSetupColumn(
#if LANGUAGE == 2
				"Rom 类型"
#else
				"Type"
#endif
				,
				ImGuiTableColumnFlags_WidthFixed, 70);
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
					if (ImGui::MenuItem("Encrypt and export")) {
						auto fl = SystemDialogs::SaveFileDialog(model.name + ".package");
						std::ofstream ofs{fl, std::ios::binary | std::ios::out};
						if (ofs) {
							RomPackage rp{};
							rp.Load(model.path);
							if (*password != 0) {
								rp.Encrypt(password);
							}
							else {
								rp.Encrypt("0x0d000721");
							}
							memset(password, 0, 60);
							Binary::Write(ofs, rp);
						}
					}
				}
				else {
					if (ImGui::MenuItem("Launch")) {
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
					if (ImGui::MenuItem("Reveal in Explorer")) {
						char buffer[480];
						sprintf(buffer, "explorer.exe \"%s\"", model.path.string().c_str());
						system(buffer); // right this will only work for windows lol
					}
#endif
					if (ImGui::MenuItem("Edit")) {
						windows2->push_back(new ModelEditor(model.path));
					}
					if (ImGui::MenuItem("Export...")) {
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
					ImGui::TextUnformatted(
#if LANGUAGE == 2
						"不适用"
#else
						"N/A"
#endif
					);
				}
			}
			else {
				ImGui::TextUnformatted(
#if LANGUAGE == 2
					"模拟器"
#else
					"Emulator"
#endif
				);
			}
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(model.type.c_str());
			ImGui::SameLine();
			ImGui::Dummy({0, ImGui::GetTextLineHeightWithSpacing()});
			ImGui::PopID();
		}
	};
} // namespace casioemu
std::string sui_loop() {
	SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
	windows2 = new std::vector<UIWindow*>();
	casioemu::StartupUi ui;
	{
		std::ifstream ifs1{"recent.bin", std::ifstream::binary};
		if (ifs1)
			Binary::Read(ifs1, ui.recently_used);
	}
	window2 = SDL_CreateWindow(
		"CasioEmuMsvc",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		1200, 800,
		SDL_WINDOW_SHOWN | (SDL_WINDOW_RESIZABLE));
	renderer2 = SDL_CreateRenderer(window2, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
	if (renderer2 == nullptr) {
		SDL_Log("Error creating SDL_Renderer!");
		return "";
	}
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	SetupDefaultFont();
	io.WantCaptureKeyboard = true;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	SetupDefaultTheme();
	ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 1.0f;

	// Setup Platform/Renderer backends
	ImGui_ImplSDL2_InitForSDLRenderer(window2, renderer2);
	ImGui_ImplSDLRenderer2_Init(renderer2);
	while (1) {
		ImGui_ImplSDLRenderer2_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();
		ui.Render();
		for (auto& wind : *windows2) {
			wind->Render();
		}
		ImGui::EndFrame();
		ImGui::Render();
		SDL_RenderSetScale(renderer2, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
		SDL_SetRenderDrawColor(renderer2, 0, 0, 0, 255);
		SDL_RenderClear(renderer2);
		ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
		SDL_RenderPresent(renderer2);
		SDL_Event event;
		if (!SDL_PollEvent(&event))
			continue;
		switch (event.type) {

		case SDL_WINDOWEVENT:
			switch (event.window.event) {
			case SDL_WINDOWEVENT_CLOSE:
				goto exit;
			}
			break;

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
		case SDL_KEYDOWN:
		case SDL_KEYUP:
		case SDL_TEXTINPUT:
		case SDL_MOUSEMOTION:
		case SDL_MOUSEWHEEL:
			ImGui_ImplSDL2_ProcessEvent(&event);
			break;
		}
		if (!ui.selected_path.empty())
			goto exit;
	}
exit:
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
	return ui.selected_path.string();
}