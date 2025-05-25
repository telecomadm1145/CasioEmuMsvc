/*
	JitU8 ±‡“Î”≈ªØ
*/
#include "CPU.hpp"
#include "MMU.hpp"
#include "asmjit/src/asmjit/asmjit.h"
#include <optional>
#include <unordered_map>

using namespace asmjit;

class JitU8 {
public:
	JitRuntime rt;
	casioemu::MMU& mmu;
	casioemu::CPU& cpu;
	struct Function {

		size_t offset;
		size_t size;

		std::unique_ptr<CodeHolder> code;
	};
	std::unordered_map<size_t, size_t> call_count;
	enum CheckStatus {
		None,
		Met,
		NotMet,
	};
	std::unordered_map<size_t, CheckStatus> condition_check;
	std::unordered_map<size_t, Function> cached_function;
	std::optional<Function> JitCall(size_t offset) {
	}
	std::optional<Function> CompileAt(size_t offset) {
		CodeHolder& code = *(new CodeHolder());
		code.init(rt.environment(), rt.cpuFeatures());
		x86::Compiler cc(&code);
		auto func = cc.addFunc(FuncSignature::build<void* /*CPU*/>());
		x86::Gp reg_cpu = cc.newIntPtr();
		func->setArg(0, reg_cpu);
		auto pc = offset & 0xffff;
		auto csr = offset & 0xff0000;

		auto reg_off = offsetof(casioemu::CPU::reg8_t, casioemu::CPU::reg8_t::raw);
		auto reg8_size = sizeof(casioemu::CPU::reg8_t);
		auto reg16_size = sizeof(casioemu::CPU::reg16_t);
		auto lr_off = offsetof(casioemu::CPU, casioemu::CPU::reg_lr);
		auto gpr_off = offsetof(casioemu::CPU, casioemu::CPU::reg_r);
		auto lcsr_off = offsetof(casioemu::CPU, casioemu::CPU::reg_lcsr);
		auto csr_off = offsetof(casioemu::CPU, casioemu::CPU::reg_csr);
		auto psw_off = offsetof(casioemu::CPU, casioemu::CPU::reg_psw);
		auto pc_off = offsetof(casioemu::CPU, casioemu::CPU::reg_pc);

		auto op_off = offsetof(casioemu::CPU, casioemu::CPU::impl_operands);
		using operand_tp = std::remove_cvref_t<decltype(*casioemu::CPU::impl_operands)>;

		auto op_size = sizeof(operand_tp);
		auto op_value_off = offsetof(operand_tp, operand_tp::value);
		auto op_reg_index_off = offsetof(operand_tp, operand_tp::register_index);
		auto op_reg_size_off = offsetof(operand_tp, operand_tp::register_size);

		auto long_imm_off = offsetof(casioemu::CPU, casioemu::CPU::impl_long_imm);
		auto hint_off = offsetof(casioemu::CPU, casioemu::CPU::impl_hint);

		auto flags_changed_off = offsetof(casioemu::CPU, casioemu::CPU::impl_flags_changed);
		auto flags_in_off = offsetof(casioemu::CPU, casioemu::CPU::impl_flags_in);
		auto flags_out_off = offsetof(casioemu::CPU, casioemu::CPU::impl_flags_out);

		std::map<size_t, std::optional<Label>> jump_map;
		size_t counter = 4096; // maximum instructions to compile.
		while (counter--) {
			auto op = mmu.ReadCode(csr | pc);
			pc += 2;
			pc &= 0xffff;
			if (!jump_map[csr | pc].has_value())
				jump_map[csr | pc] = cc.newLabel();
			cc.bind(jump_map[csr | pc].value());
			auto opd = cpu.opcode_dispatch[op];
			if (!opd)
				continue;
			auto imm = 0;
			if (opd->hint | casioemu::CPU::OpcodeHint::H_TI) {
				imm = mmu.ReadCode(csr | pc);
				pc += 2;
				pc &= 0xffff;
			}

			// Move (E)LR to PC.
			if (opd->handler_function == casioemu::CPU::OP_RT ||
				opd->handler_function == casioemu::CPU::OP_RTI) {
				auto gp1 = cc.newUInt16();
				cc.mov(x86::Mem(reg_cpu, lr_off + reg_off), gp1);
				cc.mov(gp1, x86::Mem(reg_cpu, pc_off + reg_off));
				auto gp2 = cc.newUInt8();
				cc.mov(x86::Mem(reg_cpu, lcsr_off + reg_off), gp2);
				cc.mov(gp2, x86::Mem(reg_cpu, csr_off + reg_off));
				cc.ret();
			}
			/*

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
	// * Branch Instructions
	void CPU::OP_B() {
		if (impl_hint & H_TI) {
			reg_csr = impl_operands[1].value;
			reg_pc = impl_long_imm;
		}
		else
			reg_pc = impl_operands[1].value;
	}

			*/

			/*
					{&CPU::OP_B          ,        H_TI              , 0xF000, {{0,      0,  0}, {0, 0x000F,  8}}},
		{&CPU::OP_B          ,                         0, 0xF002, {{0,      0,  0}, {2, 0x000E,  4}}},
		{&CPU::OP_BL         ,        H_TI              , 0xF001, {{0,      0,  0}, {0, 0x000F,  8}}},
		{&CPU::OP_BL         ,                         0, 0xF003, {{0,      0,  0}, {2, 0x000E,  4}}},
			*/

			// Jump to a address.
			else if ((opd->handler_function == casioemu::CPU::OP_B) ||
					 (opd->handler_function == casioemu::CPU::OP_BL)) {
				if (opd->hint & casioemu::CPU::H_TI) {
					if (opd->handler_function == casioemu::CPU::OP_BL)
						goto directly_call;
					auto target = (size_t)imm + (((size_t)op & 0xf) << 16);
					if (!jump_map[target].has_value()) {
						auto lb = cc.newLabel();
						jump_map[target] = lb;
						cc.jmp(lb);
					}
					cc.jmp(jump_map[target].value());
				}
				// for B/BL ERm, we should redirect it to interpreter again.
				else {
					cc.mov(x86::Mem(reg_cpu, pc_off + reg_off), Imm((uint16_t)pc));
					cc.mov(x86::Mem(reg_cpu, csr_off + reg_off), Imm((uint8_t)(csr >> 16)));
					cc.ret();
				}
			}
			else if (opd->handler_function == casioemu::CPU::OP_BC) {
			}

			//	void CPU::OP_POPL() {
			// #ifdef DBG
			//				auto stack = this->stack.get();
			// #endif
			//				if (impl_operands[0].value & 1)
			//					reg_ea = Pop16();
			//				if (impl_operands[0].value & 8) {
			//					/**
			//					 * Sometimes a function calls another function in one branch, and
			//					 * does not call another function in another branch. In that case
			//					 * the compiler may decide to do a `push lr` / `pop lr` in only the
			//					 * branch that has to save `lr`.
			//					 */
			// #ifdef DBG
			//					if (!stack->empty() && stack->back().lr_pushed &&
			//						stack->back().lr_push_address == reg_sp)
			//						stack->back().lr_pushed = false;
			// #endif
			//
			//					reg_lr = Pop16();
			//					if (memory_model == MM_LARGE)
			//						reg_lcsr = Pop16() & 0x000F;
			//				}
			//				if (impl_operands[0].value & 4)
			//					reg_psw = Pop16();
			//				if (impl_operands[0].value & 2) {
			// #ifdef DBG
			//					int oldsp = reg_sp;
			//					auto oldaddr = (uint32_t)reg_pc | reg_csr << 16;
			// #endif
			//					reg_pc = Pop16();
			//					if (memory_model == MM_LARGE)
			//						reg_csr = Pop16() & 0x000F;
			// #ifdef DBG
			//					if (!stack->empty()) {
			//						if (stack->back().lr_pushed) {
			//							auto& m = emulator.chipset.mmu;
			//							auto a = stack->back().lr_push_address;
			//							auto lr_o = m.ReadData(a) | (m.ReadData(a + 1) << 8) | (m.ReadData(a + 2) << 16);
			//							if (stack->back().lr_push_address == oldsp) {
			//								if (stack->back().lr != lr_o) {
			//									// std::cout << "[CPU][Warn] lr get overrided.\n";
			//									// TODO: lets treat it as calling a new function?
			//									stack->back().is_jump = true;
			//								}
			//								else {
			//									RaiseEvent(on_function_return, *this, FunctionEventArgs{oldaddr, (uint32_t)reg_pc | reg_csr << 16});
			//									stack->pop_back();
			//								}
			//							}
			//							else {
			//								// std::cout << "[CPU][Warn] stack unbalanced.\n";
			//								// TODO: lets treat it as calling a new function?
			//								stack->back().is_jump = true;
			//							}
			//						}
			//						else {
			//							stack->back().is_jump = true;
			//							stack->back().new_pc = reg_csr << 16 | reg_pc;
			//						}
			//					}
			// #endif
			//				}
			//			}

			// It's possible to use `pop pc` to call a function, which is tricky
			// For example:
			// <code>
			// func_call_func:
			// push xr0
			// pop pc
			// </code>
			// Let's just move pc to that instruction and return.
			else if (opd->handler_function == casioemu::CPU::OP_POPL) {
				// cc.call(x86::Mem(*(uint64_t*)&opd->handler_function));
			}
			else {
			directly_call:

				// Prepare the env for CPU functions to work.
				cc.mov(x86::Mem(reg_cpu, long_imm_off), Imm((uint16_t)imm));
				for (size_t ix = 0; ix != 2; ++ix) {

					// impl_operands[ix].value = (impl_opcode >> handler->operands[ix].shift) & handler->operands[ix].mask;
					// impl_operands[ix].register_index = impl_operands[ix].value;
					// impl_operands[ix].register_size = handler->operands[ix].register_size;

					if (opd->operands[ix].mask == 0 && opd->operands[ix].register_size == 0)
						continue;

					if (!opd->operands[ix].register_size)
						cc.mov(x86::Mem(reg_cpu, op_off + op_size * ix + op_value_off), Imm((decltype(operand_tp::value))(op >> opd->operands[ix].shift) & opd->operands[ix].mask));
					cc.mov(x86::Mem(reg_cpu, op_off + op_size * ix + op_reg_index_off), Imm((decltype(operand_tp::register_index))(op >> opd->operands[ix].shift) & opd->operands[ix].mask));
					cc.mov(x86::Mem(reg_cpu, op_off + op_size * ix + op_reg_size_off), Imm((decltype(operand_tp::register_size))(opd->operands[ix].register_size)));

					if (opd->operands[ix].register_size) {
						/*
						impl_operands[ix].value = 0;
						for (size_t bx = 0; bx != impl_operands[ix].register_size; ++bx)
							impl_operands[ix].value |= (uint64_t)(reg_r[impl_operands[ix].register_index + bx]) << (bx * 8);
						*/
						auto gp = cc.newUInt64();
						auto gp2 = cc.newUInt64();
						cc.mov(gp, Imm((size_t)0));
						for (size_t bx = 0; bx != opd->operands[ix].register_size; ++bx) {
							cc.mov(gp2, x86::Mem(reg_cpu, gpr_off + reg8_size * bx + reg_off));
							cc.and_(gp2, 0xff);
							cc.shl(gp2, bx * 8);
							cc.or_(gp, gp2);
						}
						cc.mov(x86::Mem(reg_cpu, op_off + op_size * ix + op_value_off), gp);
					}
				}

				// Clear flags.
				/*
							impl_flags_changed = 0;
				impl_flags_in = reg_psw;
				impl_flags_out = PSW_Z;
				(this->*(handler->handler_function))();

				reg_psw &= ~impl_flags_changed;
				reg_psw |= impl_flags_out & impl_flags_changed;
				*/
				cc.mov(x86::Mem(reg_cpu, flags_changed_off), (uint8_t)0);
				auto gp = cc.newUInt8();
				cc.mov(gp, x86::Mem(reg_cpu, psw_off + reg_off));
				cc.mov(x86::Mem(reg_cpu, flags_in_off), gp);
				cc.mov(x86::Mem(reg_cpu, flags_out_off), (uint8_t)casioemu::CPU::PSW_Z);
				// call the function here.
				cc.mov(x86::rcx, reg_cpu);
				cc.call((*(uint64_t*)&opd->handler_function));

				cc.mov(gp, x86::Mem(reg_cpu, flags_changed_off));
				cc.not_(gp);
				cc.and_(x86::Mem(reg_cpu, psw_off + reg_off),gp);
				cc.not_(gp);
				cc.and_(x86::Mem(reg_cpu, flags_out_off), gp);
				cc.or_(x86::Mem(reg_cpu, psw_off + reg_off), gp);

				/*
				
			if (handler->hint & H_WB && impl_operands[0].register_size)
				for (size_t bx = 0; bx != impl_operands[0].register_size; ++bx)
					reg_r[impl_operands[0].register_index + bx] = (uint8_t)(impl_operands[0].value >> (bx * 8));

			if (!(handler->hint & H_DS))
				break;
				*/
				// Write back
			}

			// to make debugger still work after JITed, call the hook
#ifdef DBG
			/*
			 		InstructionEventArgs iea{};
		iea.pc_before = pc_before;
		iea.pc_after = reg_csr << 16 | reg_pc;
		RaiseEvent(on_instruction, *this, iea);
		if (iea.should_break) {
			emulator.SetPaused(true);
		}
			*/
#endif
		}
		if (counter == 0) {
			return {};
		}
		cc.endFunc();
		Function func2{offset, 0, std::unique_ptr<CodeHolder>(&code)};
		return func2;
	}
	void OnMMUCodeWrite(size_t offset) {
	}
};