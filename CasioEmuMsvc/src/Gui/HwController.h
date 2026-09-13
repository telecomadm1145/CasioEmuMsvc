#pragma once
#include "Ui.hpp"
#include "../Peripheral/ScreenGate.hpp"
extern float screen_fading_blending_coefficient;
extern float screen_flashing_brightness_coeff;
extern bool screen_residual_enabled;
extern float screen_residual_alpha_scale;
extern int screen_buffer_select;
extern bool audio_enable;
class HwController : public UIWindow {
public:
	HwController() : UIWindow("Hardware") {}
	void RenderCore() override;
};
