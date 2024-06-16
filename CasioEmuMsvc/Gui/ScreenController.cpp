#include "ScreenController.h"
#include "imgui\imgui.h"
#include "..\Config.hpp"

int screen_flashing_threshold = 20;
float screen_fading_blending_coefficient = 0;
bool enable_screen_fading = true;
float screen_flashing_brightness_coeff = 1.5f;
void ScreenController::Draw() {
	ImGui::Begin(
#if LANGUAGE == 2
		"屏幕控制器"
	//""
#else
		"Screen Controller"
#endif
	);
	// TODO: FINISH THIS
	ImGui::SliderInt(
#if LANGUAGE == 2
		"闪屏模拟阈值"
#else
		"screen_flashing_threshold"
#endif
		,
		&screen_flashing_threshold, 0, 0x3F);
	ImGui::SliderFloat(
#if LANGUAGE == 2
		"闪屏对比度补偿"
#else
		"screen_flashing_brightness_coeff"
#endif
		,
		&screen_flashing_brightness_coeff, 1, 8);
	ImGui::Checkbox(
#if LANGUAGE == 2
		"启用屏幕渐变"
#else
		"enable_screen_fading"
#endif
		,
		&enable_screen_fading);
	if (enable_screen_fading)
		ImGui::SliderFloat(
#if LANGUAGE == 2
			"屏幕渐变混合因子"
#else
			"screen_fading_blending_coefficient"
#endif
			,
			&screen_fading_blending_coefficient, 0, 1);
	ImGui::End();
}
