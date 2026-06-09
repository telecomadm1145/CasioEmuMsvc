#include "CPU.hpp"

#include "Chipset.hpp"
#include "Emulator.hpp"
#include "MMU.hpp"

#include "Gui/Hooks.h"

#pragma warning(disable : 4244)

namespace casioemu {
	// * PUSH/POP Instructions
	void CPU::OP_PUSH() {
		size_t push_size = impl_operands[1].register_size;
		if (push_size == 1)
			push_size = 2;
		reg_sp -= push_size;
		for (size_t ix = impl_operands[1].register_size - 1; ix != (size_t)-1; --ix)
			emulator.chipset.mmu.WriteData(reg_sp + ix, impl_operands[1].value >> (8 * ix));
	}

	void CPU::OP_PUSHL() {
#ifdef DBG
		auto stack = this->stack.get();
#endif
		if (impl_operands[1].value & 2) {
			if (memory_model == MM_LARGE)
				Push16(reg_ecsr[reg_psw & PSW_ELEVEL]);
			Push16(reg_elr[reg_psw & PSW_ELEVEL]);
		}
		if (impl_operands[1].value & 4)
			Push16(reg_epsw[reg_psw & PSW_ELEVEL]);
		if (impl_operands[1].value & 8) {
			if (memory_model == MM_LARGE)
				Push16(reg_lcsr);
			Push16(reg_lr);
#ifdef DBG
			if (stack->empty()) {}
			else if (stack->back().lr_pushed) {}
			else {
				stack->back().lr_pushed = true;
				stack->back().lr_push_address = reg_sp;
				stack->back().lr = reg_lcsr << 16 | reg_lr;
			}
#endif
		}
		if (impl_operands[1].value & 1)
			Push16(reg_ea);
	}

	void CPU::OP_POP() {
		size_t pop_size = impl_operands[0].register_size;
		if (pop_size == 1)
			pop_size = 2;
		impl_operands[0].value = 0;
		for (size_t ix = 0; ix != impl_operands[0].register_size; ++ix)
			impl_operands[0].value |= ((uint64_t)emulator.chipset.mmu.ReadData(reg_sp + ix)) << (8 * ix);
		reg_sp += pop_size;
	}

	void CPU::OP_POPL() {
#ifdef DBG
		auto stack = this->stack.get();
#endif
		if (impl_operands[0].value & 1)
			reg_ea = Pop16();
		if (impl_operands[0].value & 8) {
			/**
			 * Sometimes a function calls another function in one branch, and
			 * does not call another function in another branch. In that case
			 * the compiler may decide to do a `push lr` / `pop lr` in only the
			 * branch that has to save `lr`.
			 */
#ifdef DBG
			if (!stack->empty() && stack->back().lr_pushed &&
				stack->back().lr_push_address == reg_sp)
				stack->back().lr_pushed = false;
#endif

			reg_lr = Pop16();
			if (memory_model == MM_LARGE)
				reg_lcsr = Pop16() & 0x000F;
		}
		if (impl_operands[0].value & 4)
			reg_psw = Pop16();
		if (impl_operands[0].value & 2) {
#ifdef DBG
			int oldsp = reg_sp;
			auto oldaddr = (uint32_t)reg_pc | reg_csr << 16;
#endif
			reg_pc = Pop16();
			if (memory_model == MM_LARGE)
				reg_csr = Pop16() & 0x000F;
#ifdef DBG
			if (!stack->empty()) {
				if (stack->back().lr_pushed) {
					auto& m = emulator.chipset.mmu;
					auto a = stack->back().lr_push_address;
					auto lr_o = m.ReadData(a) | (m.ReadData(a + 1) << 8) | (m.ReadData(a + 2) << 16);
					if (stack->back().lr_push_address == oldsp) {
						if (stack->back().lr != lr_o) {
							// std::cout << "[CPU][Warn] lr get overrided.\n";
							// TODO: lets treat it as calling a new function?
							stack->back().is_jump = true;
						}
						else {
							RaiseEvent(on_function_return, *this, FunctionEventArgs{oldaddr, (uint32_t)reg_pc | reg_csr << 16});
							stack->pop_back();
						}
					}
					else {
						// std::cout << "[CPU][Warn] stack unbalanced.\n";
						// TODO: lets treat it as calling a new function?
						stack->back().is_jump = true;
					}
				}
				else {
					stack->back().is_jump = true;
					stack->back().new_pc = reg_csr << 16 | reg_pc;
				}
			}
#endif
		}
	}

	void CPU::Push16(uint16_t data) {
		reg_sp -= 2;
		emulator.chipset.mmu.WriteData(reg_sp + 1, data >> 8);
		emulator.chipset.mmu.WriteData(reg_sp, data & 0xFF);
	}

	uint16_t CPU::Pop16() {
		uint16_t result = emulator.chipset.mmu.ReadData(reg_sp) | (((uint16_t)emulator.chipset.mmu.ReadData(reg_sp + 1)) << 8);
		reg_sp += 2;
		return result;
	}
} // namespace casioemu
