#pragma once
#include "Ui.hpp"
extern int screen_flashing_threshold;
extern float screen_fading_blending_coefficient;
extern bool enable_screen_fading;
extern float screen_flashing_brightness_coeff;
extern int screen_buffer_select;
extern bool audio_enable;
class HwController : public UIWindow {
public:
	HwController() : UIWindow("Hardware") {}
	void RenderCore() override;
};
