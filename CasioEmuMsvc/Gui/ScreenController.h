#pragma once
extern int screen_flashing_threshold;
extern float screen_fading_blending_coefficient;
extern bool enable_screen_fading;
extern float screen_flashing_brightness_coeff;
class ScreenController {
public:
	void Draw();
};
