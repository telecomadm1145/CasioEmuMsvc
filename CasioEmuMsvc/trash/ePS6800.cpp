#if 0
#include <cstdint>
#include <vector>
namespace casioemu {
	class ePS6800CPU {
		struct OpcodeSource {
			void (ePS6800CPU::*handler_function)();
			/**
			 * I know this should be an OpcodeHint, but the damn C++ initializer lists
			 * convert literally everything to int if it's more than a single enum
			 * value. Even binary OR'd values and 0. Pain in the ass.
			 */
			size_t hint;
			uint16_t opcode;
			struct OperandMask {
				/**
				 * `register_size` determines whether an operand is a register
				 * or an immediate. If it's 0, the operand is an immediate. Otherwise
				 * the operand is a register of size `register_size`.
				 */
				size_t register_size;
				uint16_t mask, shift;
			} operands[2];
		};
		enum InstHints {
			// REPEAT prefix
			H_RPT,
			// long int
			H_TI,
		};
		std::vector<OpcodeSource> opcode_sources{
			// function,                     hints,	 main mask, operand {size, mask, shift} x2
			// System Control
			{OP_NOP, 0, 0x0000, {}},
			{OP_WDTC, 0, 0x0001, {}},
			{OP_NOP, H_RPT, 0x2700, {{0, 0xff, 0}}},
			{OP_SLEP, 0, 0x0003, {}},
			{OP_BANK, 0, 0x4300, {{0, 0xff, 0}}},
			// Subroutine
			{OP_S0CALL, 0, 0x3000, {{0, 0x0fff, 0}}},
			{OP_SCALL, 0, 0xe000, {{0, 0x1fff, 0}}},
			{OP_LCALL, H_TI, 0x0030},
			{OP_RET, 0, 0x2bfe, {}},
			{OP_RETI, 0, 0x2bff, {}},
			// Compare
			{OP_TEST, 0, 0x2500, {{0, 0xff, 0}}},
			// Compare and Jump
			// Bit Compare and Jump
			// Data Transfer
			{OP_MOVAR, 0, 0x2000, {{0, 0xff, 0}}},
			{OP_MOVRA, 0, 0x2100, {{0, 0xff, 0}}},
			{OP_MOVRP, 0, 0x8000, {}}, // TODO
			{OP_MOVPR, 0, 0xa000, {}}, // TODO
			{OP_MOVAK, 0, 0x4e00, {{0, 0xff, 0}}},
			{OP_CLR, 0, 0x2400, {{0, 0xff, 0}}},
			// ROM Window

		};
		OpcodeSource** opcode_dispatch;
		void SetupOpcodeDispatch() {
			uint16_t* permutation_buffer = new uint16_t[0x10000];
			for (size_t ix = 0; ix != opcode_sources.size(); ++ix) {
				OpcodeSource& handler_stub = opcode_sources[ix];

				uint16_t varying_bits = 0;
				for (size_t ox = 0; ox != 2; ++ox)
					varying_bits |= handler_stub.operands[ox].mask << handler_stub.operands[ox].shift;

				size_t permutation_count = 1;
				permutation_buffer[0] = handler_stub.opcode;
				for (uint16_t checkbit = 0x8000; checkbit; checkbit >>= 1) {
					if (varying_bits & checkbit) {
						for (size_t px = 0; px != permutation_count; ++px)
							permutation_buffer[px + permutation_count] = permutation_buffer[px] | checkbit;
						permutation_count <<= 1;
					}
				}

				for (size_t px = 0; px != permutation_count; ++px) {
					if (opcode_dispatch[permutation_buffer[px]])
						continue;
					opcode_dispatch[permutation_buffer[px]] = &handler_stub;
				}
			}
			delete[] permutation_buffer;
		}
		uint16_t stack[32]{};

		struct CPUSR {
		public:
			uint8_t INDF0;
			uint8_t FSR0;
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
			struct {
				union {
					uint8_t data;
					struct {
						bool C : 1;	  // Carry Flag.
						bool DC : 1;  // Auxiliary Carry Flag.
						bool Z : 1;	  // Zero Flag.
						bool OV : 1;  // Overflow Flag.
						bool SLE : 1; // Less Than Or Equal Flag.
						bool SGE : 1; // Greater Than Or Equal Flag.
						bool PD : 1;  // Wdt Power Down Detect Bit.
						bool TO : 1;  // Wdt Time Out Detect Bit.
					};
				};
			} STATUS;
			uint8_t INDF2;
			uint8_t FSR2;
			uint8_t BSR2;
			uint8_t registers[12];
			struct {
				union {
					uint8_t data;
					struct {
						bool MS0 : 1;	// Cpu Operation Mode Control (0: Slow, 1: Fast).
						bool MS1 : 1;	// Cpu Operation Mode Control (0: Sleep, 1: Idle).
						bool GLINT : 1; // Enable Global Interrupt.
						bool : 4;		// Unused
						bool WBK : 1;	// Select Working RAM
					};
				};
			} CPUCON;
		} regs;

		uint8_t impl_flags_changed, impl_flags_out, impl_flags_in;
		uint8_t impl_shift_buffer;
		uint16_t impl_opcode, impl_long_imm;
		struct {
			uint64_t value;
			size_t register_index, register_size;
		} impl_operands[2];
		size_t impl_hint;
		uint16_t impl_csr_mask;

		size_t fetch_addition;

		uint8_t& DataRef(uint8_t index) {
		}

		void OP_NOP() {}

		void OP_WDTC() {
			// reg_wdt = 0;
			regs.STATUS.TO = 1;
			regs.STATUS.PD = 1;
		}
		void OP_SLEP() {
			if (regs.CPUCON.MS1) {
				// IDLE mode
			}
			else {
				// Sleep mode
			}
		}
		void OP_BANK() {
			regs.BSR = impl_operands[0].value;
		}
		void OP_S0CALL() {
			stack[regs.STKPTR--] = *(uint16_t*)&regs.PCL;
			regs.STKPTR &= 0x1f;
			*(uint16_t*)&regs.PCL = impl_operands[0].value & 0xfff;
		}
		void OP_SCALL() {
			stack[regs.STKPTR--] = *(uint16_t*)&regs.PCL;
			regs.STKPTR &= 0x1f;
			*(uint16_t*)&regs.PCL = (*(uint16_t*)&regs.PCL & ~0x1fff) | (impl_operands[0].value & 0x1fff);
		}
		// takes a imm16
		void OP_LCALL() {
			stack[regs.STKPTR--] = *(uint16_t*)&regs.PCL;
			regs.STKPTR &= 0x1f;
			*(uint16_t*)&regs.PCL = impl_long_imm;
		}
		void OP_RET() {
			*(uint16_t*)&regs.PCL = stack[regs.STKPTR--];
			regs.STKPTR &= 0x1f;
		}
		void OP_RETI() {
			*(uint16_t*)&regs.PCL = stack[regs.STKPTR--];
			regs.STKPTR &= 0x1f;
			regs.CPUCON.GLINT = true;
		}
		void OP_TEST() {
			regs.STATUS.Z = !DataRef(impl_operands[0].value);
		}
		void OP_SJMP() {
			*(uint16_t*)&regs.PCL = (*(uint16_t*)&regs.PCL & ~0x1fff) | (impl_operands[0].value & 0x1fff);
		}
		// imm16
		void OP_LJMP() {
			*(uint16_t*)&regs.PCL = impl_long_imm;
		}
		void OP_MOVAR() {
			regs.STATUS.Z = !(regs.ACC = DataRef(impl_operands[0].value));
		}
		void OP_MOVRA() {
			DataRef(impl_operands[0].value) = regs.ACC;
		}
		void OP_MOVPR() {
			// mov [r] to special function register
		}
		void OP_MOVRP() {
			// mov special function register to [r]
		}
		void OP_MOVAK() {
			regs.ACC = impl_operands[0].value;
		}
		void OP_CLR() {
			DataRef(impl_operands[0].value) = 0;
			regs.STATUS.Z = true;
		}
		void OP_COM() {
			regs.STATUS.Z = !(DataRef(impl_operands[0].value) = ~DataRef(impl_operands[0].value));
		}
		void OP_COMA() {
			regs.STATUS.Z = !(regs.ACC = ~DataRef(impl_operands[0].value));
		}
	};
} // namespace casioemu
#endif