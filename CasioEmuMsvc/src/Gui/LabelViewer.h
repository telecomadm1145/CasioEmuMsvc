#pragma once
#include "Ui.hpp"
class LabelViewer : public UIWindow {
private:
	char m_SearchBuf[64] = {};
public:
	LabelViewer() : UIWindow("Labels"){}
	void RenderCore() override;
};
