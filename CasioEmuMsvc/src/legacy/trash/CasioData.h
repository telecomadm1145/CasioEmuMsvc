#pragma once
#include "Ui.hpp"
class CasioData : public UIWindow {
public:
	CasioData() : UIWindow("DataView") {}
	void RenderCore() override;
};