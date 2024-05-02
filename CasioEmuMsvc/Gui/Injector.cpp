#include "Injector.hpp"
#include "imgui/imgui.h"
#include "hex.hpp"
#include "../Chipset/Chipset.hpp"
#include "../Peripheral/BatteryBackedRAM.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

#include "../Config.hpp"
#include "ui.hpp"
Injector::Injector()
{
	data_buf = new char[1024];
	memset(data_buf, 0, 1024);
	ImGuiIO& io = ImGui::GetIO();
	//io.Fonts->AddFontFromFileTTF("NotoSansSC-Medium.otf", 18, nullptr, io.Fonts->GetGlyphRangesDefault());
	io.Fonts->AddFontFromFileTTF("NotoSansSC-Medium.otf", 18, nullptr, io.Fonts->GetGlyphRangesChineseFull());
	io.Fonts->Build();
}

void Injector::Show() {
	ImGui::Begin("注入辅助");

	static float scale = -1.0f;
	static int range = 64;
	static char strbuf[1024] = { 0 };
	static char buf[10] = { 0 };
	static MemoryEditor editor;
	static const char* info_msg;
	ImGui::BeginChild("输入内容", ImVec2(0, ImGui::GetWindowHeight() * 0.4));
	editor.DrawContents(data_buf, range);
	ImGui::EndChild();
	ImGui::SliderInt("输入内容大小", &range, 64, 1024);
	ImGui::Text("an前数字");
	ImGui::SameLine();
	ImGui::InputText("偏移", buf, 9);
	char* base_addr = n_ram_buffer - 0x9000;
	if (ImGui::Button("计算模式数学输入")) {
		*(base_addr + 0x91A1) = 0xc1;
		*(base_addr + 0x91AE) = 0x01;
		info_msg = "模式已修改";
		ImGui::OpenPopup("info");
	}
	if (ImGui::Button("输入 an")) {
		int off = atoi(buf);
		if (off > 100) {
			memset(base_addr + 0x9268, 0x31, 100);
			memset(base_addr + 0x9268 + 100, 0xa6, 1);
			memset(base_addr + 0x9268 + 101, 0x31, off - 100);
		}
		else {
			memset(base_addr + 0x9268, 0x31, off);
		}
		*(base_addr + 0x9268 + off) = 0xfd;
		*(base_addr + 0x9268 + off + 1) = 0x20;
		info_msg = "an 已输入";
		ImGui::OpenPopup("info");
	}
	if (ImGui::Button("加载字符串")) {
		memcpy(base_addr + 0x9268, data_buf, range);
		info_msg = "字符串已加载";
		ImGui::OpenPopup("info");
	}
	if (ImGui::Button("加载Hex字符串")) {

		info_msg = "字符串已加载";

		auto valid_hex = [](char c) {
			if (c >= '0' && c <= '9') return true;
			if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) return true;
			return false;
			};
		size_t i = 0, j = 0;
		char hex_buf[3];
		while (strbuf[i] != '\0' && strbuf[i + 1] != '\0') {
			if (strbuf[i] == ';' || strbuf[i + 1] == ';') {
				// begin comment; skip the rest of the line
				for (;; ++i) {
					if (strbuf[i] == '\0') goto exit;
					if (strbuf[i] == '\n') {
						++i;
						break;
					}
				}
			}
			else {
				if (!(valid_hex(strbuf[i]) && valid_hex(strbuf[i + 1]))) {
					++i;
					continue;
				}
				hex_buf[0] = strbuf[i], hex_buf[1] = strbuf[i + 1], hex_buf[2] = '\0';
				uint8_t byte = strtoul(hex_buf, nullptr, 16);
				*(base_addr + 0x9268 + j) = (char)byte;
				*(data_buf + j) = (char)byte;
				i += 2;
				++j;
			}
		}
	exit:
		ImGui::OpenPopup("info");

	}
	ImGui::SameLine();
	ImGui::InputTextMultiline("输入Hex字符串", strbuf, IM_ARRAYSIZE(strbuf) - 1);

	if (ImGui::BeginPopupModal("info", 0, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text(info_msg);
		if (ImGui::Button("好的")) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}


	ImGui::End();
}