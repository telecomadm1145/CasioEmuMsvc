#pragma once
#include "hex.hpp"
class AppMemoryView {
	MemoryEditor mem_edit;
	MemoryEditor::OptionalMarkedSpans spans{ std::vector<MemoryEditor::MarkedSpan>{
	} };

public:
	AppMemoryView();
	void Draw();
};
