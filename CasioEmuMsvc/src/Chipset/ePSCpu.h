#pragma once
#include "Chipset.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <cstdarg>
using byte = uint8_t;
using uint = uint32_t;
using ushort = uint16_t;
using undefined = uint8_t;
using undefined2 = ushort;
using undefined4 = uint;

namespace casioemu {
	//// Custom byteswap function for uint16_t
	// inline uint16_t byteswap_ushort(uint16_t x) {
	//	return ((x & 0xFF00) >> 8) | ((x & 0x00FF) << 8);
	// }

	class ePSCPU {
	public:
		uint8_t wbk[0x30]{};
		// idk but this should work for most compilers
		// start = 0x0041e0a0
		union {
			struct{
				uint8_t INDF0;
				uint8_t FSR;
				uint8_t BSR;

				uint8_t INDF1;
				uint8_t FSR1;
				uint8_t BSR1;

				uint8_t STKPTR;

				uint8_t PCL;
				uint8_t PCM;
				uint8_t PCH;

				uint8_t ACC;

				uint8_t TABPTRL;
				uint8_t TABPTRM;
				uint8_t TABPTRH;

				uint8_t LCDDATA;

				uint8_t STATUS;

				uint8_t INDF2;
				uint8_t FSR2;
				uint8_t BSR2;

				uint8_t _padding[13];

				uint8_t CPUCON;
				uint8_t POST_ID;

				uint8_t LCDARL;
				uint8_t LCDARH;

				uint8_t INTSTA;

				uint8_t TR0CON;
				uint8_t TRL0L;
				uint8_t TRL0H;
				uint8_t T0CL;
				uint8_t T0CH;
				uint8_t TR1CON;
				uint8_t TRL1;
				uint8_t TR2WCON;
				uint8_t TRL2;
				uint8_t LCDCON;
				uint8_t _padding2;

				uint8_t STBCON;
				uint8_t PORTA;
				uint8_t PACON;
				uint8_t DCRA;
				uint8_t PAWAKE;
				uint8_t PAINTEN;
				uint8_t PAINTSTA;
				uint8_t PORTB;
				uint8_t PBCON;
				uint8_t DCRB;
				uint8_t PORTC;
				uint8_t PCCON;
				uint8_t DCRC;
				uint8_t PORTD;
				uint8_t PORTE;
				uint8_t DCRDE;
				// 0x3f
				uint8_t gpr_40;
				uint8_t gpr_41;
				uint8_t gpr_42;
				uint8_t gpr_43;
			};
			uint8_t regs[0x80]{};
			uint8_t EmuMem;
		};

		uint8_t ram[64 * 0x80]{};

		uint8_t Rom[0x40000]{};

		union {
			uint8_t StackRam;
			uint8_t stackram[0x20 * 2]{};
			uint16_t stack[0x20];
		};

		union {
			uint8_t vram[0x2000]{};
			uint8_t VRam;
		};

		byte* reg(byte param_1);
		void post_pid(char param_1);

		void auto_carry(char param_1);

		void auto_borrow(char param_1);

		void debug_printf(const wchar_t* format, ...) {
			va_list args;
			va_start(args, format);
			vwprintf(format, args);
			va_end(args);
			wprintf(L"\n");
		}
		void debug_printf(const char* format, ...) {
			va_list args;
			va_start(args, format);
			vprintf(format, args);
			va_end(args);
			printf("\n");
		}

		casioemu::MMU& mmu;
		ePSCPU(casioemu::MMU& mmu) : mmu(mmu) {
		}

		// in bytes
		uint32_t PC() {
			return (PCL | (PCM << 8) | (PCH << 16)) << 1;
		}

		void Next();

		void Reset();

		enum {
			// 低速振荡器
			ST_SLOW,
			// 高速振荡器
			ST_FAST,
			// 停止,低速振荡器继续工作
			ST_STOP,
			// 全部停止
			ST_SLEEP,
		} run_stat;
		// ???
		uint8_t t0tick;
		uint16_t t1tick;
		uint8_t t1tick2;
		uint8_t t1_pre;
		uint8_t DAT_0041e192;
		uint64_t CycleCounter;
		uint8_t RepeatCount;
		uint8_t DAT_004202b5;
		uint8_t DAT_004202b7;
		uint8_t InstFlags;

		// 这个函数应该是用来初始化的（?)
		// void FUN_004083f0(void)
		//{
		//	_memset(&StackRam, 0x0, 0x2258);
		//	_DAT_0042012c = 0xffffffff;
		//	_DAT_00420130 = 0xffffffff;
		//	EmuMem = 0x1;
		//	DAT_0041e0a3 = 0x1;
		//	INDF2 = 0x1;
		//	FSR1 = 0x80;
		//	STATUS = 0xc0;
		//	FSR2 = 0x80;
		//	POST_ID = 0x70;
		//	PAWAKE = 0xff;
		//	PORTC = 0xff;
		//	DAT_0041e0dc = 0xf;
		//	DAT_0041e0df = 0x33;
		//	return;
		//}

		// Port&Timer操作
		void InvalidateTimerSetting(uint32_t unk);
		void UpdateTimerSetting(uint32_t unk);
		void InvalidatePORTA();

		std::function<void()> portacalc;

		void RaisePAINT(int source);
		void RaiseTMINT(int source);

		void Timer0Next();

		using OP_Handler = void (ePSCPU::*)(byte* param_1);

		// 用来修复那一坨shi
		static constexpr uint32_t g_stack_cookie = 0x11451419;
		void Sleep(auto){};

		void OP_NOP(byte* param_1);

		void OP_WDTC(byte* param_1);

		void OP_SLEP(byte* param_1);

		void OP_BANK(byte* param_1);

		void OP_S0CALL(byte* param_1);

		void OP_SCALL(byte* param_1);

		void OP_LCALL(byte* param_1);

		void OP_RET(byte* param_1);

		void OP_RETI(byte* param_1);

		void OP_SJMP(byte* param_1);

		void OP_LJMP(byte* param_1);

		void OP_JGE_A_k(byte* param_1);

		void OP_JLE_A_k(byte* param_1);

		void OP_JE_A_k(byte* param_1);

		void OP_MOV_k(byte* param_1);

		void OP_TBPTRL(byte* param_1);

		void OP_TBPTRM(byte* param_1);

		void OP_TBPTRH(byte* param_1);

		void OP_OR_k(byte* param_1);

		void OP_AND_k(byte* param_1);

		void OP_XOR_k(byte* param_1);

		void OP_ADD_k(byte* param_1);

		void OP_ADC_k(byte* param_1);

		void OP_SUB_k(byte* param_1);

		void OP_SUBB_k(byte* param_1);

		void OP_UD(byte* param_1);

		void OP_RPT(byte* param_1);

		void OP_TEST(byte* param_1);

		void OP_JDNZ_A(byte* param_1);

		void OP_JDNZ_r(byte* param_1);

		void OP_JGE_A_r(byte* param_1);

		void OP_JLE_A_r(byte* param_1);

		void OP_JE_A_r(byte* param_1);

		void OP_JBC(byte* param_1);

		void OP_JBS(byte* param_1);

		void OP_MOV_A(byte* param_1);

		void OP_MOV_r(byte* param_1);

		void OP_MOVRP(byte* param_1);

		void OP_MOVPR(byte* param_1);

		void OP_CLR(byte* param_1);

		void OP_TBRD(byte* param_1);

		void OP_TBRD_A(byte* param_1);

		void OP_OR_A(byte* param_1);

		void OP_OR_R(byte* param_1);

		void OP_AND_A(byte* param_1);

		void OP_AND_r(byte* param_1);

		void OP_XOR_A(byte* param_1);

		void OP_XOR_r(byte* param_1);

		void OP_COMA(byte* param_1);

		void OP_COM(byte* param_1);

		void OP_INCA(byte* param_1);

		void OP_INC(byte* param_1);

		void OP_ADD_A(byte* param_1);

		void OP_ADD_r(byte* param_1);

		void OP_ADC_A(byte* param_1);

		void OP_ADC_r(byte* param_1);

		void OP_DECA(byte* param_1);

		void OP_DEC(byte* param_1);

		void OP_SUB_A(byte* param_1);

		void OP_SUB_r(byte* param_1);

		void OP_SUBB_A(byte* param_1);

		void OP_SUBB_r(byte* param_1);

		void OP_ADDDC_A(byte* param_1);

		void OP_ADDDC_r(byte* param_1);

		void OP_SUBDB_A(byte* param_1);

		void OP_SUBDB_r(byte* param_1);

		void OP_RRCA(byte* param_1);

		void OP_RRC(byte* param_1);

		void OP_RLCA(byte* param_1);

		void OP_RLC(byte* param_1);

		void OP_SHRA(byte* param_1);

		void OP_SHLA(byte* param_1);

		void OP_EX_r(byte* param_1);

		void OP_BC(byte* param_1);

		void OP_BS(byte* param_1);

		void OP_BTG(byte* param_1);

		void OP_EXL_r(byte* param_1);

		void OP_EXH_r(byte* param_1);

		void OP_MOVL_r(byte* param_1);

		void OP_MOVH_r(byte* param_1);

		void OP_MOVL_A(byte* param_1);

		void OP_MOVH_A(byte* param_1);

		void OP_SFR4(byte* param_1);

		void OP_SFL4(byte* param_1);

		void OP_SWAP(byte* param_1);

		void OP_SWAPA(byte* param_1);

	}; // namespace casioemu
}; // namespace casioemu