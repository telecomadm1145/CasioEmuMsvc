#pragma once
#include "Ui.hpp"
class VariableWindow : public UIWindow {
public:
	VariableWindow() : UIWindow("Variables") {}
	void RenderCore() override;
};
