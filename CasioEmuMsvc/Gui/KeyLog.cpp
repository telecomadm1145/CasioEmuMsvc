#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING 1
#include "KeyLog.h"
#include "imgui/imgui.h"
#include <Windows.h>
#include <string>
#include <codecvt>
#include <thread>
#include "../config.hpp"
char m_keylog_buffer[4096]{ 0 };
int m_keylog_i = 0;
bool m_keylog_enabled = false;
std::function<void(int, int)> keybd_in;

inline int set_clipboard_text(const char* text) {
	// 打开剪贴板
	if (!OpenClipboard(NULL)) {
		// 处理打开剪贴板失败的情况
		return 1;
	}

	// 获取文本长度
	std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
	std::u16string utf16String = converter.from_bytes(text);
	int textLength = (utf16String.size() + 1) * 2;
	// 分配全局内存
	HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, textLength);
	if (hGlobal == NULL) {
		// 处理内存分配失败的情况
		CloseClipboard();
		return 1;
	}

	// 锁定内存并获取指向内存的指针
	char* memPtr = (char*)GlobalLock(hGlobal);
	if (memPtr == NULL) {
		// 处理内存锁定失败的情况
		GlobalFree(hGlobal);
		CloseClipboard();
		return 1;
	}

	// 将文本复制到内存中
	memcpy(memPtr, utf16String.c_str(), textLength);

	// 解锁内存
	GlobalUnlock(hGlobal);

	// 清空剪贴板
	EmptyClipboard();

	// 设置剪贴板数据
	if (SetClipboardData(CF_UNICODETEXT, hGlobal) == NULL) {
		// 处理设置剪贴板数据失败的情况
		GlobalFree(hGlobal);
		CloseClipboard();
		return 1;
	}

	// 关闭剪贴板
	CloseClipboard();
}
std::string get_clipboard_text() {
	// 打开剪贴板
	if (!OpenClipboard(nullptr)) {
		return "";
	}

	// 获取剪贴板中的文本
	HANDLE hData = GetClipboardData(CF_UNICODETEXT);
	if (hData == nullptr) {
		CloseClipboard();
		return "";
	}

	// 锁定剪贴板中的文本数据
	char16_t* pszText = static_cast<char16_t*>(GlobalLock(hData));
	if (pszText == nullptr) {
		CloseClipboard();
		return "";
	}
	std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;

	// 复制文本数据到 std::string
	std::string text = converter.to_bytes(pszText);

	// 解锁数据并关闭剪贴板
	GlobalUnlock(hData);
	CloseClipboard();

	return text;
}
template <class T>
inline std::u32string Utf82Ucs4(const std::basic_string<T>& utf8String) {
	std::u32string ucs4String;

	for (size_t i = 0; i < utf8String.length();) {
		char32_t unicodeValue = 0;
		size_t codeLength = 0;

		// Check the number of leading 1s to determine the length of the UTF-8 code
		if ((utf8String[i] & 0x80) == 0) {
			// Single-byte character (ASCII)
			unicodeValue = utf8String[i];
			codeLength = 1;
		}
		else if ((utf8String[i] & 0xE0) == 0xC0) {
			// Two-byte character
			unicodeValue = (utf8String[i] & 0x1F) << 6;
			unicodeValue |= (utf8String[i + 1] & 0x3F);
			codeLength = 2;
		}
		else if ((utf8String[i] & 0xF0) == 0xE0) {
			// Three-byte character
			unicodeValue = (utf8String[i] & 0x0F) << 12;
			unicodeValue |= (utf8String[i + 1] & 0x3F) << 6;
			unicodeValue |= (utf8String[i + 2] & 0x3F);
			codeLength = 3;
		}
		else if ((utf8String[i] & 0xF8) == 0xF0) {
			// Four-byte character
			unicodeValue = (utf8String[i] & 0x07) << 18;
			unicodeValue |= (utf8String[i + 1] & 0x3F) << 12;
			unicodeValue |= (utf8String[i + 2] & 0x3F) << 6;
			unicodeValue |= (utf8String[i + 3] & 0x3F);
			codeLength = 4;
		}
		else {
			unicodeValue = '?';
			codeLength = 1;
		}

		ucs4String.push_back(unicodeValue);
		i += codeLength;
	}

	return ucs4String;
}

char input_buf[4096]{};
bool stop = true;
void KeyLog::Draw() {
	ImGui::Begin(
#if LANGUAGE == 2
		"按键日志"
#else
		"Keylog"
#endif
	);
	ImGui::BeginChild("##keylog", ImVec2(0, ImGui::GetWindowHeight() / 2), true);
	ImGui::TextWrapped(m_keylog_buffer);
	ImGui::EndChild();
	if (ImGui::Button(
#if LANGUAGE == 2
			"重置并开始"
#else
			"Reset&Start"
#endif
			)) {
		m_keylog_enabled = false; // TODO: avoid race condition there
		memset(m_keylog_buffer, 0, 4096);
		m_keylog_i = 0;
		m_keylog_enabled = true;
	}
	ImGui::SameLine();
	if (ImGui::Button(
#if LANGUAGE == 2
			"重置"
#else
			"Reset"
#endif
			)) {
		memset(m_keylog_buffer, 0, 4096);
		m_keylog_i = 0;
	}
	if (ImGui::Button(
#if LANGUAGE == 2
			"全部复制"
#else
			"Copy all"
#endif
			)) {
		set_clipboard_text(m_keylog_buffer);
	}
	ImGui::InputText(
		"##rec",
		input_buf, 500);
	ImGui::SameLine();
	if (ImGui::Button(

#if LANGUAGE == 2
			"从剪贴板导入"
#else
			"Import from clipboard"
#endif

			)) {
		auto cb = get_clipboard_text();
		strcpy(input_buf, cb.c_str());
	}
	if (stop) {
		if (ImGui::Button(

#if LANGUAGE == 2
				"执行"
#else
				"Execute"
#endif

				)) {
			stop = false;
			std::thread thd{ []() {
				// cwii_keymap[7 - getHighestBitPosition(button.ki_bit)][getHighestBitPosition(button.ko_bit)]
				auto datas = Utf82Ucs4<char>(input_buf);
				for (auto chr : datas) {
					if (chr < 0xE000)
						continue;
					if (chr == 0xE035) {
						keybd_in(114514, 0);
						std::this_thread::sleep_for(std::chrono::milliseconds(1000));
						continue;
					}
					int x = -1, y = -1;
					for (size_t ki = 0; ki < 8; ki++) {
						for (size_t ko = 0; ko < 7; ko++) {
							if (cwii_keymap[7 - ki][ko] == chr - 0xE000) {
								x = ki;
								y = ko;
								goto ex;
							}
						}
					}
				ex:
					if (x == -1 || y == -1)
						continue;
					if (stop)
						break;
					keybd_in(x, y);
					std::this_thread::sleep_for(std::chrono::milliseconds(300));
				}
				stop = true;
			} };
			thd.detach();
		}
	}
	else {
		if (ImGui::Button(

#if LANGUAGE == 2
				"停止"
#else
				"Stop"
#endif

				)) {
			stop = true;
		}
	}
	ImGui::Checkbox(

#if LANGUAGE == 2
		"启用"
#else
		"Enabled"
#endif
		,
		&m_keylog_enabled);
	ImGui::End();
}
