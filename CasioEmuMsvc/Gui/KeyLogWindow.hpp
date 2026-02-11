#pragma once
#include "Ui.hpp"
#include "Peripheral/Keyboard.hpp"

class KeyLogWindow : public UIWindow {
public:
	KeyLogWindow();
	void RenderCore() override;
};
