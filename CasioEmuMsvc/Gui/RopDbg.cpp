#include "RopDbg.h"
#include "imgui/imgui.h"
//#include <Python.h>

RopDbg::RopDbg() {
	//Py_Initialize();
}

void RopDbg::Draw() {
	ImGui::Begin("Rop Dbg");
	ImGui::InputTextMultiline("##code", buffer, 65536, ImVec2{ 0, ImGui::GetWindowHeight() });
	ImGui::End();
}
