#include "KeyLog.h"
#include "imgui/imgui.h"
char m_keylog_buffer[4096]{ 0 };
int m_keylog_i = 0;
bool m_keylog_enabled = false;

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
	ImGui::Checkbox("记录", &m_keylog_enabled);
	ImGui::End();
}
