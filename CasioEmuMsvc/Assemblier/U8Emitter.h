#pragma once
#include <vector>
#include <string>
#include <set>
#include <stack>
#include <stdexcept>
#include <stack>

namespace u8 {
	using Imm1 = unsigned char;
	using UImm4 = unsigned int;
	using UImm8 = unsigned long long;
	using Imm4 = int;
	using Imm8 = long long;
	// Opcodes datas

	using Opcode = uint16_t;
	// clang-format off
		//           function,                     hints, main mask, operand {size, mask, shift} x2
		// * Arithmetic Instructions

		enum OpcodeHint {
			H_IE = 0x0001, // * Extend Immediate flag for arithmetic instructions.
			H_ST = 0x0002, // * Store flag for load/store/coprocessor instructions.
			H_DW = 0x0004, // * Store a new DSR value.
			H_DS = 0x0008, // * Instruction is a DSR prefix.
			H_IA = 0x0010, // * Increment EA flag for load/store/coprocessor instructions.
			H_TI = 0x0020, // * Instruction takes an external long immediate value.
			H_WB = 0x0040  // * Register Writeback flag for a lot of instructions to make life easier.
		};
	constexpr auto lookup_table = {
		{"add"        , H_WB                     , 0x8001, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"add"        , H_WB                     , 0x1000, {{1, 0x000F,  8}, {0, 0x00FF,  0}}},
		{"add"      , H_WB                     , 0xF006, {{2, 0x000E,  8}, {2, 0x000E,  4}}},
		{"add"      , H_WB               | H_IE, 0xE080, {{2, 0x000E,  8}, {0, 0x007F,  0}}},
		{"addc"       , H_WB                     , 0x8006, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"addc"       , H_WB                     , 0x6000, {{1, 0x000F,  8}, {0, 0x00FF,  0}}},
		{"and"        , H_WB                     , 0x8002, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"and"        , H_WB                     , 0x2000, {{1, 0x000F,  8}, {0, 0x00FF,  0}}},
		{"sub"        ,                         0, 0x8007, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"sub"        ,                         0, 0x7000, {{1, 0x000F,  8}, {0, 0x00FF,  0}}},
		{"subc"       ,                         0, 0x8005, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"subc"       ,                         0, 0x5000, {{1, 0x000F,  8}, {0, 0x00FF,  0}}},
		{"mov"      , H_WB                     , 0xF005, {{2, 0x000E,  8}, {2, 0x000E,  4}}},
		{"mov"      , H_WB               | H_IE, 0xE000, {{2, 0x000E,  8}, {0, 0x007F,  0}}},
		{"mov"        , H_WB                     , 0x8000, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"mov"        , H_WB                     , 0x0000, {{1, 0x000F,  8}, {0, 0x00FF,  0}}},
		{"or"         , H_WB                     , 0x8003, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"or"         , H_WB                     , 0x3000, {{1, 0x000F,  8}, {0, 0x00FF,  0}}},
		{"xor"        , H_WB                     , 0x8004, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"xor"        , H_WB                     , 0x4000, {{1, 0x000F,  8}, {0, 0x00FF,  0}}},
		{"cmp"      ,                         0, 0xF007, {{2, 0x000E,  8}, {2, 0x000E,  4}}},
		{"sub"        , H_WB                     , 0x8008, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"subc"       , H_WB                     , 0x8009, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		// * Shift Instructions
		{"sll"        , H_WB                     , 0x800A, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{"sll"        , H_WB                     , 0x900A, {{1, 0x000F,  8}, {0, 0x0007,  4}}},
		{"sllc"       , H_WB                     , 0x800B, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{&CPU::OP_SLLC       , H_WB                     , 0x900B, {{1, 0x000F,  8}, {0, 0x0007,  4}}},
		{&CPU::OP_SRA        , H_WB                     , 0x800E, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{&CPU::OP_SRA        , H_WB                     , 0x900E, {{1, 0x000F,  8}, {0, 0x0007,  4}}},
		{&CPU::OP_SRL        , H_WB                     , 0x800C, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{&CPU::OP_SRL        , H_WB                     , 0x900C, {{1, 0x000F,  8}, {0, 0x0007,  4}}},
		{&CPU::OP_SRLC       , H_WB                     , 0x800D, {{1, 0x000F,  8}, {1, 0x000F,  4}}},
		{&CPU::OP_SRLC       , H_WB                     , 0x900D, {{1, 0x000F,  8}, {0, 0x0007,  4}}},
		// * Load/Store Instructions
		{&CPU::OP_LS_EA      , 2 << 8                   , 0x9032, {{0, 0x000E,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_EA      , 2 << 8 |      H_IA       , 0x9052, {{0, 0x000E,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_R       , 2 << 8                   , 0x9002, {{0, 0x000E,  8}, {2, 0x000E,  4}}},
		{&CPU::OP_LS_I_R     , 2 << 8 |      H_TI       , 0xA008, {{0, 0x000E,  8}, {2, 0x000E,  4}}},
		{&CPU::OP_LS_BP      , 2 << 8 |                0, 0xB000, {{0, 0x000E,  8}, {0, 0x003F,  0}}},
		{&CPU::OP_LS_FP      , 2 << 8 |                0, 0xB040, {{0, 0x000E,  8}, {0, 0x003F,  0}}},
		{&CPU::OP_LS_I       , 2 << 8 |      H_TI       , 0x9012, {{0, 0x000E,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_EA      , 1 << 8                   , 0x9030, {{0, 0x000F,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_EA      , 1 << 8 |      H_IA       , 0x9050, {{0, 0x000F,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_R       , 1 << 8                   , 0x9000, {{0, 0x000F,  8}, {2, 0x000E,  4}}},
		{&CPU::OP_LS_I_R     , 1 << 8 |      H_TI       , 0x9008, {{0, 0x000F,  8}, {2, 0x000E,  4}}},
		{&CPU::OP_LS_BP      , 1 << 8 |                0, 0xD000, {{0, 0x000F,  8}, {0, 0x003F,  0}}},
		{&CPU::OP_LS_FP      , 1 << 8 |                0, 0xD040, {{0, 0x000F,  8}, {0, 0x003F,  0}}},
		{&CPU::OP_LS_I       , 1 << 8 |      H_TI       , 0x9010, {{0, 0x000F,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_EA      , 4 << 8                   , 0x9034, {{0, 0x000C,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_EA      , 4 << 8 |      H_IA       , 0x9054, {{0, 0x000C,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_EA      , 8 << 8                   , 0x9036, {{0, 0x0008,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_EA      , 8 << 8 |      H_IA       , 0x9056, {{0, 0x0008,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_EA      , 2 << 8 |             H_ST, 0x9033, {{0, 0x000E,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_EA      , 2 << 8 |      H_IA | H_ST, 0x9053, {{0, 0x000E,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_R       , 2 << 8 |             H_ST, 0x9003, {{0, 0x000E,  8}, {2, 0x000E,  4}}},
		{&CPU::OP_LS_I_R     , 2 << 8 |      H_TI | H_ST, 0xA009, {{0, 0x000E,  8}, {2, 0x000E,  4}}},
		{&CPU::OP_LS_BP      , 2 << 8 |             H_ST, 0xB080, {{0, 0x000E,  8}, {0, 0x003F,  0}}},
		{&CPU::OP_LS_FP      , 2 << 8 |             H_ST, 0xB0C0, {{0, 0x000E,  8}, {0, 0x003F,  0}}},
		{&CPU::OP_LS_I       , 2 << 8 |      H_TI | H_ST, 0x9013, {{0, 0x000E,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_EA      , 1 << 8 |             H_ST, 0x9031, {{0, 0x000F,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_EA      , 1 << 8 |      H_IA | H_ST, 0x9051, {{0, 0x000F,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_R       , 1 << 8 |             H_ST, 0x9001, {{0, 0x000F,  8}, {2, 0x000E,  4}}},
		{&CPU::OP_LS_I_R     , 1 << 8 |      H_TI | H_ST, 0x9009, {{0, 0x000F,  8}, {2, 0x000E,  4}}},
		{&CPU::OP_LS_BP      , 1 << 8 |             H_ST, 0xD080, {{0, 0x000F,  8}, {0, 0x003F,  0}}},
		{&CPU::OP_LS_FP      , 1 << 8 |             H_ST, 0xD0C0, {{0, 0x000F,  8}, {0, 0x003F,  0}}},
		{&CPU::OP_LS_I       , 1 << 8 |      H_TI | H_ST, 0x9011, {{0, 0x000F,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_EA      , 4 << 8 |             H_ST, 0x9035, {{0, 0x000C,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_EA      , 4 << 8 |      H_IA | H_ST, 0x9055, {{0, 0x000C,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_EA      , 8 << 8 |             H_ST, 0x9037, {{0, 0x0008,  8}, {0,      0,  0}}},
		{&CPU::OP_LS_EA      , 8 << 8 |      H_IA | H_ST, 0x9057, {{0, 0x0008,  8}, {0,      0,  0}}},
		// * Control Register Access Instructions
		{&CPU::OP_ADDSP      ,                         0, 0xE100, {{0, 0x00FF,  0}, {0,      0,  0}}},
		{&CPU::OP_CTRL       ,                    1 << 8, 0xA00F, {{0,      0,  0}, {1, 0x000F,  4}}},
		{&CPU::OP_CTRL       ,                    2 << 8, 0xA00D, {{0,      0,  0}, {2, 0x000E,  8}}},
		{&CPU::OP_CTRL       ,                    3 << 8, 0xA00C, {{0,      0,  0}, {1, 0x000F,  4}}},
		{&CPU::OP_CTRL       , H_WB            |  4 << 8, 0xA005, {{2, 0x000E,  8}, {0,      0,  0}}},
		{&CPU::OP_CTRL       , H_WB            |  5 << 8, 0xA01A, {{2, 0x000E,  8}, {0,      0,  0}}},
		{&CPU::OP_CTRL       ,                    6 << 8, 0xA00B, {{0,      0,  0}, {1, 0x000F,  4}}},
		{&CPU::OP_CTRL       ,                    7 << 8, 0xE900, {{0,      0,  0}, {0, 0x00FF,  0}}},
		{&CPU::OP_CTRL       , H_WB            |  8 << 8, 0xA007, {{1, 0x000F,  8}, {0,      0,  0}}},
		{&CPU::OP_CTRL       , H_WB            |  9 << 8, 0xA004, {{1, 0x000F,  8}, {0,      0,  0}}},
		{&CPU::OP_CTRL       , H_WB            | 10 << 8, 0xA003, {{1, 0x000F,  8}, {0,      0,  0}}},
		{&CPU::OP_CTRL       ,                   11 << 8, 0xA10A, {{0,      0,  0}, {2, 0x000E,  4}}},
		// * PUSH/POP Instructions
		{&CPU::OP_PUSH       ,                         0, 0xF05E, {{0,      0,  0}, {2, 0x000E,  8}}},
		{&CPU::OP_PUSH       ,                         0, 0xF07E, {{0,      0,  0}, {8, 0x0008,  8}}},
		{&CPU::OP_PUSH       ,                         0, 0xF04E, {{0,      0,  0}, {1, 0x000F,  8}}},
		{&CPU::OP_PUSH       ,                         0, 0xF06E, {{0,      0,  0}, {4, 0x000C,  8}}},
		{&CPU::OP_PUSHL      ,                         0, 0xF0CE, {{0,      0,  0}, {0, 0x000F,  8}}},
		{&CPU::OP_POP        , H_WB                     , 0xF01E, {{2, 0x000E,  8}, {0,      0,  0}}},
		{&CPU::OP_POP        , H_WB                     , 0xF03E, {{8, 0x0008,  8}, {0,      0,  0}}},
		{&CPU::OP_POP        , H_WB                     , 0xF00E, {{1, 0x000F,  8}, {0,      0,  0}}},
		{&CPU::OP_POP        , H_WB                     , 0xF02E, {{4, 0x000C,  8}, {0,      0,  0}}},
		{&CPU::OP_POPL       ,                         0, 0xF08E, {{0, 0x000F,  8}, {0,      0,  0}}},
		// * Coprocessor Data Transfer Instructions
		{&CPU::OP_CR_R       ,                         0, 0xA00E, {{0, 0x000F,  8}, {0, 0x000F,  4}}},
		{&CPU::OP_CR_EA      ,      2 << 8 |           0, 0xF02D, {{0,      0,  0}, {0, 0x000E,  8}}},
		{&CPU::OP_CR_EA      ,      2 << 8 | H_IA       , 0xF03D, {{0,      0,  0}, {0, 0x000E,  8}}},
		{&CPU::OP_CR_EA      ,      1 << 8 |           0, 0xF00D, {{0,      0,  0}, {0, 0x000F,  8}}},
		{&CPU::OP_CR_EA      ,      1 << 8 | H_IA       , 0xF01D, {{0,      0,  0}, {0, 0x000F,  8}}},
		{&CPU::OP_CR_EA      ,      4 << 8 |           0, 0xF04D, {{0,      0,  0}, {0, 0x000C,  8}}},
		{&CPU::OP_CR_EA      ,      4 << 8 | H_IA       , 0xF05D, {{0,      0,  0}, {0, 0x000C,  8}}},
		{&CPU::OP_CR_EA      ,      8 << 8 |           0, 0xF06D, {{0,      0,  0}, {0, 0x0008,  8}}},
		{&CPU::OP_CR_EA      ,      8 << 8 | H_IA       , 0xF07D, {{0,      0,  0}, {0, 0x0008,  8}}},
		{&CPU::OP_CR_R       ,                      H_ST, 0xA006, {{0, 0x000F,  8}, {0, 0x000F,  4}}},
		{&CPU::OP_CR_EA      ,      2 << 8 |        H_ST, 0xF0AD, {{0, 0x000E,  8}, {0,      0,  0}}},
		{&CPU::OP_CR_EA      ,      2 << 8 | H_IA | H_ST, 0xF0BD, {{0, 0x000E,  8}, {0,      0,  0}}},
		{&CPU::OP_CR_EA      ,      1 << 8 |        H_ST, 0xF08D, {{0, 0x000F,  8}, {0,      0,  0}}},
		{&CPU::OP_CR_EA      ,      1 << 8 | H_IA | H_ST, 0xF09D, {{0, 0x000F,  8}, {0,      0,  0}}},
		{&CPU::OP_CR_EA      ,      4 << 8 |        H_ST, 0xF0CD, {{0, 0x000C,  8}, {0,      0,  0}}},
		{&CPU::OP_CR_EA      ,      4 << 8 | H_IA | H_ST, 0xF0DD, {{0, 0x000C,  8}, {0,      0,  0}}},
		{&CPU::OP_CR_EA      ,      8 << 8 |        H_ST, 0xF0ED, {{0, 0x0008,  8}, {0,      0,  0}}},
		{&CPU::OP_CR_EA      ,      8 << 8 | H_IA | H_ST, 0xF0FD, {{0, 0x0008,  8}, {0,      0,  0}}},
		// * EA Register Data Transfer Instructions
		{&CPU::OP_LEA        ,                         0, 0xF00A, {{0,      0,  0}, {2, 0x000E,  4}}},
		{&CPU::OP_LEA        ,        H_TI              , 0xF00B, {{0,      0,  0}, {2, 0x000E,  4}}},
		{&CPU::OP_LEA        ,        H_TI              , 0xF00C, {{0,      0,  0}, {0,      0,  0}}},
		// * ALU Instructions
		{&CPU::OP_DAA        , H_WB                     , 0x801F, {{1, 0x000F,  8}, {0,      0,  0}}},
		{&CPU::OP_DAS        , H_WB                     , 0x803F, {{1, 0x000F,  8}, {0,      0,  0}}},
		{&CPU::OP_NEG        , H_WB                     , 0x805F, {{1, 0x000F,  8}, {0,      0,  0}}},
		// * Bit Access Instructions
		{&CPU::OP_BITMOD     ,                         0, 0xA000, {{0, 0x000F,  8}, {0, 0x0007,  4}}},
		{&CPU::OP_BITMOD     ,        H_TI              , 0xA080, {{0,      0,  0}, {0, 0x0007,  4}}},
		{&CPU::OP_BITMOD     ,                         0, 0xA002, {{0, 0x000F,  8}, {0, 0x0007,  4}}},
		{&CPU::OP_BITMOD     ,        H_TI              , 0xA082, {{0,      0,  0}, {0, 0x0007,  4}}},
		{&CPU::OP_BITMOD     ,                         0, 0xA001, {{0, 0x000F,  8}, {0, 0x0007,  4}}},
		{&CPU::OP_BITMOD     ,        H_TI              , 0xA081, {{0,      0,  0}, {0, 0x0007,  4}}},
		// * PSW Access Instructions
		{&CPU::OP_PSW_OR     ,                         0, 0xED08, {{0,      0,  0}, {0,      0,  0}}},
		{&CPU::OP_PSW_AND    ,                         0, 0xEBF7, {{0,      0,  0}, {0,      0,  0}}},
		{&CPU::OP_PSW_OR     ,                         0, 0xED80, {{0,      0,  0}, {0,      0,  0}}},
		{&CPU::OP_PSW_AND    ,                         0, 0xEB7F, {{0,      0,  0}, {0,      0,  0}}},
		{&CPU::OP_CPLC       ,                         0, 0xFECF, {{0,      0,  0}, {0,      0,  0}}},
		// * Conditional Relative Branch Instructions
		{&CPU::OP_BC         ,                         0, 0xC000, {{0, 0x00FF,  0}, {0,      0,  0}}},
		{&CPU::OP_BC         ,                         0, 0xC100, {{0, 0x00FF,  0}, {0,      0,  0}}},
		{&CPU::OP_BC         ,                         0, 0xC200, {{0, 0x00FF,  0}, {0,      0,  0}}},
		{&CPU::OP_BC         ,                         0, 0xC300, {{0, 0x00FF,  0}, {0,      0,  0}}},
		{&CPU::OP_BC         ,                         0, 0xC400, {{0, 0x00FF,  0}, {0,      0,  0}}},
		{&CPU::OP_BC         ,                         0, 0xC500, {{0, 0x00FF,  0}, {0,      0,  0}}},
		{&CPU::OP_BC         ,                         0, 0xC600, {{0, 0x00FF,  0}, {0,      0,  0}}},
		{&CPU::OP_BC         ,                         0, 0xC700, {{0, 0x00FF,  0}, {0,      0,  0}}},
		{&CPU::OP_BC         ,                         0, 0xC800, {{0, 0x00FF,  0}, {0,      0,  0}}},
		{&CPU::OP_BC         ,                         0, 0xC900, {{0, 0x00FF,  0}, {0,      0,  0}}},
		{&CPU::OP_BC         ,                         0, 0xCA00, {{0, 0x00FF,  0}, {0,      0,  0}}},
		{&CPU::OP_BC         ,                         0, 0xCB00, {{0, 0x00FF,  0}, {0,      0,  0}}},
		{&CPU::OP_BC         ,                         0, 0xCC00, {{0, 0x00FF,  0}, {0,      0,  0}}},
		{&CPU::OP_BC         ,                         0, 0xCD00, {{0, 0x00FF,  0}, {0,      0,  0}}},
		{&CPU::OP_BC         ,                         0, 0xCE00, {{0, 0x00FF,  0}, {0,      0,  0}}},
		// * Sign Extension Instruction
		{&CPU::OP_EXTBW      ,                         0, 0x810F, {{0,      0,  0}, {0,      0,  0}}},
		{&CPU::OP_EXTBW      ,                         0, 0x832F, {{0,      0,  0}, {0,      0,  0}}},
		{&CPU::OP_EXTBW      ,                         0, 0x854F, {{0,      0,  0}, {0,      0,  0}}},
		{&CPU::OP_EXTBW      ,                         0, 0x876F, {{0,      0,  0}, {0,      0,  0}}},
		{&CPU::OP_EXTBW      ,                         0, 0x898F, {{0,      0,  0}, {0,      0,  0}}},
		{&CPU::OP_EXTBW      ,                         0, 0x8BAF, {{0,      0,  0}, {0,      0,  0}}},
		{&CPU::OP_EXTBW      ,                         0, 0x8DCF, {{0,      0,  0}, {0,      0,  0}}},
		{&CPU::OP_EXTBW      ,                         0, 0x8FEF, {{0,      0,  0}, {0,      0,  0}}},
		// * Software Interrupt Instructions
		{&CPU::OP_SWI        ,                         0, 0xE500, {{0, 0x00FF,  0}, {0,      0,  0}}},
		{&CPU::OP_BRK        ,                         0, 0xFFFF, {{0,      0,  0}, {0,      0,  0}}},
		// * Branch Instructions
		{&CPU::OP_B          ,        H_TI              , 0xF000, {{0,      0,  0}, {0, 0x000F,  8}}},
		{&CPU::OP_B          ,                         0, 0xF002, {{0,      0,  0}, {2, 0x000E,  4}}},
		{&CPU::OP_BL         ,        H_TI              , 0xF001, {{0,      0,  0}, {0, 0x000F,  8}}},
		{&CPU::OP_BL         ,                         0, 0xF003, {{0,      0,  0}, {2, 0x000E,  4}}},
		// * Multiplication and Division Instructions
		{&CPU::OP_MUL        , H_WB                     , 0xF004, {{2, 0x000E,  8}, {1, 0x000F,  4}}},
		{&CPU::OP_DIV        , H_WB                     , 0xF009, {{2, 0x000E,  8}, {1, 0x000F,  4}}},
		// * Miscellaneous Instructions
		{&CPU::OP_INC_EA     ,                         0, 0xFE2F, {{0,      0,  0}, {0,      0,  0}}},
		{&CPU::OP_DEC_EA     ,                         0, 0xFE3F, {{0,      0,  0}, {0,      0,  0}}},
		{&CPU::OP_RT         ,                         0, 0xFE1F, {{0,      0,  0}, {0,      0,  0}}},
		{&CPU::OP_RTI        ,                         0, 0xFE0F, {{0,      0,  0}, {0,      0,  0}}},
		{&CPU::OP_RTI        ,                         0, 0xFE7F, {{0,      0,  0}, {0,      0,  0}}}, // TODO: verify this
		{&CPU::OP_NOP        ,                         0, 0xFE8F, {{0,      0,  0}, {0,      0,  0}}},
		{&CPU::OP_DSR        ,               H_DS       , 0xFE9F, {{0,      0,  0}, {0,      0,  0}}},
		{&CPU::OP_DSR        ,               H_DS | H_DW, 0xE300, {{0, 0x00FF,  0}, {0,      0,  0}}},
		{&CPU::OP_DSR        ,               H_DS | H_DW, 0x900F, {{1, 0x000F,  4}, {0,      0,  0}}}
		};
	// clang-format on

	const char* GetOpCodeAbbr(Opcode op) {
		switch (op) {

		}
	}
	class Emitter {
	public:
		template <class Operand>
		class Operation {
			Emitter* em;
			size_t ptr;

		public:
			Operation(Emitter* em, size_t ptr) : em(em), ptr(ptr){};
			auto& GetOpcode() {
				return (Opcode&)em->Bytes[ptr];
			}
			auto& GetOperand() {
				return (Operand&)em->Bytes[ptr + 1];
			}
		};
		template <>
		class Operation<void> {
			Emitter* em;
			size_t ptr;

		public:
			Operation(Emitter* em, size_t ptr) : em(em), ptr(ptr){};
			auto& GetOpcode() {
				return (Opcode&)em->Bytes[ptr];
			}
		};
		template <class Operand>
		class OperationWithString {
			Emitter* em;
			size_t ptr;

		public:
			OperationWithString(Emitter* em, size_t ptr) : em(em), ptr(ptr){};
			auto& GetOpcode() {
				return (Opcode&)em->Bytes[ptr];
			}
			auto& GetOperand() {
				return (std::string&)em->Strings[(*(Operand*)&em->Bytes[ptr + 1])];
			}
		};
		std::vector<char> Bytes;
		std::vector<std::string> Strings;
		auto EmitOp(Opcode opc) {
			Operation<void> op{ this, Bytes.size() };
			Bytes.push_back(opc);
			return op;
		}
		auto EmitOpI1(Opcode opc, unsigned char imm1) {
			Operation<unsigned char> op{ this, Bytes.size() };
			Bytes.push_back(opc);
			Emit(imm1);
			return op;
		}
		auto EmitOp(Opcode opc, int imm4) {
			Operation<int> op{ this, Bytes.size() };
			Bytes.push_back(opc);
			Emit(imm4);
			return op;
		}
		auto EmitOp(Opcode opc, long long imm8) {
			Operation<long long> op{ this, Bytes.size() };
			Bytes.push_back(opc);
			Emit(imm8);
			return op;
		}
		auto EmitOp(Opcode opc, float imm4) {
			Operation<float> op{ this, Bytes.size() };
			Bytes.push_back(opc);
			Emit(imm4);
			return op;
		}
		auto EmitOp(Opcode opc, double imm8) {
			Operation<double> op{ this, Bytes.size() };
			Bytes.push_back(opc);
			Emit(imm8);
			return op;
		}
		auto EmitOp(Opcode opc, const std::string& str) {
			OperationWithString<unsigned int> ows{ this, Bytes.size() };
			Bytes.push_back(opc);

			int i = 0;
			bool found = false;
			for (auto& str1 : Strings) {
				if (str1 == str) {
					found = true;
					break;
				}
				i++;
			}
			if (!found) {
				Strings.push_back(str);
				Emit((int)Strings.size() - 1);
				return ows;
			}
			Emit(i);

			return ows;
		}
		void Emit(auto imm) {
			Bytes.insert(Bytes.end(), (char*)&imm, (char*)(&imm + 1));
		}
		void Modify(auto where, auto imm) {
			memcpy(&*where, &imm, sizeof(imm));
		}
	};
}