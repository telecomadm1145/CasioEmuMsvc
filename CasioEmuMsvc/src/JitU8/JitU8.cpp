#include "CPU.hpp"
#include "MMU.hpp"
#include <asmjit/core.h>
#include <asmjit/x86.h>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

using namespace asmjit;

// Threshold for JIT compilation
static const int JIT_CALL_THRESHOLD = 5;
// Max instructions to compile in a single block
static const size_t MAX_JIT_INSTRUCTIONS_PER_BLOCK = 128; // Reduced from 4096 for practical block sizes

namespace detail {
	// Accepts uint8/16/32/64 – the loop stops once the LSB is 1.
	template <typename T>
	constexpr unsigned lsb_pos(T mask) noexcept {
		unsigned pos = 0;
		while ((mask & T{1}) == 0 && pos < sizeof(T) * 8u) {
			mask >>= 1;
			++pos;
		}
		return pos;
	}
} // namespace detail

// -----------------------------------------------------------------------------
//  PSW bit positions (shift values)
//  They are derived from the masks already defined in casioemu::CPU
// -----------------------------------------------------------------------------
static constexpr unsigned PSW_C_shift = detail::lsb_pos((int)casioemu::CPU::PSW_C);
static constexpr unsigned PSW_Z_shift = detail::lsb_pos((int)casioemu::CPU::PSW_Z);
static constexpr unsigned PSW_S_shift = detail::lsb_pos((int)casioemu::CPU::PSW_S);
static constexpr unsigned PSW_OV_shift = detail::lsb_pos((int)casioemu::CPU::PSW_OV);

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
				if (err != asmjit::Error::kOk) {
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
			if (err != asmjit::Error::kOk) {
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
		code_ptr->init(rt.environment(), rt.cpu_features());
		x86::Compiler cc(code_ptr.get());

		auto func_sig = FuncSignature::build<void, void*>(); // void JIT_Func(CPU* cpu_ptr);
		FuncNode* func_node = cc.add_func(func_sig);

		x86::Gp reg_cpu = cc.new_gpz("reg_cpu"); // To hold the CPU* argument
		func_node->set_arg(0, reg_cpu);

		uint16_t pc = entry_offset & 0xFFFF;
		uint32_t csr_val = entry_offset & 0xFF0000; // Full CSR shifted value e.g. 0x0F0000

		// CPU struct member offsets
		auto reg_off = offsetof(casioemu::CPU::reg8_t, raw);
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
		Label entry_label = cc.new_label();
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
				Label new_label = cc.new_label();
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
				jump_map[next_sequential_addr] = cc.new_label();
			}
			Label fallthrough_label = jump_map[next_sequential_addr];

			// --- Instruction Handling ---
			if (opd->handler_function == &casioemu::CPU::OP_RT ||
				opd->handler_function == &casioemu::CPU::OP_RTI) {
				x86::Gp gp1 = cc.new_gp16("rt_lr_pc");
				cc.mov(gp1, x86::Mem(reg_cpu, lr_off + reg_off)); // Assuming reg_lr is reg16_t, access its .raw
				cc.mov(x86::Mem(reg_cpu, pc_off + reg_off), gp1);

				x86::Gp gp2 = cc.new_gp8("rt_lcsr_csr");
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
								jump_map[target_addr] = cc.new_label();
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
				// -----------------------------------------------------------------------------
				//  Branch on Condition (BC  xxxx_xxxx)       – Optimised JIT emission
				// -----------------------------------------------------------------------------

				// -------------------------------------------------
				// 1. Compute branch target -------------------------------------------------
				int8_t disp_s8 = static_cast<int8_t>((op >> opd->operands[0].shift) &
													 opd->operands[0].mask);
				int16_t offset = static_cast<int16_t>(disp_s8) << 1; // sign-ext *2
				uint16_t tgtPC = (pc + offset) & 0xFFFF;
				size_t tgtAbs = csr_val | tgtPC;

				// Create / reuse label for the “taken” path
				Label lblTaken;
				if (!jump_map.count(tgtAbs))
					jump_map[tgtAbs] = cc.new_label();
				lblTaken = jump_map[tgtAbs];

				const uint8_t cond = (op >> 8) & 0x0F; // 0 … 15
				// BRA (cond==14) is unconditional → fast path
				if (cond == 14) {
					cc.jmp(lblTaken);
					block_terminated_by_jit = true; // block ends here
					break;
				}

				// -------------------------------------------------
				// 2. Emit condition check -------------------------
				//    Simple conditions use only TEST + Jcc.
				//    Complex ones (LTS, LES) need one tiny helper.
				// -------------------------------------------------

				// Bring PSW once into an 8-bit register
				x86::Gp psw = cc.new_gp8("psw");
				cc.mov(psw, x86::Mem(reg_cpu, psw_off + reg_off));

				/*  Condition LUT
				 *  0  !C      (BC)
				 *  1   C      (BNC)
				 *  2  !LE = !(C|Z)  (BH)
				 *  3   LE =  (C|Z)  (BLS)
				 *  4  !LTS           (BGE)     -> needs XOR(OV,S)
				 *  5   LTS           (BLT)
				 *  6  !LES           (BGT)     -> needs (LTS|Z)
				 *  7   LES           (BLE)
				 *  8  !Z   (BNE)
				 *  9   Z   (BEQ)
				 * 10  !OV  (BNV)
				 * 11   OV  (BV)
				 * 12  !S   (BPL)
				 * 13   S   (BMI)
				 * 14  BRA  (handled above)
				 * 15  false (never branch)
				 *
				 *  For the “simple” ones we can describe them as:
				 *      (psw & mask) == 0  ?  JccTaken_ifZero :  JccTaken_ifNonZero
				 */
				struct CondInfo {
					uint8_t mask;
					bool branchIfNonZero;
				};
				static constexpr CondInfo lut[14] = {
					{casioemu::CPU::PSW_C, false},						  // 0 !C
					{casioemu::CPU::PSW_C, true},						  // 1  C
					{casioemu::CPU::PSW_C | casioemu::CPU::PSW_Z, false}, // 2 !LE
					{casioemu::CPU::PSW_C | casioemu::CPU::PSW_Z, true},  // 3  LE
					{0, false},											  // 4 !LTS (special)
					{0, true},											  // 5  LTS  (special)
					{0, false},											  // 6 !LES (special)
					{0, true},											  // 7  LES  (special)
					{casioemu::CPU::PSW_Z, false},						  // 8 !Z
					{casioemu::CPU::PSW_Z, true},						  // 9  Z
					{casioemu::CPU::PSW_OV, false},						  // 10 !OV
					{casioemu::CPU::PSW_OV, true},						  // 11  OV
					{casioemu::CPU::PSW_S, false},						  // 12 !S
					{casioemu::CPU::PSW_S, true}						  // 13  S
				};

				if (cond <= 13 && cond != 4 && cond != 5 && cond != 6 && cond != 7) {
					// ---------- Simple bit/union test ------------------------------------
					const CondInfo& ci = lut[cond];
					cc.test(psw, ci.mask); // sets ZF
					if (ci.branchIfNonZero)
						cc.jnz(lblTaken); // … mask != 0
					else
						cc.jz(lblTaken); // … mask == 0
				}
				else {
					// ---------- Complex conditions (LTS / LES) ---------------------------
					// LTS  = OV ^ S
					// LES  = (OV ^ S) | Z
					// ->  tmp = (psw & (OV|S)) ; tmp = ((tmp >> ovShift) ^ (tmp >> sShift)) & 1
					// Using 8-bit regs keeps the sequence small.
					x86::Gp tmp = cc.new_gp8("tmp");

					switch (cond) {
					case 4: // !LTS
					case 5: //  LTS
						cc.mov(tmp, psw);
						// tmp = ((OV>>ovShift) ^ (S>>sShift)) & 1
						cc.shr(tmp, PSW_OV_shift);		// carry OV into bit0
						cc.mov(x86::Gp(tmp.r8()), psw); // reload psw into tmp.lo
						cc.shr(tmp, PSW_S_shift);		// carry S into bit0
						cc.xor_(tmp.r8(), tmp);			// OV ^ S  in bit0
						cc.and_(tmp.r8(), 1);
						if (cond == 5)
							cc.jnz(lblTaken);
						else
							cc.jz(lblTaken);
						break;

					case 6: // !LES
					case 7: //  LES
						// les = lts | z
						// Step1: compute lts into tmp
						cc.mov(tmp, psw);
						cc.shr(tmp, PSW_OV_shift);
						cc.mov(x86::Gp(tmp.r8()), psw);
						cc.shr(tmp, PSW_S_shift);
						cc.xor_(tmp.r8(), tmp);
						cc.and_(tmp.r8(), 1); // bit0 = lts

						// Step2: OR with Z
						cc.test(psw, casioemu::CPU::PSW_Z);
						cc.setnz(tmp.r8()); // tmp = z ? 1 : lts
						if (cond == 7)
							cc.jnz(lblTaken);
						else
							cc.jz(lblTaken);
						break;

					default:
						break; // should never reach here
					}
				}

				// -------------------------------------------------
				// 3. Fall-through path ----------------------------
				cc.jmp(fallthrough_label);

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
			cc.mov(x86::Mem(reg_cpu, pc_off + reg_off), Imm(pc));
			cc.mov(x86::Mem(reg_cpu, csr_off + reg_off), Imm(uint8_t(csr_val >> 16)));

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
					x86::Gp acc_val = cc.new_gp64("reg_acc_val");
					cc.mov(acc_val, Imm(0));

					x86::Gp reg_base_ptr = cc.new_gpz("gpr_base");
					cc.lea(reg_base_ptr, x86::Mem(reg_cpu, gpr_off + op_field_val * reg8_size));

					for (size_t bx = 0; bx < opd->operands[ix].register_size; ++bx) {
						x86::Gp byte_val = cc.new_gp8("reg_byte_val");
						cc.mov(byte_val, x86::Mem(reg_base_ptr, bx * reg8_size + reg_off)); // Access .raw field

						x86::Gp temp_qword = cc.new_gp64("temp_qword_val");
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
			x86::Gp psw_reg = cc.new_gp8("psw_val_generic");
			cc.mov(psw_reg, x86::Mem(reg_cpu, psw_off + reg_off));
			cc.mov(x86::Mem(reg_cpu, flags_in_off), psw_reg);
			cc.mov(x86::Mem(reg_cpu, flags_out_off), Imm(static_cast<uint8_t>(casioemu::CPU::PSW_Z))); // Default flags_out

			// Call the original C++ handler function
			x86::Gp temp_cpu_ptr = cc.new_gpz("reg_cpu_tmp"); // Ensure reg_cpu is not clobbered if it's also an arg reg

			// TODO: ???
			cc.mov(temp_cpu_ptr, reg_cpu);
#if defined(_WIN64)
			cc.mov(x86::rcx, temp_cpu_ptr);
#else // System V AMD64 ABI (Linux, macOS)
			cc.mov(x86::rdi, temp_cpu_ptr);
#endif
			cc.call(asmjit::Imm(*(size_t*)&opd->handler_function));

			// Update PSW based on flags_changed and flags_out from C++ handler
			x86::Gp flags_changed_val = cc.new_gp8("flags_changed_val");
			x86::Gp flags_out_val = cc.new_gp8("flags_out_val");

			cc.mov(flags_changed_val, x86::Mem(reg_cpu, flags_changed_off));
			cc.mov(flags_out_val, x86::Mem(reg_cpu, flags_out_off));

			// psw = (psw & ~flags_changed) | (flags_out & flags_changed)
			x86::Gp psw_current = cc.new_gp8("psw_current_val");
			cc.mov(psw_current, x86::Mem(reg_cpu, psw_off + reg_off));

			x86::Gp mask_fc = cc.new_gp8("mask_fc");
			cc.mov(mask_fc, flags_changed_val);
			cc.not_(mask_fc);			   // ~flags_changed
			cc.and_(psw_current, mask_fc); // psw & ~flags_changed

			cc.and_(flags_out_val, flags_changed_val); // flags_out & flags_changed
			cc.or_(psw_current, flags_out_val);		   // Combine
			cc.mov(x86::Mem(reg_cpu, psw_off + reg_off), psw_current);

			// Write-back for operands (if H_WB hint)
			if ((opd->hint & casioemu::CPU::OpcodeHint::H_WB) && opd->operands[0].register_size > 0) {
				uint16_t wb_reg_idx = (op >> opd->operands[0].shift) & opd->operands[0].mask;
				x86::Gp wb_val = cc.new_gp64("wb_val");
				cc.mov(wb_val, x86::Mem(reg_cpu, op_operands_off + op_size * 0 + op_value_off)); // Operand 0 value

				x86::Gp wb_reg_base_ptr = cc.new_gpz("wb_gpr_base");
				cc.lea(wb_reg_base_ptr, x86::Mem(reg_cpu, gpr_off + wb_reg_idx * reg8_size));

				for (size_t bx = 0; bx < opd->operands[0].register_size; ++bx) {
					x86::Gp temp_byte = cc.new_gp8("wb_byte");
					// Extract (bx)-th byte from wb_val
					x86::Gp shifted_val = cc.new_gp64("shifted_wb_val");
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

		} // End of directly_call_path
		} // End of instruction compilation loop

		if (!block_terminated_by_jit) {
			// Loop finished due to instruction count limit, not an explicit JIT ret/jmp.
			// End block by setting PC/CSR to current (next) instruction and returning.
			cc.mov(x86::Mem(reg_cpu, pc_off + reg_off), Imm(pc));
			cc.mov(x86::Mem(reg_cpu, csr_off + reg_off), Imm(static_cast<uint8_t>(csr_val >> 16)));
			cc.ret();
		}

		cc.end_func();
		Error err = cc.finalize();
		if (err != asmjit::Error::kOk) {
			// std::cerr << "AsmJit finalization error: " << DebugUtils::errorAsString(err) << std::endl;
			return std::nullopt;
		}

		size_t guest_code_end_address = csr_val | pc;
		return Function(entry_offset, guest_code_end_address, code_ptr->code_size(), std::move(code_ptr));
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