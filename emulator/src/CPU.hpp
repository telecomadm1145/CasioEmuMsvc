#pragma once
#include "Config.hpp"

#include <cstdint>

namespace casioemu
{
	class Emulator;

	class CPU
	{
		Emulator &emulator;

	public:
		CPU(Emulator &emulator);
		~CPU();

		/**
		 * See 1.2.1 in the nX-U8 manual.
		 */
		uint8_t reg_r[16], reg_cr[16];
		uint16_t reg_pc, reg_elr[4], &reg_lr;
		uint16_t reg_csr, reg_ecsr[4], &reg_lcsr;
		uint16_t reg_epsw[4], &reg_psw;
		uint16_t reg_sp, reg_ea;
		uint8_t reg_dsr;

		uint8_t impl_last_dsr;
		uint16_t impl_long_imm;
		struct {
			uint64_t value;
			size_t register_index, register_size;
		} impl_operands[2];
		size_t impl_hint;

		/**
		 * See 1.2.2.1 in the nX-U8 manual.
		 */
		enum StatusFlag
		{
			PSW_C = 0x80,
			PSW_Z = 0x40,
			PSW_S = 0x20,
			PSW_OV = 0x10,
			PSW_MIE = 0x8,
			PSW_HC = 0x4,
			PSW_ELEVEL = 0x3
		};

		/**
		 * See 1.3.6 in the nX-U8 manual.
		 */
		enum MemoryModel
		{
			MM_SMALL,
			MM_LARGE
		} memory_model;

		void Next();

	private:
		uint16_t Fetch();

		enum OpcodeHint
		{
			H_IE = 0x0001, // * Extend Immediate flag for arithmethic instructions.
			H_ST = 0x0001, // * Store flag for load/store/coprocessor instructions.
			H_DW = 0x0001, // * Store a new DSR value.
			H_DS = 0x0002, // * Instruction is a DSR prefix.
			H_IA = 0x0002, // * Increment EA flag for load/store/coprocessor instructions.
			H_TI = 0x0004  // * Instruction takes an external long immediate value.
		};

		struct OpcodeSource
		{
			void (CPU::*handler_function)();
			/**
			 * I know this should be an OpcodeHint, but the damn C++ initializer lists
			 * convert literally everything to int if it's more than a single enum
			 * value. Even binary OR'd values and 0. Pain in the ass.
			 */
			uint16_t hint;
			uint16_t opcode;
			struct OperandMask
			{
				/**
				 * `register_size` determines whether an operand is a register
				 * or an immediate. If it's 0, the operand is an immediate. Otherwise
				 * the operand is a register of size `register_size`.
				 */
				size_t register_size;
				uint16_t mask, shift;
			} operands[2];
		};
		static OpcodeSource opcode_sources[];
		OpcodeSource **opcode_dispatch;

		// * Arithmetic Instructions
		void OP_ADD();
		void OP_ADD16();
		void OP_ADDC();
		void OP_AND();
		void OP_CMP();
		void OP_CMPC();
		void OP_MOV16();
		void OP_MOV();
		void OP_OR();
		void OP_XOR();
		void OP_CMP16();
		void OP_SUB();
		void OP_SUBC();
		// * Shift Instructions
		void OP_SLL();
		void OP_SLLC();
		void OP_SRA();
		void OP_SRL();
		void OP_SRLC();
		// * Load/Store Instructions
		void OP_LS_EA();
		void OP_LS_R();
		void OP_LS_I_R();
		void OP_LS_I_BP();
		void OP_LS_I_FP();
		void OP_LS_I();
		// * Control Register Access Instructions
		void OP_ADDSP();
		void OP_CTRL();
		// * PUSH/POP Instructions
		void OP_PUSH();
		void OP_PUSH2();
		void OP_POP();
		void OP_POP2();
		// * Coprocessor Data Transfer Instructions
		void OP_CR_R();
		void OP_CR_EA();
		// * EA Register Data Transfer Instructions
		void OP_LEA_R();
		void OP_LEA_I_R();
		void OP_LEA_I();
		// * ALU Instructions
		void OP_DAA();
		void OP_DAS();
		void OP_NEG();
		// * Bit Access Instructions
		void OP_BITMOD();
		// * PSW Access Instructions
		void OP_PSW_OR();
		void OP_PSW_AND();
		void OP_CPLC();
		// * Conditional Relative Branch Instructions
		void OP_BC();
		// * Sign Extension Instruction
		void OP_EXTBW();
		// * Software Interrupt Instructions
		void OP_SWI();
		void OP_BRK();
		// * Branch Instructions
		void OP_B();
		void OP_BL();
		// * Multiplication and Division Instructions
		void OP_MUL();
		void OP_DIV();
		// * Miscellaneous Instructions
		void OP_INC_EA();
		void OP_DEC_EA();
		void OP_RT();
		void OP_RTI();
		void OP_NOP();
		void OP_DSR();
	};
}

