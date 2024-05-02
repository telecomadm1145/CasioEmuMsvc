#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING 1
#include "KeyLog.h"
#include "imgui/imgui.h"
#include <Windows.h>
#include <string>
#include <codecvt>
char m_keylog_buffer[4096]{ 0 };
int m_keylog_i = 0;
bool m_keylog_enabled = false;

inline int set_clipboard_text(const char* text) {
	// 打开剪贴板
	if (!OpenClipboard(NULL))
	{
		// 处理打开剪贴板失败的情况
		return 1;
	}

	// 获取文本长度
	std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
	std::u16string utf16String = converter.from_bytes(text);
	int textLength = (utf16String.size() + 1) * 2;
	// 分配全局内存
	HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, textLength);
	if (hGlobal == NULL)
	{
		// 处理内存分配失败的情况
		CloseClipboard();
		return 1;
	}

	// 锁定内存并获取指向内存的指针
	char* memPtr = (char*)GlobalLock(hGlobal);
	if (memPtr == NULL)
	{
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
	if (SetClipboardData(CF_UNICODETEXT, hGlobal) == NULL)
	{
		// 处理设置剪贴板数据失败的情况
		GlobalFree(hGlobal);
		CloseClipboard();
		return 1;
	}

	// 关闭剪贴板
	CloseClipboard();
}

void KeyLog::Draw()
{
	ImGui::Begin("按键日志");
	ImGui::BeginChild("##keylog", ImVec2(0, ImGui::GetWindowHeight() / 2));
	ImGui::TextWrapped(m_keylog_buffer);
	ImGui::EndChild();
	if (ImGui::Button("重置并开始")) {
		m_keylog_enabled = false;// TODO: avoid race condition there
		memset(m_keylog_buffer, 0, 4096);
		m_keylog_i = 0;
		m_keylog_enabled = true;
	}
	if (ImGui::Button("全部复制")) {
		set_clipboard_text(m_keylog_buffer);
	}
	ImGui::Checkbox("记录", &m_keylog_enabled);
	ImGui::End();
}
