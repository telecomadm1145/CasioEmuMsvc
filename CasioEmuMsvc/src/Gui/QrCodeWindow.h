#pragma once

#include "Ui.hpp"

class QrCodeWindow : public UIWindow {
public:
	QrCodeWindow();
	void RenderCore() override;
};
