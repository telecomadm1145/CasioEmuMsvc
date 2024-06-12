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
#include <windows.h>
#include <fstream>
#include <iostream>
#include <filesystem>
Injector::Injector() {
	data_buf = new char[1024];
	memset(data_buf, 0, 1024);
}
static std::string OpenFile() {
	OPENFILENAMEA ofn;
	char fileName[MAX_PATH] = "";

	ZeroMemory(&ofn, sizeof(ofn));

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFilter = "All Files (*.*)\0*.*\0";
	ofn.lpstrFile = fileName;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

	if (GetOpenFileNameA(&ofn)) {
		return std::string(fileName);
	}

	return "";
}

void Injector::Show() {
	ImGui::Begin(
#if LANGUAGE == 2
		"注入辅助"
#else
		"Injector"
#endif
	);

	static float scale = -1.0f;
	static int range = 64;
	static char strbuf[1024] = { 0 };
	static char buf[10] = { 0 };
	static MemoryEditor editor;
	static const char* info_msg;
	ImGui::BeginChild(
#if LANGUAGE == 2
		"输入内容"
#else
		"Input"
#endif
		,
		ImVec2(0, ImGui::GetWindowHeight() * 0.4));
	editor.DrawContents(data_buf, range);
	ImGui::EndChild();
	// if (ImGui::Button("暗色")) {
	//	ImGui::StyleColorsDark();
	// }
	// ImGui::SameLine();
	// if (ImGui::Button("亮色")) {
	//	ImGui::StyleColorsLight();
	// }
	ImGui::SliderInt(
#if LANGUAGE == 2
		"输入内容大小"
#else
		"Input Size"
#endif
		,
		&range, 64, 1024);
	// ImGuiIO& io = ImGui::GetIO();
	// ImGui::SliderFloat("缩放", &io.FontGlobalScale, 0.5, 2);
	ImGui::Text(
#if LANGUAGE == 2
		"an前数字"
#else
		"x an"
#endif
	);
	ImGui::SameLine();
	ImGui::InputText(
#if LANGUAGE == 2
		"偏移"
#else
		"Offset"
#endif
		,
		buf, 9);
	char* base_addr = n_ram_buffer - 0x9000;
	if (ImGui::Button(
#if LANGUAGE == 2
			"计算模式数学输入"
#else
			"Math IO with Calc Mode"
#endif
			)) {
		*(base_addr + 0x91A1) = 0xc1;
		*(base_addr + 0x91AE) = 0x01;
#if LANGUAGE == 2
		info_msg = "模式已修改";
#else
		info_msg = "Mode changed";
#endif
		ImGui::OpenPopup("info");
	}
	if (ImGui::Button(
#if LANGUAGE == 2
			"输入 an"
#else
			"Input \"an\""
#endif
			)) {
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
#if LANGUAGE == 2
		info_msg = "\"an\" 已输入";
#else
		info_msg = "\"an\" Inputed";
#endif
		ImGui::OpenPopup("info");
	}
	if (ImGui::Button(
#if LANGUAGE == 2
			"加载数据到输入区"
#else
			"Load to input area"
#endif
			)) {
		memcpy(base_addr + 0x9268, data_buf, range);
		info_msg = "字符串已加载";
		ImGui::OpenPopup("info");
	}
	// if (ImGui::Button("加载 Rop 二进制文件")) {
	//	auto f = OpenFile();
	//	std::ifstream ifs2(f, std::ios::in | std::ios::binary);
	//	if (!ifs2) {
	//		info_msg = "文件打开失败";
	//		ImGui::OpenPopup("info");
	//	}
	//	else {
	//		char load[0x1000]{};
	//		ifs2.get(load, 0x1000);
	//		ifs2.seekg(0, std::ios::end);
	//		auto sz = (size_t)ifs2.tellg();
	//		if (sz < 0xF7) {
	//			info_msg = "Rop 二进制文件不正确";
	//			ImGui::OpenPopup("info");
	//		}
	//		else {
	//			memcpy(base_addr + 0x9268, load + 0x8, 200);
	//			memcpy(base_addr + 0x965E, load + 0x5E, 0xF7 - 0x5E);
	//			if (sz > 0xF8) {
	//				std::cout << "Loaded to stack:" << sz - 0xF8 << "\n";
	//				memcpy(base_addr + 0xE300, load + 0xF8, sz - 0xF8);
	//			}
	//			info_msg = "Rop 已加载，按 Exe 执行";
	//			ImGui::OpenPopup("info");
	//		}
	//	}
	// }
	if (ImGui::Button(
#if LANGUAGE == 2
			"加载Hex字符串"
#else
			"Load hex string"
#endif
			)) {
#if LANGUAGE == 2
		info_msg = "字符串已加载";
#else
		info_msg = "String loaded";
#endif

		auto valid_hex = [](char c) {
			if (c >= '0' && c <= '9')
				return true;
			if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
				return true;
			return false;
		};
		size_t i = 0, j = 0;
		char hex_buf[3];
		while (strbuf[i] != '\0' && strbuf[i + 1] != '\0') {
			if (strbuf[i] == ';' || strbuf[i + 1] == ';') {
				// begin comment; skip the rest of the line
				for (;; ++i) {
					if (strbuf[i] == '\0')
						goto exit;
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
	ImGui::InputTextMultiline(
#if LANGUAGE == 2
		"输入Hex字符串"
#else
		"Input hex string"
#endif
		,
		strbuf, IM_ARRAYSIZE(strbuf) - 1);

	if (ImGui::BeginPopupModal("info", 0, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text(info_msg);
		if (ImGui::Button(
#if LANGUAGE == 2
				"好的"
#else
				"Ok"
#endif
				)) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}


	ImGui::End();
}