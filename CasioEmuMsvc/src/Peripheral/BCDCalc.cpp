#include "BCDCalc.hpp"

#include "Chipset/Chipset.hpp"
#include "Chipset/CPU.hpp"
#include "Chipset/MMU.hpp"
#include "Chipset/MMURegion.hpp"
#include "Emulator.hpp"
#include "Logger.hpp"
#include "ModelInfo.h"

namespace casioemu {
	class BCDCalc : public Peripheral {
		MMURegion region_BCDCMD, region_BCDCON, region_BCDMCN, region_BCDMCR, region_BCDFLG, region_BCDLLZ, region_BCDMLZ,
			region_BCDREGA, region_BCDREGB, region_BCDREGC, region_BCDREGD;

		uint8_t data_BCDCMD, data_BCDCON, data_BCDMCN, data_BCDLLZ, data_BCDMLZ;
		uint8_t BCDREG[4][12];

		uint8_t BCDMCN;
		bool C_flag, Z_flag, macro_running;

		uint8_t BCDCMD_req, BCDMCR_req;
		bool BCDCMD_pend, BCDMCR_pend;

		uint16_t* current_pgm;
		uint8_t pgm_counter;

		uint16_t mul_pgm[32] = {0x21B7, 0x21B6, 0x211A, 0x211A, 0x21A1, 0x86D0, 0x1F91, 0x2F91, 0x2F91, 0x2F91, 0x2F91, 0x2F91, 0x2F91, 0x2F91, 0x2F91, 0x2F91,
			0x1F91, 0x1F91, 0x1F91, 0x1F91, 0x1F91, 0x1F91, 0x1F1D, 0x161D, 0x212D, 0x1F19, 0x1619, 0x1719, 0x1819, 0x1919, 0x1A19, 0xE500};
		uint16_t div_pgm[32] = {0xBC00, 0xBC00, 0xBC00, 0xBC00, 0xBC00, 0xBC00, 0xBC00, 0xBC00, 0xBC00, 0xBC00, 0x7029, 0x7329, 0x4929, 0x681D, 0x671D, 0x061D,
			0x621D, 0x611D, 0x001D, 0x651D, 0x641D, 0x031D, 0x21B7, 0x21B6, 0x2116, 0x2116, 0x21B1, 0x22A0, 0x2181, 0x0A80, 0xFC00, 0xFC00};
		uint16_t divsn_pgm[32] = {0xBC00, 0xBC00, 0xBC00, 0xBC00, 0xBC00, 0xBC00, 0xBC00, 0xBC00, 0xBC00, 0xBC00, 0x7029, 0x7329, 0x4929, 0x681D, 0x671D, 0x061D,
			0x621D, 0x611D, 0x001D, 0x651D, 0x641D, 0x031D, 0x21B7, 0x21B6, 0x2116, 0x2116, 0x21CD, 0x0A8C, 0x21C1, 0x0A80, 0xFC00, 0xFC00};
		uint16_t sft_pgm[32] = {0xC180, 0xC284, 0xC488, 0xC88C, 0xC181, 0xC285, 0xC489, 0xC88D, 0xC182, 0xC286, 0xC48A, 0xC88E, 0xC183, 0xC287, 0xC48B, 0xC88F,
			0xC190, 0xC294, 0xC498, 0xC89C, 0xC191, 0xC295, 0xC499, 0xC89D, 0xC192, 0xC296, 0xC49A, 0xC89E, 0xC193, 0xC297, 0xC49B, 0xC89F};

		uint16_t* pgm_ptr[16] = {nullptr, nullptr, mul_pgm, mul_pgm, div_pgm, div_pgm, divsn_pgm, divsn_pgm,
			sft_pgm, sft_pgm, sft_pgm, sft_pgm, sft_pgm, sft_pgm, sft_pgm, sft_pgm};
		uint8_t pgm_entry[16] = {0x00, 0x00, 0x00, 0x05, 0x16, 0x1C, 0x16, 0x1C, 0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C};

		static uint8_t ReadReg(MMURegion* region, size_t offset) {
			offset -= region->base;
			return ((uint8_t*)region->userdata)[offset];
		}

		static void WriteReg(MMURegion* region, size_t offset, uint8_t data) {
			offset -= region->base;
			((uint8_t*)region->userdata)[offset] = data;
		}

	public:
		using Peripheral::Peripheral;

		void Initialise();
		void Reset();
		void Tick();

		void RunCommand(uint8_t);
		void StartMacro(uint8_t);
		void ShiftLeft(uint8_t, uint8_t, bool);
		void ShiftRight(uint8_t, uint8_t, bool);
	};
	void BCDCalc::Initialise() {
		clock_type = CLOCK_HSCLK;

		Reset();

		region_BCDCMD.Setup(0xF400, 1, "BCD/BCDCMD", this, [](MMURegion* region, size_t) { return ((BCDCalc*)region->userdata)->data_BCDCMD; }, [](MMURegion* region, size_t, uint8_t data) {
			BCDCalc* bcd = (BCDCalc*)region->userdata;
			bcd->BCDCMD_req = data;
			if (!bcd->macro_running) {
				bcd->RunCommand(data);
			}
			else {
				bcd->emulator.chipset.cpu.cpu_run_stat = false;
				bcd->BCDCMD_pend = true;
			} }, emulator);

		region_BCDCON.Setup(0xF402, 1, "BCD/BCDCON", &data_BCDCON, MMURegion::DefaultRead<uint8_t>, [](MMURegion* region, size_t, uint8_t data) {
			data &= 0x0F;
			if (data < 1) data = 1;
			if (data > 6) data = 6;
			*(uint8_t*)region->userdata = data; }, emulator);

		region_BCDMCN.Setup(0xF404, 1, "BCD/BCDMCN", &data_BCDMCN, MMURegion::DefaultRead<uint8_t, 0x1F>, MMURegion::DefaultWrite<uint8_t, 0x1F>, emulator);

		region_BCDMCR.Setup(0xF405, 1, "BCD/BCDMCR", this, [](MMURegion* region, size_t) { return (uint8_t)(((BCDCalc*)region->userdata)->macro_running ? 0x80 : 0); }, [](MMURegion* region, size_t, uint8_t data) {
			BCDCalc* bcd = (BCDCalc*)region->userdata;
			bcd->BCDMCR_req = data;
			if (!bcd->macro_running) {
				bcd->StartMacro(data);
			}
			else {
				bcd->emulator.chipset.cpu.cpu_run_stat = false;
				bcd->BCDMCR_pend = true;
			} }, emulator);

		region_BCDFLG.Setup(0xF410, 1, "BCD/BCDFLG", this, [](MMURegion* region, size_t) {
			BCDCalc* bcd = (BCDCalc*)region->userdata;
			return uint8_t((bcd->C_flag ? 0x80 : 0) | (bcd->Z_flag ? 0x40 : 0)); }, [](MMURegion* region, size_t, uint8_t data) {
			BCDCalc* bcd = (BCDCalc*)region->userdata;
			bcd->C_flag = (data & 0x80) != 0;
			bcd->Z_flag = (data & 0x40) != 0; }, emulator);

		region_BCDLLZ.Setup(0xF414, 1, "BCD/BCDLLZ", &data_BCDLLZ, MMURegion::DefaultRead<uint8_t>, MMURegion::IgnoreWrite, emulator);
		region_BCDMLZ.Setup(0xF415, 1, "BCD/BCDMLZ", &data_BCDMLZ, MMURegion::DefaultRead<uint8_t>, MMURegion::IgnoreWrite, emulator);

		region_BCDREGA.Setup(0xF480, 12, "BCD/BCDREGA", BCDREG[0], ReadReg, WriteReg, emulator);
		region_BCDREGB.Setup(0xF4A0, 12, "BCD/BCDREGB", BCDREG[1], ReadReg, WriteReg, emulator);
		region_BCDREGC.Setup(0xF4C0, 12, "BCD/BCDREGC", BCDREG[2], ReadReg, WriteReg, emulator);
		region_BCDREGD.Setup(0xF4E0, 12, "BCD/BCDREGD", BCDREG[3], ReadReg, WriteReg, emulator);
	}

	void BCDCalc::Tick() {
		if (!macro_running)
			return;

	fetch:
		uint16_t inst = current_pgm[pgm_counter];
		uint8_t offset = (inst >> 8) & 0x1F;
		uint8_t cond = 0;

		switch (inst >> 13) {
		case 0:
			pgm_counter = offset;
			break;
		case 1:
			pgm_counter = (pgm_counter + offset) & 0x1F;
			break;
		case 2:
			pgm_counter++;
			cond = 1;
			break;
		case 3:
			pgm_counter++;
			cond = 2;
			break;
		case 4:
			pgm_counter = ((BCDREG[0][0] & 0x0F) + offset) & 0x1F;
			break;
		case 5:
			BCDREG[0][0] = (BCDREG[0][0] & 0xF0) | (pgm_counter & 0x0F);
			pgm_counter = offset;
			if (--BCDMCN)
				goto fetch;
			macro_running = false;
			break;
		case 6:
			BCDMCN -= offset;
			pgm_counter &= 0xFC;
			if (BCDMCN & 0xF8)
				pgm_counter |= 3;
			else if (BCDMCN & 0x04)
				pgm_counter |= 2;
			else if (BCDMCN & 0x02)
				pgm_counter |= 1;
			else if (!BCDMCN)
				macro_running = false;
			break;
		case 7:
			pgm_counter = offset;
			if (--BCDMCN)
				goto fetch;
			macro_running = false;
			break;
		}

		RunCommand(inst & 0xFF);
		if (cond && ((cond & 1) ^ C_flag))
			pgm_counter = offset;

		if (!macro_running) {
			if (BCDCMD_pend) {
				emulator.chipset.cpu.cpu_run_stat = true;
				BCDCMD_pend = false;
				RunCommand(BCDCMD_req);
			}
			if (BCDMCR_pend) {
				emulator.chipset.cpu.cpu_run_stat = true;
				BCDMCR_pend = false;
				StartMacro(BCDMCR_req);
			}
		}
	}

	void BCDCalc::Reset() {
		data_BCDCMD = data_BCDMCN = data_BCDLLZ = data_BCDMLZ = 0;
		data_BCDCON = 6;

		BCDCMD_req = BCDMCR_req = 0;
		C_flag = Z_flag = macro_running = BCDCMD_pend = BCDMCR_pend = false;

		current_pgm = nullptr;
		pgm_counter = 0;
	}

	void BCDCalc::RunCommand(uint8_t cmd) {
		data_BCDCMD = cmd;
		uint8_t src = (cmd >> 2) & 3, dst = cmd & 3, op = cmd >> 4;
		bool arithmetic_mode = (op & 0x08) == 0;
		int calc_ptr = (op >= 8 || op == 1 || op == 2) ? 0 : (op == 7 ? 2 : 6);
		while (calc_ptr < 6) {
			if (calc_ptr == 0) {
				C_flag = false;
				Z_flag = true;
			}
			bool carry = op == 2 ? !C_flag : C_flag;
			uint16_t res = 0, op_src = BCDREG[src][calc_ptr << 1] | uint16_t(BCDREG[src][(calc_ptr << 1) + 1] << 8),
					 op_dst = BCDREG[dst][calc_ptr << 1] | uint16_t(BCDREG[dst][(calc_ptr << 1) + 1] << 8);
			for (int i = 0; i < 4; i++) {
				uint8_t op1 = op_src & 0x0F, op2 = op_dst & 0x0F;
				op_src >>= 4;
				op_dst >>= 4;
				if (op == 2)
					op1 = (9 - op1) & 0x0F;
				op2 += op1 + (carry ? 1 : 0);
				if ((carry = op2 >= 10))
					op2 -= 10;
				res |= op2 << (i * 4);
			}
			if (op == 2)
				carry = !carry;
			if (res)
				Z_flag = false;
			if (arithmetic_mode) {
				BCDREG[dst][calc_ptr << 1] = res & 0xFF;
				BCDREG[dst][(calc_ptr << 1) + 1] = res >> 8;
				C_flag = carry;
				calc_ptr++;
				if (op < 7 && calc_ptr >= data_BCDCON)
					break;
			}
			else {
				if (calc_ptr < data_BCDCON)
					C_flag = carry;
				if (calc_ptr++)
					break;
			}
		}

		if (!arithmetic_mode) {
			switch (op & 0x07) {
			case 0:
				ShiftLeft(src, dst, false);
				break;
			case 1:
				ShiftRight(src, dst, false);
				break;
			case 2:
				memset(BCDREG[dst], 0, 12);
				BCDREG[dst][0] = src == 3 ? 5 : src;
				break;
			case 3:
				memcpy(BCDREG[dst], BCDREG[src], 12);
				break;
			case 4:
				ShiftLeft(src, dst, true);
				break;
			case 5:
				ShiftRight(src, dst, true);
				break;
			case 7:
				memset(BCDREG[dst], 0, 12);
				break;
			default:
				break;
			}
		}

		data_BCDLLZ = 0;
		for (int i = 0; i < 12; i++) {
			if (i >= 2 * data_BCDCON) {
				data_BCDLLZ += 2;
				continue;
			}
			if (BCDREG[dst][i] & 0x0F)
				break;
			data_BCDLLZ++;
			if (BCDREG[dst][i] >> 4)
				break;
			data_BCDLLZ++;
		}

		data_BCDMLZ = 24 - 4 * data_BCDCON;
		for (int i = 2 * data_BCDCON - 1; i >= 0; i--) {
			if (BCDREG[dst][i] >> 4)
				break;
			data_BCDMLZ++;
			if (BCDREG[dst][i] & 0x0F)
				break;
			data_BCDMLZ++;
		}
	}

	void BCDCalc::StartMacro(uint8_t index) {
		BCDMCN = data_BCDMCN + 1;
		if (index > 0x0F) {
			current_pgm = nullptr;
			pgm_counter = 0;
		}
		else {
			current_pgm = pgm_ptr[index];
			pgm_counter = pgm_entry[index];
			if (index & 8) {
				if (BCDMCN & 0xF8)
					pgm_counter |= 3;
				else if (BCDMCN & 0x04)
					pgm_counter |= 2;
				else if (BCDMCN & 0x02)
					pgm_counter |= 1;
			}
		}
		if (current_pgm != nullptr)
			macro_running = true;
	}

	void BCDCalc::ShiftLeft(uint8_t src, uint8_t dst, bool continuous) {
		if (!src) {
			for (int i = 11; i > 0; i--)
				BCDREG[dst][i] = (BCDREG[dst][i] << 4) | (BCDREG[dst][i - 1] >> 4);
			BCDREG[dst][0] = (BCDREG[dst][0] << 4) | (continuous ? (BCDREG[(dst + 3) & 3][11] >> 4) : 0);
		}
		else {
			int size = 1 << (src - 1);
			memmove(BCDREG[dst] + size, BCDREG[dst], 12 - size);
			if (continuous)
				memcpy(BCDREG[dst], BCDREG[(dst + 3) & 3] + 12 - size, size);
			else
				memset(BCDREG[dst], 0, size);
		}
	}

	void BCDCalc::ShiftRight(uint8_t src, uint8_t dst, bool continuous) {
		if (!src) {
			for (int i = 0; i < 11; i++)
				BCDREG[dst][i] = (BCDREG[dst][i] >> 4) | (BCDREG[dst][i + 1] << 4);
			BCDREG[dst][11] = (BCDREG[dst][11] >> 4) | (continuous ? (BCDREG[(dst + 1) & 3][0] << 4) : 0);
		}
		else {
			int size = 1 << (src - 1);
			memmove(BCDREG[dst], BCDREG[dst] + size, 12 - size);
			if (continuous)
				memcpy(BCDREG[dst] + 12 - size, BCDREG[(dst + 1) & 3], size);
			else
				memset(BCDREG[dst] + 12 - size, 0, size);
		}
	}
	Peripheral* CreateBcdCalc(Emulator& emu) {
		return new BCDCalc(emu);
	}
} // namespace casioemu
