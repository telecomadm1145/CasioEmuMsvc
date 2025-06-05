#pragma once

#include "Emulator.hpp"
#include "Ui.hpp"
#include "hex.hpp"
#include <cstdint>

class WatchWindow : public UIWindow {
private:
	uint8_t reg_rx[16][3];
	char reg_lr[10], reg_sp[5], reg_ea[10], reg_pc[10], reg_psw[3], reg_dsr[3];
	char reg_ex1[10];
	char reg_ex2[10];
	char reg_ex3[10];
	int char_width;
	MemoryEditor mem_editor;

public:
	WatchWindow() : UIWindow("Watch"){};

	void RenderCore() override;

	void ShowRX();

	void PrepareRX();
	void ModRX();

	void UpdateRX();
};