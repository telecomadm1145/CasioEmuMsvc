#include "CPU.hpp"

#include "Chipset.hpp"
#include "Emulator.hpp"
#include "Gui/Hooks.h"
#include "Ui.hpp"
#include "MMU.hpp"

#pragma warning(disable : 4244)

namespace casioemu {
	// * Control Register Access Instructions
	void CPU::OP_ADDSP() {
		impl_operands[0].value |= (impl_operands[0].value & 0x80) ? 0xFF00 : 0;
		reg_sp += impl_operands[0].value;
		if (cpu_model == CM_NX_U16) {
			reg_sp &= 0xfffe;
		}
	}

	void CPU::OP_CTRL() {
		switch (impl_hint >> 8) {
		case 1:
			reg_ecsr[reg_psw & PSW_ELEVEL] = impl_operands[1].value;
			break;
		case 2:
			reg_elr[reg_psw & PSW_ELEVEL] = impl_operands[1].value;
			break;
		case 3:
			if (reg_psw & PSW_ELEVEL)
				reg_epsw[reg_psw & PSW_ELEVEL] = impl_operands[1].value;
			break;
		case 4:
			impl_operands[0].value = reg_elr[reg_psw & PSW_ELEVEL];
			break;
		case 5:
			impl_operands[0].value = reg_sp;
			break;
		case 6:
		case 7:
			reg_psw = impl_operands[1].value;
			break;
		case 8:
			impl_operands[0].value = reg_ecsr[reg_psw & PSW_ELEVEL];
			break;
		case 9:
			if (reg_psw & PSW_ELEVEL) {
				impl_operands[0].value = reg_epsw[reg_psw & PSW_ELEVEL];
			}
			else {
				if (cpu_model == CM_NX_U16) {
					impl_operands[0].value = 0xFF;
				}
			}
			break;
		case 10:
			impl_operands[0].value = reg_psw;
			break;
		case 11:
			reg_sp = impl_operands[1].value;
			if (cpu_model == CM_NX_U16) {
				reg_sp &= 0xfffe;
			}
			break;
		}
	}

	// * EA Register Data Transfer Instructions
	void CPU::OP_LEA() {
		reg_ea = 0;
		if (impl_operands[1].register_size)
			reg_ea += impl_operands[1].value;
		if (impl_hint & H_TI)
			reg_ea += impl_long_imm;
	}

	// * Coprocessor Data Transfer Instructions
	void CPU::OP_CR_R() {
		size_t op0_index = (impl_opcode >> 8) & 0x000F;
		size_t op1_index = (impl_opcode >> 4) & 0x000F;
		if (impl_hint & H_ST)
			reg_r[op0_index] = reg_cr[op1_index];
		else
			reg_cr[op0_index] = reg_r[op1_index];
	}

	void CPU::OP_CR_EA() {
		size_t op0_index = (impl_opcode >> 8) & 0x000F;
		size_t register_size = impl_hint >> 8;

		if (impl_hint & H_ST)
			for (size_t ix = register_size - 1; ix != (size_t)-1; --ix)
				emulator.chipset.mmu.WriteData((((size_t)reg_dsr) << 16) | (uint16_t)(reg_ea + ix), reg_cr[op0_index + ix]);
		else
			for (size_t ix = 0; ix != register_size; ++ix)
				reg_cr[op0_index + ix] = emulator.chipset.mmu.ReadData((((size_t)reg_dsr) << 16) | (uint16_t)(reg_ea + ix));

		if (impl_hint & H_IA)
			BumpEA(register_size);
	}

	void CPU::BumpEA(size_t value_size) {
		reg_ea += value_size;
		if (value_size != 1)
			reg_ea &= ~1;
	}

	// * PSW Access Instructions
	void CPU::OP_PSW_OR() {
		reg_psw |= (impl_opcode & 0xFF);
		if (impl_opcode & 0x0008)
			emulator.chipset.isMIBlocked = true;
	}

	void CPU::OP_PSW_AND() {
		reg_psw &= (impl_opcode & 0xFF);
	}

	void CPU::OP_CPLC() {
		reg_psw ^= PSW_C;
	}

	// * Conditional Relative Branch Instructions
	void CPU::OP_BC() {
		bool c = impl_flags_in & PSW_C;
		bool z = impl_flags_in & PSW_Z;
		bool s = impl_flags_in & PSW_S;
		bool ov = impl_flags_in & PSW_OV;
		bool le = z || c;
		bool lts = ov ^ s;
		bool les = lts || z;

		bool branch;
		switch ((impl_opcode >> 8) & 0x000F) {
		case 0:
			branch = !c;
			break;
		case 1:
			branch = c;
			break;
		case 2:
			branch = !le;
			break;
		case 3:
			branch = le;
			break;
		case 4:
			branch = !lts;
			break;
		case 5:
			branch = lts;
			break;
		case 6:
			branch = !les;
			break;
		case 7:
			branch = les;
			break;
		case 8:
			branch = !z;
			break;
		case 9:
			branch = z;
			break;
		case 10:
			branch = !ov;
			break;
		case 11:
			branch = ov;
			break;
		case 12:
			branch = !s;
			break;
		case 13:
			branch = s;
			break;
		case 14:
			branch = true;
			break;
		case 15:
		default:
			branch = false;
			break;
		}

		if (branch) {
			impl_operands[0].value |= (impl_operands[0].value & 0x80) ? 0x7F00 : 0;
			reg_pc += impl_operands[0].value << 1;
		}
	}

	// * Software Interrupt Instructions
	void CPU::OP_SWI() {
		emulator.chipset.RaiseSoftware(impl_operands[0].value & 0x3F);
	}

	void CPU::OP_BRK() {
		emulator.chipset.Break();
	}

	void CPU::OP_ICESWI() {
		std::cout << "Software breakpoint hit!(0xFEFF)\n";
		emulator.chipset.RaiseEmulator();
	}
	void CPU::OP_RTICE() {
		OP_RTI();
	}
	// * Branch Instructions
	void CPU::OP_B() {
		if (impl_hint & H_TI) {
			reg_csr = impl_operands[1].value;
			reg_pc = impl_long_imm;
		}
		else
			reg_pc = impl_operands[1].value;
	}

	void CPU::OP_BL() {
#ifdef DBG
		auto stack = this->stack.get();
#endif
		reg_lr = reg_pc;
		reg_lcsr = reg_csr;
		// if (!stack->empty() && !stack->back().lr_pushed) {}
		OP_B();
#ifdef DBG
		StackFrame sf{};
		sf.er0 = reg_r[0] | (reg_r[1] << 8);
		sf.er2 = reg_r[2] | (reg_r[3] << 8);
		sf.sp = reg_sp;
		sf.new_pc = reg_csr << 16 | reg_pc;
		if (!stack->empty() && !stack->back().lr_pushed) {
			std::cout << "[CPU][Warn] Lr get override.(BL after lr not backuped)\n";
			stack->back().lr = 0xffffff;
			stack->back().lr_push_address = 0;
			stack->back().lr_pushed = true;
			// stack->clear();
		}
		stack->push_back(sf);
		if (on_call_function)
			on_call_function(*this, {sf.new_pc, (uint32_t)(reg_lcsr << 16 | reg_lr)});
#endif
	}

	// * Miscellaneous Instructions
	void CPU::OP_RT() {
#ifdef DBG
		auto stack = this->stack.get();
		if (stack->empty()) {}
		else {
			if (stack->back().lr_pushed) {}
			stack->pop_back();
		}
#endif
		reg_csr = reg_lcsr;
		reg_pc = reg_lr;
	}

	void CPU::OP_RTI() {
		reg_csr = reg_ecsr[reg_psw & PSW_ELEVEL];
		reg_pc = reg_elr[reg_psw & PSW_ELEVEL];
		reg_psw = reg_epsw[reg_psw & PSW_ELEVEL];
		emulator.chipset.isMIBlocked = true;
	}
} // namespace casioemu
