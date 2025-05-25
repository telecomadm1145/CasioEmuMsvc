/*
	JitU8 编译优化
*/
#include "CPU.hpp"
#include "MMU.hpp"
#include "asmjit/src/asmjit/asmjit.h"
#include <map> // For jump_map
#include <optional>
#include <unordered_map>
#include <vector> // For worklist in potential future advanced CompileAt

// Define PSW bit indices if not available from CPU.hpp. Replace with actual values.
// These are typical but may vary for casioemu::CPU.
// If casioemu::CPU::PSW_C, etc. are masks (e.g. 0x01, 0x04), use those directly.
// For direct bit testing (bt instruction), indices are needed.
// Example: if PSW_C is (1 << 0), PSW_C_BIT_IDX is 0.
//          if PSW_Z is (1 << 2), PSW_Z_BIT_IDX is 2.
//          if PSW_S is (1 << 3), PSW_S_BIT_IDX is 3.
//          if PSW_OV is (1 << 4), PSW_OV_BIT_IDX is 4.
// For this implementation, I'll use the masks directly with 'test' or 'and' if bit indices are not known.
// The C++ OP_BC implementation implies flags are already in CPU members like `impl_flags_in`.
// For simplicity and correctness, OP_BC JIT will call a helper or be very carefully JITed.

// Let's assume these constants from casioemu::CPU are available:
// casioemu::CPU::PSW_C, PSW_Z, PSW_S, PSW_OV (as masks)
// casioemu::CPU::OpcodeHint::H_TI, H_WB, H_DS

using namespace asmjit;

// Threshold for JIT compilation
static const int JIT_CALL_THRESHOLD = 5;
// Max instructions to compile in a single block
static const size_t MAX_JIT_INSTRUCTIONS_PER_BLOCK = 128; // Reduced from 4096 for practical block sizes

class JitU8 {
public:
	JitRuntime rt;
	casioemu::MMU& mmu;
	casioemu::CPU& cpu;

	struct Function {
		size_t guest_code_start_addr; // Start address (CSR|PC) of the guest code block
		size_t guest_code_end_addr;	  // End address (CSR|PC) of the guest code block (address *after* last instruction)
		size_t jit_code_host_size;	  // Size of the compiled native code

		std::unique_ptr<CodeHolder> code;  // AsmJit code holder
		void* compiled_func_ptr = nullptr; // Pointer to executable JITed code

		// Default constructor for map emplacement
		Function() = default;

		Function(size_t start, size_t end, size_t host_size, std::unique_ptr<CodeHolder> ch)
			: guest_code_start_addr(start), guest_code_end_addr(end),
			  jit_code_host_size(host_size), code(std::move(ch)), compiled_func_ptr(nullptr) {}
	};

	std::unordered_map<size_t, size_t> call_count;
	enum CheckStatus {
		None,	// Not yet checked or below threshold
		Met,	// Conditions met, JIT compilation attempted/successful
		NotMet, // Conditions not met, or JIT compilation failed, don't retry soon
	};
	std::unordered_map<size_t, CheckStatus> condition_check;
	std::unordered_map<size_t, Function> cached_function;

	JitU8(casioemu::MMU& m, casioemu::CPU& c) : mmu(m), cpu(c) {
		// Initialize JitRuntime, potentially
	}

	std::optional<std::reference_wrapper<Function>> JitCall(size_t offset) {
		auto it = cached_function.find(offset);
		if (it != cached_function.end()) {
			Function& func = it->second;
			if (!func.compiled_func_ptr) {
				// This should ideally be done once after successful compilation
				// For simplicity, doing it here if somehow it's null
				Error err = rt.add(&func.compiled_func_ptr, func.code.get());
				if (err) {
					// Failed to finalize code
					cached_function.erase(it); // Remove broken entry
					condition_check[offset] = NotMet;
					return std::nullopt;
				}
			}
			// Execute the JITed function
			// The JITed function takes casioemu::CPU* as its argument
			reinterpret_cast<void (*)(casioemu::CPU*)>(func.compiled_func_ptr)(&cpu);
			return func;
		}

		// Function not in cache, check eligibility for JIT
		call_count[offset]++;
		CheckStatus status = condition_check.count(offset) ? condition_check[offset] : None;

		if (status == NotMet) {
			return std::nullopt; // Previously failed or decided not to JIT
		}

		if (status == None && call_count[offset] <= JIT_CALL_THRESHOLD) {
			return std::nullopt; // Below threshold
		}

		// Eligible for JIT compilation
		condition_check[offset] = Met; // Mark as attempting/met
		std::optional<Function> compiled_func_opt = CompileAt(offset);

		if (compiled_func_opt) {
			Function& new_func_storage = compiled_func_opt.value();
			Error err = rt.add(&new_func_storage.compiled_func_ptr, new_func_storage.code.get());
			if (err) {
				condition_check[offset] = NotMet; // Compilation finalized failed
				return std::nullopt;
			}

			auto [inserted_it, success] = cached_function.emplace(offset, std::move(new_func_storage));
			if (!success) {										   // Should not happen if logic is correct
				rt.release(inserted_it->second.compiled_func_ptr); // Release if somehow already existed
				condition_check[offset] = NotMet;
				return std::nullopt;
			}

			Function& func_in_cache = inserted_it->second;
			// Execute the newly JITed function
			reinterpret_cast<void (*)(casioemu::CPU*)>(func_in_cache.compiled_func_ptr)(&cpu);
			return func_in_cache;
		}
		else {
			condition_check[offset] = NotMet; // Compilation failed
			return std::nullopt;
		}
	}

	std::optional<Function> CompileAt(size_t entry_offset) {
		auto code_ptr = std::make_unique<CodeHolder>();
		code_ptr->init(rt.environment(), rt.cpuFeatures());
		x86::Compiler cc(code_ptr.get());

		auto func_sig = FuncSignature::build<void, void*>(); // void JIT_Func(CPU* cpu_ptr);
		FuncNode* func_node = cc.addFunc(func_sig);

		x86::Gp reg_cpu = cc.newIntPtr("reg_cpu"); // To hold the CPU* argument
		func_node->setArg(0, reg_cpu);

		uint16_t pc = entry_offset & 0xFFFF;
		uint32_t csr_val = entry_offset & 0xFF0000; // Full CSR shifted value e.g. 0x0F0000

		// CPU struct member offsets
		auto reg_off = offsetof(casioemu::CPU::reg8_t, casioemu::CPU::reg8_t::raw);
		auto reg8_size = sizeof(casioemu::CPU::reg8_t);
		// auto reg16_size = sizeof(casioemu::CPU::reg16_t); // Not used directly it seems
		auto lr_off = offsetof(casioemu::CPU, reg_lr);
		auto gpr_off = offsetof(casioemu::CPU, reg_r);
		auto lcsr_off = offsetof(casioemu::CPU, reg_lcsr);
		auto csr_off = offsetof(casioemu::CPU, reg_csr);
		auto psw_off = offsetof(casioemu::CPU, reg_psw);
		auto pc_off = offsetof(casioemu::CPU, reg_pc);

		auto op_operands_off = offsetof(casioemu::CPU, impl_operands);
		using operand_tp = std::remove_cvref_t<decltype(cpu.impl_operands[0])>;
		auto op_size = sizeof(operand_tp);
		auto op_value_off = offsetof(operand_tp, value);
		auto op_reg_index_off = offsetof(operand_tp, register_index);
		auto op_reg_size_off = offsetof(operand_tp, register_size);

		auto long_imm_off = offsetof(casioemu::CPU, impl_long_imm);
		auto hint_off = offsetof(casioemu::CPU, impl_hint);

		auto flags_changed_off = offsetof(casioemu::CPU, impl_flags_changed);
		auto flags_in_off = offsetof(casioemu::CPU, impl_flags_in);
		auto flags_out_off = offsetof(casioemu::CPU, impl_flags_out);

		std::map<size_t, Label> jump_map; // Using std::map for ordered iteration if needed, keys are guest_addr
		size_t instructions_compiled_count = 0;
		bool block_terminated_by_jit = false;

		// Label for the very first instruction in the block
		Label entry_label = cc.newLabel();
		jump_map[entry_offset] = entry_label;
		cc.bind(entry_label);

		for (; instructions_compiled_count < MAX_JIT_INSTRUCTIONS_PER_BLOCK; ++instructions_compiled_count) {
			size_t current_instr_addr = csr_val | pc;

			// Bind label if it's a known jump target.
			// If current_instr_addr is not entry_offset, it might have been added to jump_map by a previous branch.
			if (jump_map.count(current_instr_addr)) {
				// Check if label already bound to avoid re-binding. AsmJit might error.
				// Simple approach: always try to bind. More robust: track bound labels.
				// AsmJit's cc.bind is idempotent if label is already bound to current location.
				cc.bind(jump_map[current_instr_addr]);
			}
			else {
				// This case implies non-contiguous compilation, or an issue.
				// For linear scan, this shouldn't be hit after the first instruction.
				// If it's a new unforeseen entry, create and bind.
				Label new_label = cc.newLabel();
				jump_map[current_instr_addr] = new_label;
				cc.bind(new_label);
			}

			uint16_t op = mmu.ReadCode(current_instr_addr); // Read 2 bytes for opcode

			uint16_t pc_after_op = (pc + 2) & 0xFFFF;
			uint16_t imm = 0;

			auto opd = cpu.opcode_dispatch[op];
			if (!opd) { // Unknown or unimplemented opcode
				// Update PC to this unknown instruction and return to interpreter
				cc.mov(x86::Mem(reg_cpu, pc_off + reg_off), Imm(pc));
				cc.mov(x86::Mem(reg_cpu, csr_off + reg_off), Imm(static_cast<uint8_t>(csr_val >> 16)));
				cc.ret();
				block_terminated_by_jit = true;
				break;
			}

			if (opd->hint & casioemu::CPU::OpcodeHint::H_TI) {
				imm = mmu.ReadCode(csr_val | pc_after_op);
				pc = (pc_after_op + 2) & 0xFFFF; // PC is now after opcode and immediate
			}
			else {
				pc = pc_after_op; // PC is now after opcode
			}

			// Prepare label for the *next* sequential instruction (fallthrough)
			size_t next_sequential_addr = csr_val | pc;
			if (!jump_map.count(next_sequential_addr)) {
				jump_map[next_sequential_addr] = cc.newLabel();
			}
			Label fallthrough_label = jump_map[next_sequential_addr];

			// --- Instruction Handling ---
			if (opd->handler_function == &casioemu::CPU::OP_RT ||
				opd->handler_function == &casioemu::CPU::OP_RTI) {
				x86::Gp gp1 = cc.newUInt16("rt_lr_pc");
				cc.mov(gp1, x86::Mem(reg_cpu, lr_off + reg_off)); // Assuming reg_lr is reg16_t, access its .raw
				cc.mov(x86::Mem(reg_cpu, pc_off + reg_off), gp1);

				x86::Gp gp2 = cc.newUInt8("rt_lcsr_csr");
				cc.mov(gp2, x86::Mem(reg_cpu, lcsr_off + reg_off)); // Assuming reg_lcsr is reg8_t, access its .raw
				cc.mov(x86::Mem(reg_cpu, csr_off + reg_off), gp2);
				cc.ret();
				block_terminated_by_jit = true;
				break; // End of JIT block
			}
			else if (opd->handler_function == &casioemu::CPU::OP_B ||
					 opd->handler_function == &casioemu::CPU::OP_BL) {
				if (opd->hint & casioemu::CPU::OpcodeHint::H_TI) { // B/BL #imm (Absolute address)
																   // Target address calculation for B #imm24 / BL #imm24
					// operand[1] is CSR bits, imm is PC
					// Check casioemu::CPU::impl_operands[1].value for CSR, ::impl_long_imm for PC.
					// The 'op & 0xf' is for specific encoding variants, must match CPU decoder logic.
					// For H_TI, `imm` is the 16-bit immediate (PC). CSR comes from operand.
					// The example used `(op & 0xf) << 16` for CSR part.
					// This should be derived from `opd->operands[1]` if it holds CSR.
					// Assuming opd->operands[1] is used for CSR part:
					uint8_t target_csr_byte = (op >> opd->operands[1].shift) & opd->operands[1].mask;
					size_t target_addr = (static_cast<size_t>(target_csr_byte) << 16) | imm;

					if (opd->handler_function == &casioemu::CPU::OP_BL) { // BL #target (Call)
																		  // Fall through to directly_call_and_ret behavior
						// This means setting up operands, calling C++ handler, then JIT ret.
						// (This path will be handled by the generic handler + special ret logic later)
						goto directly_call_path;
					}
					else { // B #target (Jump)
						// If target CSR is different, this is an inter-segment jump.
						// JIT usually ends block here.
						if ((target_addr & 0xFF0000) != csr_val) {
							// Inter-segment jump: Update CPU state and ret from JIT
							cc.mov(x86::Mem(reg_cpu, pc_off + reg_off), Imm(static_cast<uint16_t>(target_addr & 0xFFFF)));
							cc.mov(x86::Mem(reg_cpu, csr_off + reg_off), Imm(static_cast<uint8_t>((target_addr >> 16) & 0xFF)));
							cc.ret();
							block_terminated_by_jit = true;
						}
						else {
							// Intra-segment jump: Use JIT label
							if (!jump_map.count(target_addr)) {
								jump_map[target_addr] = cc.newLabel();
							}
							cc.jmp(jump_map[target_addr]);
							// JIT execution path diverges, compiler loop might continue if other paths exist.
							// For simple linear scan, this also means end of current continuous trace.
							block_terminated_by_jit = true; // Current linear path ends
						}
						break;
					}
				}
				else { // B ERm, BL ERm (Register indirect)
					   // Update PC/CSR in CPU struct to current values, then call C++ handler via direct_call, then ret.
					// The C++ handler will read operands and set the new PC/CSR.
					// This will be handled by directly_call_path and its logic.
					goto directly_call_path;
				}
			}
			else if (opd->handler_function == &casioemu::CPU::OP_BC) {
				// Conditional Branch: BC cond, displacement
				// Displacement is from opd->operands[0] (usually op & 0xFF for BC)
				// Displacement is sign-extended 8-bit value, shifted left by 1.
				int8_t disp_s8 = static_cast<int8_t>((op >> opd->operands[0].shift) & opd->operands[0].mask);
				int16_t branch_offset = static_cast<int16_t>(disp_s8) << 1;

				// Target PC if branch taken: (PC of instruction *after* BC + branch_offset)
				// `pc` variable is already PC after BC and its potential immediate.
				// For BC, there's no immediate, so `pc` = (PC of BC) + 2.
				uint16_t target_pc_if_taken = (pc + branch_offset) & 0xFFFF;
				size_t target_full_addr_if_taken = csr_val | target_pc_if_taken;

				Label lbl_branch_taken_jit;
				if (!jump_map.count(target_full_addr_if_taken)) {
					jump_map[target_full_addr_if_taken] = cc.newLabel();
				}
				lbl_branch_taken_jit = jump_map[target_full_addr_if_taken];

				// Fallthrough label is already prepared as `fallthrough_label`

				// --- Evaluate condition ---
				// Load PSW
				x86::Gp psw_val_reg = cc.newUInt8("psw_for_bc");
				cc.mov(psw_val_reg, x86::Mem(reg_cpu, psw_off + reg_off));

				// Condition evaluation logic (from CPU::OP_BC reference)
				// Need actual PSW bit definitions from casioemu::CPU
				// Assuming casioemu::CPU::PSW_C, ::PSW_Z etc are masks.
				x86::Gp c = cc.newUInt8("c_flag");
				x86::Gp z = cc.newUInt8("z_flag");
				x86::Gp s = cc.newUInt8("s_flag");
				x86::Gp ov = cc.newUInt8("ov_flag");

				// Extract C: (psw & PSW_C) != 0
				cc.mov(c, psw_val_reg);
				cc.and_(c, casioemu::CPU::PSW_C);
				cc.setne(c);
				// Extract Z: (psw & PSW_Z) != 0
				cc.mov(z, psw_val_reg);
				cc.and_(z, casioemu::CPU::PSW_Z);
				cc.setne(z);
				// Extract S: (psw & PSW_S) != 0
				cc.mov(s, psw_val_reg);
				cc.and_(s, casioemu::CPU::PSW_S);
				cc.setne(s);
				// Extract OV: (psw & PSW_OV) != 0
				cc.mov(ov, psw_val_reg);
				cc.and_(ov, casioemu::CPU::PSW_OV);
				cc.setne(ov);

				x86::Gp le = cc.newUInt8("le_cond");
				cc.mov(le, z);
				cc.or_(le, c); // le = z || c
				x86::Gp lts = cc.newUInt8("lts_cond");
				cc.mov(lts, ov);
				cc.xor_(lts, s); // lts = ov ^ s
				x86::Gp les = cc.newUInt8("les_cond");
				cc.mov(les, lts);
				cc.or_(les, z); // les = lts || z

				uint8_t cond_type = (op >> 8) & 0x000F; // Condition code from opcode
				bool branched_unconditionally = false;
				switch (cond_type) {
				// For "branch if true": cmp flag, 1; je target
				// For "branch if false": cmp flag, 0; je target
				case 0:
					cc.cmp(c, 0);
					cc.je(lbl_branch_taken_jit);
					break; // !c  (BC)
				case 1:
					cc.cmp(c, 1);
					cc.je(lbl_branch_taken_jit);
					break; //  c  (BNC)
				case 2:
					cc.cmp(le, 0);
					cc.je(lbl_branch_taken_jit);
					break; // !le (BH)
				case 3:
					cc.cmp(le, 1);
					cc.je(lbl_branch_taken_jit);
					break; //  le (BLS)
				case 4:
					cc.cmp(lts, 0);
					cc.je(lbl_branch_taken_jit);
					break; // !lts(BGE)
				case 5:
					cc.cmp(lts, 1);
					cc.je(lbl_branch_taken_jit);
					break; //  lts(BLT)
				case 6:
					cc.cmp(les, 0);
					cc.je(lbl_branch_taken_jit);
					break; // !les(BGT)
				case 7:
					cc.cmp(les, 1);
					cc.je(lbl_branch_taken_jit);
					break; //  les(BLE)
				case 8:
					cc.cmp(z, 0);
					cc.je(lbl_branch_taken_jit);
					break; // !z  (BNE)
				case 9:
					cc.cmp(z, 1);
					cc.je(lbl_branch_taken_jit);
					break; //  z  (BEQ)
				case 10:
					cc.cmp(ov, 0);
					cc.je(lbl_branch_taken_jit);
					break; // !ov (BNV)
				case 11:
					cc.cmp(ov, 1);
					cc.je(lbl_branch_taken_jit);
					break; //  ov (BV)
				case 12:
					cc.cmp(s, 0);
					cc.je(lbl_branch_taken_jit);
					break; // !s  (BPL)
				case 13:
					cc.cmp(s, 1);
					cc.je(lbl_branch_taken_jit);
					break; //  s  (BMI)
				case 14:
					cc.jmp(lbl_branch_taken_jit);
					branched_unconditionally = true;
					break; // true (BRA)
				case 15:
				default:
					break; // false (no branch)
				}

				// Fallthrough path (branch not taken)
				if (!branched_unconditionally) { // If not BRA or other unconditional branch type
					cc.jmp(fallthrough_label);
				}
				// JIT path diverges. The compiler loop `break`s because both paths are handled by JIT jumps.
				block_terminated_by_jit = true;
				break;
			}
			// OP_POPL potentially changes PC, so it needs careful handling (like a return or indirect jump)
			// If POPL involves PC, it should end the JIT block.
			// Generic instructions are handled below.
			// Default to directly_call_path for unspecialized instructions like POPL.

		directly_call_path: {
			// Prepare environment for CPU member function call
			cc.mov(x86::Mem(reg_cpu, long_imm_off), Imm(imm)); // `imm` is already prepared
			cc.mov(x86::Mem(reg_cpu, hint_off), Imm(opd->hint));

			for (size_t ix = 0; ix != 2; ++ix) {
				if (opd->operands[ix].mask == 0 && opd->operands[ix].register_size == 0)
					continue;

				uint16_t op_field_val = (op >> opd->operands[ix].shift) & opd->operands[ix].mask;

				cc.mov(x86::Mem(reg_cpu, op_operands_off + op_size * ix + op_reg_index_off), Imm(static_cast<uint16_t>(op_field_val)));
				cc.mov(x86::Mem(reg_cpu, op_operands_off + op_size * ix + op_reg_size_off), Imm(static_cast<uint8_t>(opd->operands[ix].register_size)));

				if (opd->operands[ix].register_size == 0) { // Immediate or address, not register
					cc.mov(x86::Mem(reg_cpu, op_operands_off + op_size * ix + op_value_off), Imm(static_cast<uint16_t>(op_field_val)));
				}
				else { // Register operand, load its value
					x86::Gp acc_val = cc.newUInt64("reg_acc_val");
					cc.mov(acc_val, Imm(0));

					x86::Gp reg_base_ptr = cc.newIntPtr("gpr_base");
					cc.lea(reg_base_ptr, x86::Mem(reg_cpu, gpr_off + op_field_val * reg8_size));

					for (size_t bx = 0; bx < opd->operands[ix].register_size; ++bx) {
						x86::Gp byte_val = cc.newUInt8("reg_byte_val");
						cc.mov(byte_val, x86::Mem(reg_base_ptr, bx * reg8_size + reg_off)); // Access .raw field

						x86::Gp temp_qword = cc.newUInt64("temp_qword_val");
						cc.movzx(temp_qword, byte_val); // zero extend byte to qword
						if (bx > 0)
							cc.shl(temp_qword, bx * 8);
						cc.or_(acc_val, temp_qword);
					}
					cc.mov(x86::Mem(reg_cpu, op_operands_off + op_size * ix + op_value_off), acc_val);
				}
			}

			// Clear/setup flags state for C++ handler
			cc.mov(x86::Mem(reg_cpu, flags_changed_off), Imm(static_cast<uint8_t>(0)));
			x86::Gp psw_reg = cc.newUInt8("psw_val_generic");
			cc.mov(psw_reg, x86::Mem(reg_cpu, psw_off + reg_off));
			cc.mov(x86::Mem(reg_cpu, flags_in_off), psw_reg);
			cc.mov(x86::Mem(reg_cpu, flags_out_off), Imm(static_cast<uint8_t>(casioemu::CPU::PSW_Z))); // Default flags_out

			// Call the original C++ handler function
			x86::Gp temp_cpu_ptr = cc.newIntPtr("reg_cpu"); // Ensure reg_cpu is not clobbered if it's also an arg reg
			cc.mov(temp_cpu_ptr, reg_cpu);
#if defined(_WIN64)
			cc.mov(x86::rcx, temp_cpu_ptr);
#else // System V AMD64 ABI (Linux, macOS)
			cc.mov(x86::rdi, temp_cpu_ptr);
#endif
			cc.call(asmjit::Imm(*(size_t*)&opd->handler_function));

			// Update PSW based on flags_changed and flags_out from C++ handler
			x86::Gp flags_changed_val = cc.newUInt8("flags_changed_val");
			x86::Gp flags_out_val = cc.newUInt8("flags_out_val");

			cc.mov(flags_changed_val, x86::Mem(reg_cpu, flags_changed_off));
			cc.mov(flags_out_val, x86::Mem(reg_cpu, flags_out_off));

			// psw = (psw & ~flags_changed) | (flags_out & flags_changed)
			x86::Gp psw_current = cc.newUInt8("psw_current_val");
			cc.mov(psw_current, x86::Mem(reg_cpu, psw_off + reg_off));

			x86::Gp mask_fc = cc.newUInt8("mask_fc");
			cc.mov(mask_fc, flags_changed_val);
			cc.not_(mask_fc);			   // ~flags_changed
			cc.and_(psw_current, mask_fc); // psw & ~flags_changed

			cc.and_(flags_out_val, flags_changed_val); // flags_out & flags_changed
			cc.or_(psw_current, flags_out_val);		   // Combine
			cc.mov(x86::Mem(reg_cpu, psw_off + reg_off), psw_current);

			// Write-back for operands (if H_WB hint)
			if ((opd->hint & casioemu::CPU::OpcodeHint::H_WB) && opd->operands[0].register_size > 0) {
				uint16_t wb_reg_idx = (op >> opd->operands[0].shift) & opd->operands[0].mask;
				x86::Gp wb_val = cc.newUInt64("wb_val");
				cc.mov(wb_val, x86::Mem(reg_cpu, op_operands_off + op_size * 0 + op_value_off)); // Operand 0 value

				x86::Gp wb_reg_base_ptr = cc.newIntPtr("wb_gpr_base");
				cc.lea(wb_reg_base_ptr, x86::Mem(reg_cpu, gpr_off + wb_reg_idx * reg8_size));

				for (size_t bx = 0; bx < opd->operands[0].register_size; ++bx) {
					x86::Gp temp_byte = cc.newUInt8("wb_byte");
					// Extract (bx)-th byte from wb_val
					x86::Gp shifted_val = cc.newUInt64("shifted_wb_val");
					cc.mov(shifted_val, wb_val);
					if (bx > 0)
						cc.shr(shifted_val, bx * 8);
					cc.mov(temp_byte, shifted_val.r8());

					cc.mov(x86::Mem(wb_reg_base_ptr, bx * reg8_size + reg_off), temp_byte);
				}
			}

			// --- Post C++ call: Decide JIT flow ---
			// If it was BL #imm, POPL with PC, B/BL ERm, JIT function should return.
			// C++ handler would have updated PC/CSR/LR appropriately.
			bool is_call_type = (opd->handler_function == &casioemu::CPU::OP_BL); // Includes BL #imm and BL ERm
			bool is_pop_pc = false;
			if (opd->handler_function == &casioemu::CPU::OP_POPL) {
				uint8_t pop_mask = (op >> opd->operands[0].shift) & opd->operands[0].mask;
				if (pop_mask & 2) { // Assuming bit 1 (value 2) in mask means PC is popped
					is_pop_pc = true;
				}
			}
			bool is_indirect_branch_reg = (opd->handler_function == &casioemu::CPU::OP_B && !(opd->hint & casioemu::CPU::OpcodeHint::H_TI));

			if (is_call_type || is_pop_pc || is_indirect_branch_reg) {
				cc.ret();
				block_terminated_by_jit = true;
				break;
			}

			// If not a special terminating instruction and no Delay Slot hint, end JIT block.
			//if (!(opd->hint & casioemu::CPU::OpcodeHint::H_DS)) {
			//	cc.mov(x86::Mem(reg_cpu, pc_off + reg_off), Imm(pc)); // pc is already advanced to next instr
			//	cc.mov(x86::Mem(reg_cpu, csr_off + reg_off), Imm(static_cast<uint8_t>(csr_val >> 16)));
			//	cc.ret();
			//	block_terminated_by_jit = true;
			//	break;
			//}
			// Else (H_DS is set), JIT compilation continues to the next instruction.
		} // End of directly_call_path
		} // End of instruction compilation loop

		if (!block_terminated_by_jit) {
			// Loop finished due to instruction count limit, not an explicit JIT ret/jmp.
			// End block by setting PC/CSR to current (next) instruction and returning.
			cc.mov(x86::Mem(reg_cpu, pc_off + reg_off), Imm(pc));
			cc.mov(x86::Mem(reg_cpu, csr_off + reg_off), Imm(static_cast<uint8_t>(csr_val >> 16)));
			cc.ret();
		}

		cc.endFunc();
		Error err = cc.finalize();
		if (err) {
			// std::cerr << "AsmJit finalization error: " << DebugUtils::errorAsString(err) << std::endl;
			return std::nullopt;
		}

		size_t guest_code_end_address = csr_val | pc;
		return Function(entry_offset, guest_code_end_address, code_ptr->codeSize(), std::move(code_ptr));
	}

	void OnMMUCodeWrite(size_t modified_guest_addr_start, size_t modified_length) {
		size_t modified_guest_addr_end = modified_guest_addr_start + modified_length;

		std::vector<size_t> invalidated_keys;
		for (auto& pair : cached_function) {
			const Function& func = pair.second;
			// Check for overlap: (StartA <= EndB) and (EndA >= StartB)
			if (func.guest_code_start_addr < modified_guest_addr_end &&
				func.guest_code_end_addr > modified_guest_addr_start) {

				invalidated_keys.push_back(pair.first);
				if (func.compiled_func_ptr) {
					rt.release(func.compiled_func_ptr);
				}
			}
		}
		for (size_t key : invalidated_keys) {
			cached_function.erase(key);
			call_count.erase(key);
			condition_check.erase(key); // Reset JIT decision state
		}
	}
};