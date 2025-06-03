#pragma once
#include "Chipset.hpp"
#include "MMU.hpp"
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <cstdio>
namespace casioemu {
	// Custom byteswap function for uint16_t
	inline uint16_t byteswap_ushort(uint16_t x) {
		return ((x & 0xFF00) >> 8) | ((x & 0x00FF) << 8);
	}

	class ePSCPU {
	public:
		char regs[0x80]{};
		char wbk[0x80]{};
		// char ram[64 * 0x80];
		// char rom[0x20000];

		casioemu::MMU& mmu;
		ePSCPU(casioemu::MMU& mmu) : mmu(mmu) {
		}

		char vram[0x2000]{};
		struct SFRs {
			enum {
				INDF0,
				FSR0,
				BSR,

				INDF1,
				FSR1,
				BSR1,

				STKPTR,

				PCL,
				PCM,
				PCH,

				ACC,

				TABPTRL,
				TABPTRM,
				TABPTRH,

				LCDDATA,

				STATUS,

				INDF2,
				FSR2,
				BSR2,

				CPUCON = 0x20,
				POST_ID,

				LCDARL,
				LCDARH,

				ZERO_REG = 0x2f,
			};
		};

		char rdata = 0;

		// 单位是word
		uint32_t stack[256]{};

		uint32_t pc{};
		// 单位是byte
		uint32_t tabptr{};

		uint32_t lcd_ptr{};

		uint8_t PCL() {
			return pc;
		}
		uint8_t PCM() {
			return (pc >> 8);
		}
		uint8_t PCH() {
			return (pc >> 16) & 1;
		}

		uint8_t TABPTRL() {
			return tabptr;
		}
		uint8_t TABPTRM() {
			return (tabptr >> 8);
		}
		uint8_t TABPTRH() {
			return (tabptr >> 16) & 11;
		}

		void setPCL(uint8_t value) {
			pc = (pc & 0x1ff00) | (value);
		}

		void setPCM(uint8_t value) {
			pc = (pc & 0x100ff) | ((value) << 8);
		}

		void setPCH(uint8_t value) {
			pc = (pc & 0x0ffff) | ((value & 1) << 16);
		}

		void setTABPTRL(uint8_t value) {
			tabptr = (tabptr & 0x3ff00) | value;
		}

		void setTABPTRM(uint8_t value) {
			tabptr = (tabptr & 0x300ff) | ((value) << 8);
		}

		void setTABPTRH(uint8_t value) {
			tabptr = (tabptr & 0x30000) | ((value & 11) << 16);
		}

		// uint16_t PC() {
		//	return (regs[SFRs::PCL] & 0x7f) | ((regs[SFRs::PCM] & 0x7f) << 7) | ((regs[SFRs::PCH] & 1) << 15);
		// }
		// void SetPC(uint16_t pc) {

		//}
		// uint32_t TABPTR() {
		//	return (regs[SFRs::PCL]) | ((regs[SFRs::PCM] & 0x7f) << 8) | ((regs[SFRs::PCH] & 1) << 16);
		//}
		// void SetTABPTR(uint32_t tabptr) {

		//}
		uint8_t& STKPTR() {
			return (uint8_t&)regs[SFRs::STKPTR];
		}
		uint8_t& ACC() {
			return (uint8_t&)regs[SFRs::ACC];
		}
		uint8_t& FSR0() {
			return (uint8_t&)regs[SFRs::FSR0];
		}
		uint8_t& BSR() {
			return (uint8_t&)regs[SFRs::BSR];
		}
		uint8_t& FSR1() {
			return (uint8_t&)regs[SFRs::FSR1];
		}
		uint8_t& BSR1() {
			return (uint8_t&)regs[SFRs::BSR1];
		}
		uint8_t& FSR2() {
			return (uint8_t&)regs[SFRs::FSR2];
		}
		uint8_t& BSR2() {
			return (uint8_t&)regs[SFRs::BSR2];
		}

		// STATUS Register Bits (from Sleigh bitrange)
		enum StatusBits : uint8_t {
			C_BIT = 1 << 0,
			DC_BIT = 1 << 1,
			Z_BIT = 1 << 2,
			OV_BIT = 1 << 3,
			SLE_BIT = 1 << 4,
			SGE_BIT = 1 << 5,
			PD_BIT = 1 << 6,
			TO_BIT = 1 << 7,
		};

		// CPUCON Register Bits
		enum CPUCONBits : uint8_t {
			MS0_BIT = 1 << 0,
			MS1_BIT = 1 << 1,
			GLINT_BIT = 1 << 2,
			WBK_BIT = 1 << 7,
		};

		// POST_ID Bits
		enum PostIDBits : uint8_t {
			FSR0_PE_BIT = 1 << 0,
			FSR1_PE_BIT = 1 << 1,
			LCD_PE_BIT = 1 << 2,
			FSR2_PE_BIT = 1 << 3,
			FSR0_ID_BIT = 1 << 4,
			FSR1_ID_BIT = 1 << 5,
			LCD_ID_BIT = 1 << 6,
			FSR2_ID_BIT = 1 << 7,
		};

		// --- Flag Helpers ---
		bool getStatusBit(StatusBits bit) const { return (regs[SFRs::STATUS] & bit) != 0; }
		void setStatusBit(StatusBits bit, bool value) {
			if (value)
				regs[SFRs::STATUS] |= bit;
			else
				regs[SFRs::STATUS] &= ~bit;
		}
		bool getCFlag() const { return getStatusBit(C_BIT); }
		void setCFlag(bool v) { setStatusBit(C_BIT, v); }
		bool getDCFlag() const { return getStatusBit(DC_BIT); }
		void setDCFlag(bool v) { setStatusBit(DC_BIT, v); }
		bool getZFlag() const { return getStatusBit(Z_BIT); }
		void setZFlag(bool v) { setStatusBit(Z_BIT, v); }
		void ZCheck(uint8_t result) { setZFlag(result == 0); }
		bool getOVFlag() const { return getStatusBit(OV_BIT); }
		void setOVFlag(bool v) { setStatusBit(OV_BIT, v); }
		bool getSLEFlag() const { return getStatusBit(SLE_BIT); }
		void setSLEFlag(bool v) { setStatusBit(SLE_BIT, v); }
		void updateSLEFlag(int8_t result) { setSLEFlag(result <= 0); } // Signed comparison
		bool getSGEFlag() const { return getStatusBit(SGE_BIT); }
		void setSGEFlag(bool v) { setStatusBit(SGE_BIT, v); }
		void updateSGEFlag(int8_t result) { setSGEFlag(result >= 0); } // Signed comparison
		bool getPDFlag() const { return getStatusBit(PD_BIT); }
		void setPDFlag(bool v) { setStatusBit(PD_BIT, v); }
		bool getTOFlag() const { return getStatusBit(TO_BIT); }
		void setTOFlag(bool v) { setStatusBit(TO_BIT, v); }

		bool getCPUCONBit(CPUCONBits bit) const { return (regs[SFRs::CPUCON] & bit) != 0; }
		void setCPUCONBit(CPUCONBits bit, bool value) {
			if (value)
				regs[SFRs::CPUCON] |= bit;
			else
				regs[SFRs::CPUCON] &= ~bit;
		}
		bool getWBKFlag() const { return getCPUCONBit(WBK_BIT); }
		bool getGLINTFlag() const { return getCPUCONBit(GLINT_BIT); }
		void setGLINTFlag(bool v) { setCPUCONBit(GLINT_BIT, v); }

		bool getPostIDBit(uint8_t reg_addr, PostIDBits bit) const {
			if (reg_addr == SFRs::POST_ID)
				return (regs[SFRs::POST_ID] & bit) != 0;
			return false; // Should not happen if called correctly
		}

		// Specific getters for POST_ID bits used in FSR post-inc/dec
		bool getFSR0_PE() const { return (static_cast<uint8_t>(regs[SFRs::POST_ID]) & PostIDBits::FSR0_PE_BIT) != 0; }
		bool getFSR0_ID() const { return (static_cast<uint8_t>(regs[SFRs::POST_ID]) & PostIDBits::FSR0_ID_BIT) != 0; }
		bool getFSR1_PE() const { return (static_cast<uint8_t>(regs[SFRs::POST_ID]) & PostIDBits::FSR1_PE_BIT) != 0; }
		bool getFSR1_ID() const { return (static_cast<uint8_t>(regs[SFRs::POST_ID]) & PostIDBits::FSR1_ID_BIT) != 0; }
		bool getFSR2_PE() const { return (static_cast<uint8_t>(regs[SFRs::POST_ID]) & PostIDBits::FSR2_PE_BIT) != 0; }
		bool getFSR2_ID() const { return (static_cast<uint8_t>(regs[SFRs::POST_ID]) & PostIDBits::FSR2_ID_BIT) != 0; }
		bool getLCD_PE() const { return (static_cast<uint8_t>(regs[SFRs::POST_ID]) & PostIDBits::LCD_PE_BIT) != 0; }
		bool getLCD_ID() const { return (static_cast<uint8_t>(regs[SFRs::POST_ID]) & PostIDBits::LCD_ID_BIT) != 0; }

		// --- Memory Access Helpers (implementing Sleigh readDat/writeDat logic) ---
		uint16_t FetchInst() {
			pc &= 0xffff;
			return byteswap_ushort(mmu.ReadCode((size_t)(pc++) << 1));
		}
		uint16_t FetchWord() {
			pc &= 0xffff;
			return (mmu.ReadCode((size_t)(pc++) << 1));
		}
		uint16_t FetchWord2() {
			pc &= 0xffff;
			return (mmu.ReadCode((size_t)(pc++) << 1));
		}
		// Internal read without post-increment for MOV defgh, reg8 type instructions
		uint8_t _internal_read_reg_direct(uint8_t addr) {
			if (addr < 0x80)
				return static_cast<uint8_t>(regs[addr]);
			// This version is for direct access only, not banked GPRs via FSR interpretation
			fprintf(stderr, "Error: _internal_read_reg_direct called for banked addr 0x%02x\n", addr);
			return 0xFF;
		}

		void _internal_write_reg_direct(uint8_t addr, uint8_t value) {
			if (addr < 0x80)
				regs[addr] = static_cast<char>(value);
			else {
				fprintf(stderr, "Error: _internal_write_reg_direct called for banked addr 0x%02x\n", addr);
			}
		}

#define _post_id_handle_indf(x) \
    if (original_rptr_for_post_id == SFRs::INDF##x) { \
        if (getFSR##x##_PE()) { \
            uint8_t temp_FSR##x = static_cast<uint8_t>(regs[SFRs::FSR##x]); \
            if (getFSR##x##_ID()) \
                regs[SFRs::FSR##x] = static_cast<char>(temp_FSR##x + 1); \
            else \
                regs[SFRs::FSR##x] = static_cast<char>(temp_FSR##x - 1); \
        } \
    }

#define _post_id_handle_lcddata(x) \
    if (original_rptr_for_post_id == SFRs::LCDDATA) { \
        if (getLCD_PE()) { \
            uint8_t temp = static_cast<uint8_t>(regs[SFRs::LCDARL]); \
            if (getLCD_ID()) \
                regs[SFRs::LCDARL] = static_cast<char>(temp + 1); \
            else \
                regs[SFRs::LCDARL] = static_cast<char>(temp - 1); \
        } \
    }

#define _post_id_handle_indf2(x)                                                    \
	if (original_rptr_for_post_id == SFRs::INDF##x) {                               \
		if (getFSR##x##_PE()) {                                                     \
			uint8_t temp = static_cast<uint8_t>(regs[SFRs::FSR##x]);              \
			if (getFSR##x##_ID()) {                                                 \
				if (temp == 0x7f)                                                   \
					++BSR##x();                                                     \
				regs[SFRs::FSR##x] = static_cast<char>(((temp + 1) & 0x7F) | 0x80); \
			}                                                                       \
			else {                                                                  \
				if (temp == 0)                                                      \
					--BSR##x();                                                     \
				regs[SFRs::FSR##x] = static_cast<char>(((temp - 1) & 0x7F) | 0x80); \
			}                                                                       \
		}                                                                           \
	}
		// Simulates Sleigh readDat and stores result in rdata_temp
		void
		ReadDat(uint8_t rptr_arg) {
			uint8_t current_ptr_val = rptr_arg;
			uint8_t current_bsr_val = static_cast<uint8_t>(regs[SFRs::BSR]);
			uint8_t original_rptr_for_post_id = rptr_arg;
			bool is_indirect = false;

			// Resolve indirection
			if (rptr_arg == SFRs::INDF0) {
				current_ptr_val = static_cast<uint8_t>(regs[SFRs::FSR0]);
				is_indirect = true;
			}
			else if (rptr_arg == SFRs::INDF1) {
				current_ptr_val = static_cast<uint8_t>(regs[SFRs::FSR1]);
				current_bsr_val = static_cast<uint8_t>(regs[SFRs::BSR1]);
				is_indirect = true;
			}
			else if (rptr_arg == SFRs::INDF2) {
				current_ptr_val = static_cast<uint8_t>(regs[SFRs::FSR2]);
				current_bsr_val = static_cast<uint8_t>(regs[SFRs::BSR2]);
				is_indirect = true;
			}
			if (current_ptr_val < 0x80) {
				if (current_ptr_val == SFRs::ZERO_REG) {
					rdata = 0;
				}
				else if ((current_ptr_val >= 0x25 && current_ptr_val <= 0x3f) && getWBKFlag()) {
					rdata = wbk[current_ptr_val];
				}
				else if (current_ptr_val == SFRs::PCL) {
					rdata = PCL();
				}
				else if (current_ptr_val == SFRs::PCM) {
					rdata = PCM();
				}
				else if (current_ptr_val == SFRs::PCH) {
					rdata = PCH();
				}
				else if (current_ptr_val == SFRs::TABPTRL) {
					rdata = TABPTRL();
				}
				else if (current_ptr_val == SFRs::TABPTRM) {
					rdata = TABPTRM();
				}
				else if (current_ptr_val == SFRs::TABPTRH) {
					rdata = TABPTRH();
				}
				else if (current_ptr_val == SFRs::LCDDATA) {
					auto low = regs[SFRs::LCDARL] & 0x63;
					if (low <= 0x61) {
						auto high = regs[SFRs::LCDARH] & 0x3;
						rdata = vram[high * 98 + low];
					}
					else {
						rdata = 0;
					}
				}
				else {
					rdata = regs[current_ptr_val];
				}
			}
			else {
				uint16_t effective_addr = (static_cast<uint16_t>(current_bsr_val & 63) << 7) | (current_ptr_val & 0x7F);
				rdata = mmu.ReadData(effective_addr);
			}

			if (is_indirect) {
				_post_id_handle_indf(0) else _post_id_handle_indf2(1) else _post_id_handle_indf2(2) else _post_id_handle_lcddata(0);
			}
		}

		void WriteDat(uint8_t rptr_arg, uint8_t value_to_write) {
			uint8_t current_ptr_val = rptr_arg;
			uint8_t current_bsr_val = static_cast<uint8_t>(regs[SFRs::BSR]);
			uint8_t original_rptr_for_post_id = rptr_arg;
			bool is_indirect = false;

			// Resolve indirection
			if (rptr_arg == SFRs::INDF0) {
				current_ptr_val = static_cast<uint8_t>(regs[SFRs::FSR0]);
				is_indirect = true;
			}
			else if (rptr_arg == SFRs::INDF1) {
				current_ptr_val = static_cast<uint8_t>(regs[SFRs::FSR1]);
				current_bsr_val = static_cast<uint8_t>(regs[SFRs::BSR1]);
				is_indirect = true;
			}
			else if (rptr_arg == SFRs::INDF2) {
				current_ptr_val = static_cast<uint8_t>(regs[SFRs::FSR2]);
				current_bsr_val = static_cast<uint8_t>(regs[SFRs::BSR2]);
				is_indirect = true;
			}

			if (current_ptr_val == SFRs::ZERO_REG) {
			}
			else if (current_ptr_val < 0x80) {
				if ((current_ptr_val >= 0x25 && current_ptr_val <= 0x3f) && getWBKFlag()) {
					wbk[current_ptr_val] = static_cast<char>(value_to_write);
				}
				else if (current_ptr_val == SFRs::PCL) {
					setPCL(value_to_write);
				}
				else if (current_ptr_val == SFRs::PCM) {
					setPCM(value_to_write);
				}
				else if (current_ptr_val == SFRs::PCH) {
					setPCH(value_to_write);
				}
				else if (current_ptr_val == SFRs::TABPTRL) {
					setTABPTRL(value_to_write);
				}
				else if (current_ptr_val == SFRs::TABPTRM) {
					setTABPTRM(value_to_write);
				}
				else if (current_ptr_val == SFRs::TABPTRH) {
					setTABPTRH(value_to_write);
				}
				else if (current_ptr_val == SFRs::LCDDATA) {
					auto low = regs[SFRs::LCDARL] & 0x63;
					if (low <= 0x61) {
						auto high = regs[SFRs::LCDARH] & 0x3;
						vram[high * 98 + low] = value_to_write;
					}
				}
				else {
					regs[current_ptr_val] = static_cast<char>(value_to_write);
				}
			}
			else {
				uint16_t effective_addr = (static_cast<uint16_t>(current_bsr_val & 63) << 7) | (current_ptr_val & 0x7F);
				mmu.WriteData(effective_addr, static_cast<char>(value_to_write));
			}

			if (is_indirect) {
				_post_id_handle_indf(0) else _post_id_handle_indf2(1) else _post_id_handle_indf2(2) else _post_id_handle_lcddata(0);
			}
		}

		// --- Arithmetic Helper Macros from Sleigh ---
		void AddWithFlags(uint8_t& dest, uint8_t val_b) {
			uint8_t val_a = dest;
			uint16_t result16 = static_cast<uint16_t>(val_a) + val_b;
			uint8_t result8 = static_cast<uint8_t>(result16);

			setCFlag(result16 > 0xFF);
			// DC for ADD: carry from bit 3 to bit 4
			setDCFlag(((val_a & 0x0F) + (val_b & 0x0F)) > 0x0F);
			// OV: overflow if signs of operands are same and sign of result is different
			setOVFlag(((val_a ^ val_b) & 0x80) == 0 && ((val_a ^ result8) & 0x80) != 0);

			dest = result8;
			ZCheck(dest);
			updateSLEFlag(static_cast<int8_t>(dest));
			updateSGEFlag(static_cast<int8_t>(dest));
		}

		void AdcWithFlags(uint8_t& dest, uint8_t val_b) { // Used by ADC, ADDDC
			uint8_t val_a = dest;
			uint8_t c_in = getCFlag() ? 1 : 0;
			uint16_t result16 = static_cast<uint16_t>(val_a) + val_b + c_in;
			uint8_t result8 = static_cast<uint8_t>(result16);

			// For ADC, DC flag logic is more complex or sometimes not affected standardly
			// Sleigh shows `add(ACC,rdata)` for ADC, so DC based on ACC+rdata only.
			// For simplicity here, use standard DC based on a+b+c_in.
			// A more accurate DC for PIC-like ADC might be (A&0xf)+(B&0xf)+C_in_to_nibble > 0xf
			// However, the Sleigh for ADD and ADC is identical `add(ACC,rdata)`. This implies C is handled by the `add` pcode if it were a true pcodeop.
			// Since our `add` Sleigh macro doesn't use C, we need to add C for ADC.
			// Let's stick to the Sleigh `ADD` macro for `ADD` and `ADC` which is:
			// C = carry(opa, opb); DC = C; OV = scarry(opa, opb); opa = opa + opb; SLE/SGE/Z
			// This means for ADC, the C flag is an *input* to the operation, but the Sleigh `add` macro *recalculates* C.
			// This implies the Sleigh definition of `add` for `ADC` might be simplified or expects C to be added *before* calling `add`.
			// Let's assume ADC means ACC = ACC + data + C_flag
			uint16_t result16_adc = static_cast<uint16_t>(val_a) + val_b + (getCFlag() ? 1 : 0);
			uint8_t result8_adc = static_cast<uint8_t>(result16_adc);

			bool old_c = getCFlag(); // Preserve for DC calc if needed, or assume DC is based on A+B part

			setCFlag(result16_adc > 0xFF);
			setDCFlag(((val_a & 0x0F) + (val_b & 0x0F) + (old_c ? 1 : 0)) > 0x0F);			 // DC for A+B+C
			setOVFlag(((val_a ^ val_b) & 0x80) == 0 && ((val_a ^ result8_adc) & 0x80) != 0); // OV for A+B part for simplicity
																							 // or ((val_a ^ (val_b+old_c)) & 0x80) == 0 ...

			dest = result8_adc;
			ZCheck(dest);
			updateSLEFlag(static_cast<int8_t>(dest));
			updateSGEFlag(static_cast<int8_t>(dest));
		}

		void SubWithFlags(uint8_t& dest, uint8_t val_b) { // dest = dest - val_b
			uint8_t val_a = dest;
			uint16_t result16 = static_cast<uint16_t>(val_a) - val_b; // Effectively val_a + (~val_b) + 1
			uint8_t result8 = static_cast<uint8_t>(result16);

			// C for SUB: set if no borrow (A >= B), clear if borrow (A < B)
			// Sleigh `sub(opa,opb)` is `add(opa, opb ^ 0xff)`. Then C becomes carry of A + ~B.
			// If A + ~B has carry, it means A - B has no borrow. So C is carry(A, ~B).
			// C = carry(A, ~B) = ( (uint16_t)A + (uint8_t)(~val_b) ) > 0xFF (this is for A + ~B)
			// For A - B, C is borrow: A < B.
			setCFlag(val_a < val_b); // Set if borrow occurred. Sleigh's SUB might use inverted C.
									 // The Sleigh `sub` uses `add(opa, opb ^ 0xff)` and then its `add` sets C normally.
									 // So C for SUB will be `carry(A, ~B)`. This is equivalent to `!(A < B)`.
									 // Let's follow Sleigh's `add(opa, opb ^ 0xff)`:
			uint8_t val_b_inv = ~val_b;
			uint16_t inter_sum = static_cast<uint16_t>(val_a) + val_b_inv; // This is A + ~B for the C flag from add macro
			// AddWithFlags(dest, val_b_inv + 1); // This is A - B
			// This gets complicated. Let's use direct subtraction logic for flags.

			// Standard SUB flags:
			setCFlag(static_cast<uint16_t>(val_a) < static_cast<uint16_t>(val_b)); // C is BORROW
			// DC for SUB: borrow from bit 3 to bit 4
			setDCFlag((val_a & 0x0F) < (val_b & 0x0F)); // DC is BORROW from nibble
			// OV: overflow if signs of operands are different and sign of result is different from minuend
			setOVFlag(((val_a ^ val_b) & 0x80) != 0 && ((val_a ^ result8) & 0x80) != 0);

			dest = result8;
			ZCheck(dest);
			updateSLEFlag(static_cast<int8_t>(dest));
			updateSGEFlag(static_cast<int8_t>(dest));
		}

		void SubbWithFlags(uint8_t& dest, uint8_t val_b) { // dest = dest - val_b - C
			uint8_t val_a = dest;
			uint8_t c_in = getCFlag() ? 1 : 0; // C is borrow flag here
			uint16_t val_b_plus_c = static_cast<uint16_t>(val_b) + c_in;
			uint16_t result16 = static_cast<uint16_t>(val_a) - val_b_plus_c;
			uint8_t result8 = static_cast<uint8_t>(result16);

			// C for SUBB: borrow
			setCFlag(static_cast<uint16_t>(val_a) < val_b_plus_c);
			// DC for SUBB: borrow from nibble
			setDCFlag((val_a & 0x0F) < ((val_b & 0x0F) + c_in)); // Approximation, careful with c_in carrying into nibble sum

			setOVFlag(((val_a ^ val_b) & 0x80) != 0 && ((val_a ^ result8) & 0x80) != 0); // Approx.

			dest = result8;
			ZCheck(dest);
			updateSLEFlag(static_cast<int8_t>(dest));
			updateSGEFlag(static_cast<int8_t>(dest));
		}

		bool running = 0;

		// --- P-Code Op Stubs ---
		void pcode_nop() { /* No operation */ }
		void pcode_sleep() {
			printf("PCODE: SLEEP\n");
			running = 0; /* TODO: Halt CPU, wait for interrupt */
		}
		void pcode_halt() {
			printf("PCODE: HALT\n");
			running = 0; /* TODO: Halt CPU */
		}
		void pcode_sfr4() { /*std::cout << "PCODE: SFR4 (not implemented)" << std::endl; *//* TODO */ }
		void pcode_sfl4() { /*std::cout << "PCODE: SFL4 (not implemented)" << std::endl;*/ /* TODO */ }
		void pcode_daa() { // Decimal Adjust Accumulator after ADD/ADC
			uint8_t acc_val = ACC();
			bool old_c = getCFlag();
			uint8_t correction = 0;

			if ((acc_val & 0x0F) > 9 || getDCFlag()) {
				correction += 0x06;
			}
			if (acc_val > 0x99 || old_c) { // If original ACC > 99 or original C was set
				correction += 0x60;
				setCFlag(true); // Set C if upper nibble adjustment happened or original C was set
			}
			// Sleigh's DAA pcode doesn't show explicit C update logic based on >0x99 but uses BCDAdjustCarry
			// The BCDAdjust pcodeop is likely more complex.
			// This is a common DAA implementation.
			ACC() += correction;
			ZCheck(ACC()); // Z is usually updated by DAA
						   // OV, SLE, SGE not usually affected by DAA. DC is an input.
		}
		void pcode_das() { // Decimal Adjust Accumulator after SUB/SUBB
			uint8_t acc_val = ACC();
			bool old_c = getCFlag();   // Borrow
			bool old_dc = getDCFlag(); // Nibble borrow
			uint8_t correction = 0;

			if (old_dc || (acc_val & 0x0F) > 9) { // If nibble borrow or lower nibble > 9 (after subtraction implies it was < before)
				correction -= 0x06;
			}
			if (old_c || acc_val > 0x99) { // If full borrow or result > 0x99 (meaning it wrapped around from negative)
				correction -= 0x60;
				setCFlag(true); // DAS usually sets C if a full correction occurs (or based on old C)
			}
			else {
				// setCFlag(false); // If no major borrow, C might be cleared. Sleigh BCDAdjust is complex.
			}
			ACC() += correction; // Add negative correction
			ZCheck(ACC());
		}
		void pcode_disableMaskableInterrupts() { setGLINTFlag(false); } // Assuming GLINT is global interrupt enable
		void pcode_enableMaskableInterrupts() { setGLINTFlag(true); }

		void PushPC() {
			std::cout << "Pushed PC(0x" << std::hex << (pc << 1) << ")\n";
			stack[STKPTR() >> 1] = pc;
			STKPTR() = (STKPTR() + 2);
		}
		void PopPC() {
			STKPTR() = (STKPTR() - 2);
			pc = stack[STKPTR() >> 1];
			std::cout << "Poped PC(0x" << std::hex << (pc << 1) << ")\n";
		}

		bool repeat_flag;
		// --- Instruction Execution ---
		void Next() {
			uint32_t inst_pc = pc;
			bool is_rpt = false;
			uint16_t opcode = FetchInst();
			uint8_t op1 = opcode, op2 = opcode >> 8;
			uint8_t operand1, operand2; // For multi-byte instructions
			uint16_t operand_word;
			uint8_t reg_addr;
			uint8_t imm_val;
			uint32_t target_addr;
			static auto& rdata_temp = rdata;
			switch (op1) {
			case 0: {
				switch (op2) {

					// NOP, WDTC, SLEP (0000 00xx)
				case 0x00: // NOP
					pcode_nop();
					break;
				case 0x01: // WDTC (Sleigh: code=0; code=1)
					setTOFlag(true);
					setPDFlag(true);
					break;
				case 0x02: // SLEP (Sleigh: code=0; code=2)
					pcode_sleep();
					break;
				default:
					switch (op2 >> 4) {
						// LJMP/LCALL (001x xxxx)
						// LJMP cadr1 is code = 0x00 ; abcd = 0x2... -> 0010 xxxx (efgh from opcode)
						// LCALL cadr1 is code = 0x00 ; abcd = 0x3... -> 0011 xxxx (efgh from opcode)
					case 2:							// LJMP
						operand_word = FetchWord2(); // imm16
						// cadr1: a = (efgh<<17) + imm16 << 1;
						// efgh = opcode & 0x0F
						pc = (static_cast<uint32_t>(opcode & 0x0F) << 16) | (static_cast<uint32_t>(operand_word));
						std::cout  << "From 0x" << std::hex << (inst_pc<<1) << " calls " << std::hex << "0x" << (pc << 1) << "\n";
						break;
					case 3:							// LCALL
						operand_word = FetchWord2(); // imm16
						// cadr1: a = (efgh<<17) + imm16 << 1;
						// efgh = opcode & 0x0F
						PushPC();
						pc = (static_cast<uint32_t>(opcode & 0x0F) << 16) | (static_cast<uint32_t>(operand_word));
						std::cout  << "From 0x" << std::hex << (inst_pc<<1) << " calls " << std::hex << "0x" << (pc << 1) << "\n";
						break;
					default:
						goto invalid_op;
						break;
					}
					break;
				}
			} break;
			// SFR4
			case 0x01:
				reg_addr = op2;
				pcode_sfr4(); // Needs to know about reg_addr
				// std::cout << "SFR4 on reg 0x" << std::hex << (int)reg_addr << std::endl;
				break;

			// OR "A", reg8 (0000 0010) + reg8
			case 0x02:
				reg_addr = op2;
				ReadDat(reg_addr);
				ACC() |= static_cast<uint8_t>(rdata);
				ZCheck(ACC());
				break;
			// OR reg8, "A" (0000 0011) + reg8
			case 0x03:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t val_to_write = ACC() | static_cast<uint8_t>(rdata_temp);
					WriteDat(reg_addr, val_to_write);
					ZCheck(val_to_write);
				}
				break;

			// AND "A", reg8 (0000 0100) + reg8
			case 0x04:
				reg_addr = op2;
				ReadDat(reg_addr);
				ACC() &= static_cast<uint8_t>(rdata_temp);
				ZCheck(ACC());
				break;
			// AND reg8, "A" (0000 0101) + reg8
			case 0x05:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t val_to_write = ACC() & static_cast<uint8_t>(rdata_temp);
					WriteDat(reg_addr, val_to_write);
					ZCheck(val_to_write);
				}
				break;

			// XOR "A", reg8 (0000 0110) + reg8
			case 0x06:
				reg_addr = op2;
				ReadDat(reg_addr);
				ACC() ^= static_cast<uint8_t>(rdata_temp);
				ZCheck(ACC());
				break;
			// XOR reg8, "A" (0000 0111) + reg8
			case 0x07:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t val_to_write = ACC() ^ static_cast<uint8_t>(rdata_temp);
					WriteDat(reg_addr, val_to_write);
					ZCheck(val_to_write);
				}
				break;

			// COMA reg8 (0000 1000) + reg8
			case 0x08:
				reg_addr = op2;
				ReadDat(reg_addr);
				ACC() = ~static_cast<uint8_t>(rdata_temp);
				ZCheck(ACC());
				break;
			// COM reg8 (0000 1001) + reg8
			case 0x09:
				reg_addr = op2;
				ReadDat(reg_addr);									   // Reads original into rdata_temp, handles post-inc
				WriteDat(reg_addr, ~static_cast<uint8_t>(rdata_temp)); // Write back complemented original
				ZCheck(~static_cast<uint8_t>(rdata_temp));			   // Z flag on written value
				break;

			// RRCA reg8 (0000 1010) + reg8
			case 0x0A:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t val = static_cast<uint8_t>(rdata_temp);
					bool new_c = (val & 0x01) != 0;
					ACC() = (val >> 1) | ((getCFlag() ? 1 : 0) << 7); // Sleigh implies old C shifts in.
																	  // RRC / RRCA typically shifts bit 0 to C and into bit 7.
																	  // Sleigh: C = rdata & 1; ACC = (rdata >> 1) | (C << 7);
																	  // This is standard RRC.
					ACC() = (val >> 1) | (new_c ? 0x80 : 0x00);
					setCFlag(new_c);
				}
				break;
			// RRC reg8 (0000 1011) + reg8
			case 0x0B:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t val = static_cast<uint8_t>(rdata_temp);
					bool new_c = (val & 0x01) != 0;
					uint8_t result = (val >> 1) | (new_c ? 0x80 : 0x00);
					WriteDat(reg_addr, result);
					setCFlag(new_c);
				}
				break;
			// RLCA reg8 (0000 1100) + reg8
			case 0x0C:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t val = static_cast<uint8_t>(rdata_temp);
					bool new_c = (val & 0x80) != 0;
					ACC() = (val << 1) | (new_c ? 0x01 : 0x00);
					setCFlag(new_c);
				}
				break;
			// RLC reg8 (0000 1101) + reg8
			case 0x0D:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t val = static_cast<uint8_t>(rdata_temp);
					bool new_c = (val & 0x80) != 0;
					uint8_t result = (val << 1) | (new_c ? 0x01 : 0x00);
					WriteDat(reg_addr, result);
					setCFlag(new_c);
				}
				break;

			// SWAPA reg8 (0000 1110) + reg8
			case 0x0E:
				reg_addr = op2;
				ReadDat(reg_addr); // rdata_temp has reg value
				{
					uint8_t reg_val = static_cast<uint8_t>(rdata_temp);
					uint8_t acc_val = ACC();
					uint8_t reg_low = reg_val & 0x0F;
					uint8_t reg_high = (reg_val >> 4) & 0x0F;
					uint8_t acc_low = acc_val & 0x0F;
					uint8_t acc_high = (acc_val >> 4) & 0x0F;

					WriteDat(reg_addr, (acc_low << 4) | acc_high); // Swapped ACC nibbles to reg
					ACC() = (reg_low << 4) | reg_high;			   // Swapped reg nibbles to ACC
				}
				break;
			// SWAP reg8 (0000 1111) + reg8
			case 0x0F:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t val = static_cast<uint8_t>(rdata_temp);
					uint8_t low = val & 0x0F;
					uint8_t high = (val >> 4) & 0x0F;
					WriteDat(reg_addr, (low << 4) | high);
				}
				break;

			// ADD "A", reg8 (0001 0000) + reg8
			case 0x10:
				reg_addr = op2;
				ReadDat(reg_addr);
				AddWithFlags(ACC(), static_cast<uint8_t>(rdata_temp));
				break;
			// ADD reg8, "A" (0001 0001) + reg8
			case 0x11:
				reg_addr = op2;
				ReadDat(reg_addr); // Original reg_val in rdata_temp
				{
					uint8_t temp_val = static_cast<uint8_t>(rdata_temp); // Copy before AddWithFlags modifies it if dest is rdata_temp
					AddWithFlags(temp_val, ACC());
					WriteDat(reg_addr, temp_val);
					if (reg_addr == SFRs::PCL) {
						if (getCFlag()) {
							regs[SFRs::PCM]++;
						}
					}
					else if (reg_addr == SFRs::TABPTRL) {
						if (getCFlag()) {
							regs[SFRs::TABPTRM]++;
						}
					}
				}
				break;
			// ADC "A", reg8 (0001 0010) + reg8
			case 0x12:
				reg_addr = op2;
				ReadDat(reg_addr);
				AdcWithFlags(ACC(), static_cast<uint8_t>(rdata_temp));
				break;
			// ADC reg8, "A" (0001 0011) + reg8
			case 0x13:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t temp_val = static_cast<uint8_t>(rdata_temp);
					AdcWithFlags(temp_val, ACC());
					WriteDat(reg_addr, temp_val);
				}
				break;
			// ADDDC "A", reg8 (0001 0100) + reg8
			case 0x14:
				reg_addr = op2;
				ReadDat(reg_addr);
				AdcWithFlags(ACC(), static_cast<uint8_t>(rdata_temp)); // Sleigh add(ACC,rdata) implies ADC
				pcode_daa();
				break;
			// ADDDC reg8, "A" (0001 0101) + reg8
			case 0x15:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t temp_val = static_cast<uint8_t>(rdata_temp);
					AdcWithFlags(temp_val, ACC());
					WriteDat(reg_addr, temp_val);
					// DAA usually operates on ACC. If it needs to operate on memory, this is more complex.
					// Sleigh `daa();` implies it operates on ACC.
					// If result written to reg needs DAA, then ACC must be temporarily loaded, DAA'd, then stored back.
					// For now, assume DAA always on ACC. The instruction might be: (reg+ACC+C)->reg, then DAA(ACC).
					// Let's assume Sleigh meant DAA on ACC after the operation.
					pcode_daa();
				}
				break;

			// SUB "A", reg8 (0001 0110) + reg8
			case 0x16:
				reg_addr = op2;
				ReadDat(reg_addr);
				SubWithFlags(ACC(), static_cast<uint8_t>(rdata_temp));
				break;
			// SUB reg8, "A" (0001 0111) + reg8
			case 0x17:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t temp_val = static_cast<uint8_t>(rdata_temp);
					SubWithFlags(temp_val, ACC());
					WriteDat(reg_addr, temp_val);
				}
				break;
			// SUBB "A", reg8 (0001 1000) + reg8
			case 0x18:
				reg_addr = op2;
				ReadDat(reg_addr);
				SubbWithFlags(ACC(), static_cast<uint8_t>(rdata_temp));
				break;
			// SUBB reg8, "A" (0001 1001) + reg8
			case 0x19:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t temp_val = static_cast<uint8_t>(rdata_temp);
					SubbWithFlags(temp_val, ACC());
					WriteDat(reg_addr, temp_val);
				}
				break;
			// SUBDB "A", reg8 (0001 1010) + reg8
			case 0x1A:
				reg_addr = op2;
				ReadDat(reg_addr);
				SubbWithFlags(ACC(), static_cast<uint8_t>(rdata_temp)); // Sleigh implies SUBB
				pcode_das();
				break;
			// SUBDB reg8, "A" (0001 1011) + reg8
			case 0x1B:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t temp_val = static_cast<uint8_t>(rdata_temp);
					SubbWithFlags(temp_val, ACC());
					WriteDat(reg_addr, temp_val);
					pcode_das(); // DAS on ACC
				}
				break;

			// INCA reg8 (0001 1100) + reg8
			case 0x1C:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t val = static_cast<uint8_t>(rdata_temp);
					uint8_t result = val + 1;
					// Sleigh: C = carry(rdata,1); ACC = rdata + 1; setZFlag(ACC);
					// C flag for INC is often not standard CPU behavior or specific (like overflow from 0xFF to 0x00)
					// Sleigh `carry` pcode op. Standard carry for +1:
					setCFlag(val == 0xFF);
					ACC() = result;
					ZCheck(ACC());
					// OV, SLE, SGE may also be affected by INC. Not shown in Sleigh simple form.
				}
				break;
			// INC reg8 (0001 1101) + reg8
			case 0x1D:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t val = static_cast<uint8_t>(rdata_temp);
					uint8_t result = val + 1;
					setCFlag(val == 0xFF);
					WriteDat(reg_addr, result);
					ZCheck(result); // Z based on written value
				}
				break;
			// DECA reg8 (0001 1110) + reg8
			case 0x1E:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t val = static_cast<uint8_t>(rdata_temp);
					uint8_t result = val - 1;
					// Sleigh: C = carry(rdata, 1 ^ 0xff); (carry for A + (-1))
					// Standard C for DEC (borrow): set if val was 0x00
					setCFlag(val == 0x00);
					ACC() = result;
					ZCheck(ACC());
				}
				break;
			// DEC reg8 (0001 1111) + reg8
			case 0x1F:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t val = static_cast<uint8_t>(rdata_temp);
					uint8_t result = val - 1;
					setCFlag(val == 0x00);
					WriteDat(reg_addr, result);
					ZCheck(result);
				}
				break;

			// MOV "A", reg8 (0010 0000) + reg8
			case 0x20:
				reg_addr = op2;
				ReadDat(reg_addr);
				ACC() = static_cast<uint8_t>(rdata_temp);
				// MOV usually doesn't affect flags. Sleigh shows no flag ops.
				break;
			// MOV reg8, "A" (0010 0001) + reg8
			case 0x21:
				reg_addr = op2;
				WriteDat(reg_addr, ACC());
				break;

			// SHRA reg8 (0010 0010) + reg8 ; Arithmetic Shift Right (MSB preserved) into ACC
			case 0x22:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t val = static_cast<uint8_t>(rdata_temp);
					// Sleigh: ACC = (rdata >> 1) | (C << 7); This is a ROTATE through carry (SAR with C).
					// A true SHRA would be: ACC = (val >> 1) | (val & 0x80); C_OUT = val & 0x01;
					// Let's follow Sleigh:
					bool old_c = getCFlag();
					setCFlag((val & 0x01) != 0); // Bit shifted out goes to C
					ACC() = (val >> 1) | (old_c ? 0x80 : 0x00);
				}
				break;
			// SHLA reg8 (0010 0011) + reg8 ; Shift Left into ACC
			case 0x23:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t val = static_cast<uint8_t>(rdata_temp);
					// Sleigh: ACC = (rdata << 1) | C;
					bool old_c = getCFlag();
					setCFlag((val & 0x80) != 0); // Bit shifted out goes to C
					ACC() = (val << 1) | (old_c ? 0x01 : 0x00);
				}
				break;
			// CLR reg8 (0010 0100) + reg8
			case 0x24:
				reg_addr = op2;
				WriteDat(reg_addr, 0x00);
				// CLR might set Z flag. Sleigh doesn't show. Common behavior: Z=1.
				// setZFlag(true); // If CLR always makes Z=1. Or Z based on written value (0).
				ZCheck(0);
				break;
			// TEST reg8 (0010 0101) + reg8
			case 0x25:
				reg_addr = op2;
				ReadDat(reg_addr);
				ZCheck(static_cast<uint8_t>(rdata_temp));
				// Other flags (N, V) might be affected in some CPUs. Sleigh only shows Z.
				break;

			// MOVL reg8, "A" (0010 0110) + reg8
			case 0x26: // Move Low Nibble of ACC to Low Nibble of reg8
				reg_addr = op2;
				ReadDat(reg_addr); // reg_val in rdata_temp
				{
					uint8_t reg_val = static_cast<uint8_t>(rdata_temp);
					uint8_t acc_low_nibble = ACC() & 0x0F;
					WriteDat(reg_addr, (reg_val & 0xF0) | acc_low_nibble);
				}
				break;
			// RPT reg8 (0010 0111) + reg8
			case 0x27:
				reg_addr = op2;
				ReadDat(reg_addr);						  // Value of reg8 into rdata_temp
				ACC() = static_cast<uint8_t>(rdata_temp); // ACC gets the repeat count
				is_rpt = repeat_flag = true;
				break;

			// MOVH reg8, "A" (0010 1000) + reg8
			case 0x28: // Move Low Nibble of ACC to High Nibble of reg8 (Sleigh: (rdata & 0x0F) | ((ACC & 0xF) << 4)) )
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t reg_val = static_cast<uint8_t>(rdata_temp);
					uint8_t acc_low_nibble_shifted = (ACC() & 0x0F) << 4;
					WriteDat(reg_addr, (reg_val & 0x0F) | acc_low_nibble_shifted);
				}
				break;
			// MOVL "A", reg8 (0010 1001) + reg8
			case 0x29: // ACC = reg8 & 0x0F
				reg_addr = op2;
				ReadDat(reg_addr);
				ACC() = static_cast<uint8_t>(rdata_temp) & 0x0F;
				break;
			// MOVH "A", reg8 (0010 1010) + reg8
			case 0x2A: // ACC = rdata >> 4 (Sleigh logic)
				reg_addr = op2;
				ReadDat(reg_addr); // rdata_temp has reg value
				ACC() = (static_cast<uint8_t>(rdata_temp) >> 4) & 0x0F;
				break;

			// RET / RETI (0010 1011) + FE/FF
			case 0x2B: {
				uint8_t sub_opcode = op2;
				if (sub_opcode == 0xFE) { // RET
					PopPC();
				}
				else if (sub_opcode == 0xFF) { // RETI
					setGLINTFlag(true);		   // Enable interrupts
					PopPC();
				}
				else {
					printf("Warning: Unknown sub-opcode for 0x2B: 0x%02x\n", sub_opcode);
				}
			} break;

			// TBRD gh, reg8 (0010 11xx) + reg8 (Sleigh: abcdef = 0xB & gh) -> Opcode pattern 001011gh
			// gh = 00 (read, no inc/dec), 01 (read, post-inc), 10 (read, post-dec), 11 (read ACC-indexed)
			case 0x2C: // gh = 00
			case 0x2D: // gh = 01 (WI)
			case 0x2E: // gh = 10 (DI)
			case 0x2F: // gh = 11 (ACC indexed)
			{
				uint8_t gh = opcode & 0x03;
				reg_addr = op2;

				uint32_t tabptr_val = tabptr & 0x1ffff;
				uint8_t byte_from_rom = 0;

				if (gh == 3) {
					tabptr_val += ACC();
					tabptr_val &= 0x1ffff;
				}
				if (tabptr_val & 1) {
					byte_from_rom = static_cast<uint8_t>(mmu.ReadCode(tabptr_val >> 1) >> 8);
				}
				else {
					byte_from_rom = static_cast<uint8_t>(mmu.ReadCode(tabptr_val >> 1));
				}

				WriteDat(reg_addr, byte_from_rom);

				if (gh == 1) {
					tabptr_val++;
					tabptr++;
				}
				else if (gh == 2) {
					tabptr_val--;
					tabptr--;
				}
			} break;

			case 0x30:
			case 0x31:
			case 0x32:
			case 0x33:
			case 0x34:
			case 0x35:
			case 0x36:
			case 0x37:
			case 0x38:
			case 0x39:
			case 0x3A:
			case 0x3B:
			case 0x3C:
			case 0x3D:
			case 0x3E:
			case 0x3F:						// LCALL
				operand_word = FetchWord(); // imm16
				PushPC();					// Push current PC (after fetching operands)
				target_addr = (static_cast<uint32_t>(opcode & 0x0F) << 17) | (static_cast<uint32_t>(operand_word) << 1);

				break;
			// TBPTL imm8 (0100 0000) + imm8
			case 0x40:
				setTABPTRL(op2);
				break;
			// TBPTM imm8 (0100 0001) + imm8
			case 0x41:
				setTABPTRM(op2);
				break;
			// TBPTH imm8 (0100 0010) + imm8
			case 0x42:
				setTABPTRH(op2);
				break;
			// BANK imm8 (0100 0011) + imm8
			case 0x43:
				BSR() = op2;
				break;
			// OR "A", imm8 (0100 0100) + imm8
			case 0x44:
				ACC() |= op2;
				ZCheck(ACC());
				break;
			// AND "A", imm8 (0100 0101) + imm8
			case 0x45:
				ACC() &= op2;
				ZCheck(ACC());
				break;
			// XOR "A", imm8 (0100 0110) + imm8
			case 0x46:
				ACC() ^= op2;
				ZCheck(ACC());
				break;

			// JGE "A", imm8, radr0 (0100 0111) + imm8 + imm16
			case 0x47:
				imm_val = op2;
				operand_word = FetchWord();											// imm16 for radr0
				if (static_cast<uint8_t>(ACC()) >= static_cast<uint8_t>(imm_val)) { // Signed compare for JGE/JLE
																					// Sleigh: if (ACC > imm8)
																					// This is unsigned in Sleigh.
					// radr0: a = (inst_start & 0x80000) | imm16 << 1;
					pc = (inst_pc & 0x10000) | (static_cast<uint32_t>(operand_word));
					std::cout  << "From 0x" << std::hex << (inst_pc<<1) << " calls " << std::hex << "0x" << (pc << 1) << "\n";
				}
				break;
			// JLE "A", imm8, radr0 (0100 1000) + imm8 + imm16
			case 0x48:
				imm_val = op2;
				operand_word = FetchWord();
				if (ACC() < imm_val) { // Unsigned compare
					pc = (inst_pc & 0x10000) | (static_cast<uint32_t>(operand_word));
					std::cout  << "From 0x" << std::hex << (inst_pc<<1) << " calls " << std::hex << "0x" << (pc << 1) << "\n";
				}
				break;
			// JE "A", imm8, radr0 (0100 1001) + imm8 + imm16
			case 0x49:
				imm_val = op2;
				operand_word = FetchWord();
				if (ACC() == imm_val) {
					pc = (inst_pc & 0x10000) | (static_cast<uint32_t>(operand_word));
					std::cout  << "From 0x" << std::hex << (inst_pc<<1) << " calls " << std::hex << "0x" << (pc << 1) << "\n";
				}
				break;

			// ADD "A", imm8 (0100 1010) + imm8
			case 0x4A:
				imm_val = op2;
				AddWithFlags(ACC(), imm_val);
				break;
			// ADC "A", imm8 (0100 1011) + imm8
			case 0x4B:
				imm_val = op2;
				AdcWithFlags(ACC(), imm_val);
				break;
			// SUB "A", imm8 (0100 1100) + imm8
			case 0x4C:
				imm_val = op2;
				SubWithFlags(ACC(), imm_val);
				break;
			// SUBB "A", imm8 (0100 1101) + imm8
			case 0x4D:
				imm_val = op2;
				SubbWithFlags(ACC(), imm_val);
				break;
			// MOV "A", "#"imm8 (0100 1110) + imm8
			case 0x4E:
				ACC() = op2;
				// Z flag? Common for MOV #imm. Sleigh doesn't show.
				ZCheck(ACC());
				break;
			// SFL4 reg8 (0100 1111) + reg8
			case 0x4F: {
				reg_addr = op2;
				std::cout << "A\n";
			} break;

			// JDNZ "A", reg8, radr0 (0101 0000) + reg8 + imm16
			case 0x50:
				reg_addr = op2;								  // This reg8 is source for ACC value
				operand_word = FetchWord();					  // radr0
				ReadDat(reg_addr);							  // Value read into rdata_temp
				ACC() = static_cast<uint8_t>(rdata_temp) - 1; // ACC = [reg8] - 1
				ZCheck(ACC());								  // Z flag based on ACC
				if (ACC() != 0) {
					pc = (inst_pc & 0x10000) | (static_cast<uint32_t>(operand_word));
					std::cout  << "From 0x" << std::hex << (inst_pc<<1) << " calls " << std::hex << "0x" << (pc << 1) << "\n";
				}
				break;
			// JDNZ reg8, radr0 (0101 0001) + reg8 + imm16
			case 0x51:
				reg_addr = op2;				// This reg8 is to be decremented
				operand_word = FetchWord(); // radr0
				ReadDat(reg_addr);			// val in rdata_temp
				{
					uint8_t val = static_cast<uint8_t>(rdata_temp) - 1;
					WriteDat(reg_addr, val); // Write decremented value back
					ZCheck(val);			 // Z based on written value
					if (val != 0) {
						pc = (inst_pc & 0x10000) | (static_cast<uint32_t>(operand_word));
						std::cout  << "From 0x" << std::hex << (inst_pc<<1) << " calls " << std::hex << "0x" << (pc << 1) << "\n";
					}
				}
				break;
			// EXL reg8 (0101 0010) + reg8 ; Exchange Low nibbles ACC.low <=> reg.low
			case 0x52:
				reg_addr = op2;
				ReadDat(reg_addr); // reg_val in rdata_temp
				{
					uint8_t reg_val = static_cast<uint8_t>(rdata_temp);
					uint8_t acc_val = ACC();
					uint8_t acc_low = acc_val & 0x0F;
					uint8_t reg_low = reg_val & 0x0F;
					ACC() = (acc_val & 0xF0) | reg_low;
					WriteDat(reg_addr, (reg_val & 0xF0) | acc_low);
				}
				break;
			// EXH reg8 (0101 0011) + reg8 ; Exchange High nibbles ACC.high <=> reg.high
			case 0x53:
				reg_addr = op2;
				ReadDat(reg_addr);
				{
					uint8_t reg_val = static_cast<uint8_t>(rdata_temp);
					uint8_t acc_val = ACC();
					uint8_t acc_high_shifted = (acc_val >> 4) & 0x0F;
					uint8_t reg_high_shifted = (reg_val >> 4) & 0x0F;
					ACC() = (acc_val & 0x0F) | (reg_high_shifted << 4);
					WriteDat(reg_addr, (reg_val & 0x0F) | (acc_high_shifted << 4));
				}
				break;
			// EX reg8 (0101 0100) + reg8 ; Exchange ACC <=> reg8
			case 0x54:
				reg_addr = op2;
				ReadDat(reg_addr); // reg_val in rdata_temp
				{
					uint8_t reg_val = static_cast<uint8_t>(rdata_temp);
					uint8_t acc_val = ACC();
					ACC() = reg_val;
					WriteDat(reg_addr, acc_val);
				}
				break;

			// JGE "A", reg8, radr0 (0101 0101) + reg8 + imm16
			case 0x55:
				reg_addr = op2;
				operand_word = FetchWord();
				ReadDat(reg_addr);								// val in rdata_temp
				if (ACC() > static_cast<uint8_t>(rdata_temp)) { // Unsigned
					pc = (inst_pc & 0x10000) | (static_cast<uint32_t>(operand_word));
					std::cout  << "From 0x" << std::hex << (inst_pc<<1) << " calls " << std::hex << "0x" << (pc << 1) << "\n";
				}
				break;
			// JLE "A", reg8, radr0 (0101 0110) + reg8 + imm16
			case 0x56:
				reg_addr = op2;
				operand_word = FetchWord();
				ReadDat(reg_addr);
				if (ACC() < static_cast<uint8_t>(rdata_temp)) { // Unsigned
					pc = (inst_pc & 0x10000) | (static_cast<uint32_t>(operand_word));
					std::cout  << "From 0x" << std::hex << (inst_pc<<1) << " calls " << std::hex << "0x" << (pc << 1) << "\n";
				}
				break;
			// JE "A", reg8, radr0 (0101 0111) + reg8 + imm16
			case 0x57:
				reg_addr = op2;
				operand_word = FetchWord();
				ReadDat(reg_addr);
				if (ACC() == static_cast<uint8_t>(rdata_temp)) {
					pc = (inst_pc & 0x10000) | (static_cast<uint32_t>(operand_word));
					std::cout  << "From 0x" << std::hex << (inst_pc<<1) << " calls " << std::hex << "0x" << (pc << 1) << "\n";
				}
				break;

			// JBC reg8.fgh, radr0 (0101 1fff) + reg8 + imm16 (Sleigh: abcde=0xB) -> 01011 fgh
			// JBS reg8.fgh, radr0 (0110 0fff) + reg8 + imm16 (Sleigh: abcde=0xC) -> 01100 fgh
			// BC  reg8.fgh       (0110 1fff) + reg8       (Sleigh: abcde=0xD) -> 01101 fgh
			// BS  reg8.fgh       (0111 0fff) + reg8       (Sleigh: abcde=0xE) -> 01110 fgh
			// BTG reg8.fgh       (0111 1fff) + reg8       (Sleigh: abcde=0xF) -> 01111 fgh
			// This implies opcodes are:
			// JBC: 0x58-0x5F (01011xxx)
			// JBS: 0x60-0x67 (01100xxx)
			// BC:  0x68-0x6F (01101xxx)
			// BS:  0x70-0x77 (01110xxx)
			// BTG: 0x78-0x7F (01111xxx)
			case 0x58:
			case 0x59:
			case 0x5A:
			case 0x5B:
			case 0x5C:
			case 0x5D:
			case 0x5E:
			case 0x5F: // JBC
			{
				uint8_t bit_index = opcode & 0x07; // fgh
				reg_addr = op2;
				operand_word = FetchWord();									  // radr0
				ReadDat(reg_addr);											  // val in rdata_temp
				if (!(static_cast<uint8_t>(rdata_temp) & (1 << bit_index))) { // If bit is Clear
					pc = (inst_pc & 0x10000) | (static_cast<uint32_t>(operand_word));
					std::cout  << "From 0x" << std::hex << (inst_pc<<1) << " calls " << std::hex << "0x" << (pc << 1) << "\n";
				}
			} break;
			case 0x60:
			case 0x61:
			case 0x62:
			case 0x63:
			case 0x64:
			case 0x65:
			case 0x66:
			case 0x67: // JBS
			{
				uint8_t bit_index = opcode & 0x07;
				reg_addr = op2;
				operand_word = FetchWord();
				ReadDat(reg_addr);
				if ((static_cast<uint8_t>(rdata_temp) & (1 << bit_index))) {
					pc = (inst_pc & 0x10000) | (static_cast<uint32_t>(operand_word));
					std::cout  << "From 0x" << std::hex << (inst_pc<<1) << " calls " << std::hex << "0x" << (pc << 1) << "\n";
				}
			} break;
			case 0x68:
			case 0x69:
			case 0x6A:
			case 0x6B:
			case 0x6C:
			case 0x6D:
			case 0x6E:
			case 0x6F: // BC
			{
				uint8_t bit_index = opcode & 0x07;
				reg_addr = op2;
				ReadDat(reg_addr);
				uint8_t val = static_cast<uint8_t>(rdata_temp) & ~(1 << bit_index);
				WriteDat(reg_addr, val);
			} break;
			case 0x70:
			case 0x71:
			case 0x72:
			case 0x73:
			case 0x74:
			case 0x75:
			case 0x76:
			case 0x77: // BS
			{
				uint8_t bit_index = opcode & 0x07;
				reg_addr = op2;
				ReadDat(reg_addr);
				uint8_t val = static_cast<uint8_t>(rdata_temp) | (1 << bit_index);
				WriteDat(reg_addr, val);
			} break;
			case 0x78:
			case 0x79:
			case 0x7A:
			case 0x7B:
			case 0x7C:
			case 0x7D:
			case 0x7E:
			case 0x7F: // BTG
			{
				uint8_t bit_index = opcode & 0x07;
				reg_addr = op2;
				ReadDat(reg_addr);
				uint8_t val = static_cast<uint8_t>(rdata_temp) ^ (1 << bit_index);
				WriteDat(reg_addr, val);
			} break;

			default: {
				// MOV defgh, reg8 (Sleigh: abc=100) -> 100ddddd (defgh=ddddd)
				if (op1 >= 0x80 and op1 <= 0x9f) {
					uint8_t defgh_addr = opcode & 0x1F;
					uint8_t src_reg8_addr = op2;
					ReadDat(src_reg8_addr);
					WriteDat(defgh_addr, rdata);
				}
				// MOV reg8, defgh (Sleigh: abc=101) -> 101sssss (defgh=sssss)
				else if (op1 >= 0xa0 and op1 <= 0xbf) {
					uint8_t defgh_addr = opcode & 0x1F;
					uint8_t src_reg8_addr = op2;
					ReadDat(defgh_addr);
					WriteDat(src_reg8_addr, rdata);
				}
				// SJMP cadr3 (Sleigh: abc=110) -> 110ddddd (defgh=ddddd)
				// SCALL cadr3 (Sleigh: abc=111) -> 111ddddd (defgh=ddddd)
				// Opcode ranges:
				// SJMP: 0xC0 - 0xDF
				// SCALL:0xE0 - 0xFF
				else if (op1 >= 0xc0 and op1 <= 0xdf) {
					uint8_t defgh_val = opcode & 0x1F;
					imm_val = op2; // imm8 for cadr3
					// cadr3: a = (inst_start & 0xFC000) | (defgh<<9) | imm8 << 1;
					pc = (inst_pc & 0x1e000) |
						 (static_cast<uint32_t>(defgh_val) << 8) |
						 (static_cast<uint32_t>(imm_val));
					std::cout  << "From 0x" << std::hex << (inst_pc<<1) << " calls " << std::hex << "0x" << (pc << 1) << "\n";
				}
				else if (op1 >= 0xe0) {
					uint8_t defgh_val = opcode & 0x1F;
					imm_val = op2;
					PushPC();
					pc = (inst_pc & 0x1e000) |
						 (static_cast<uint32_t>(defgh_val) << 8) |
						 (static_cast<uint32_t>(imm_val));
					std::cout  << "From 0x" << std::hex << (inst_pc<<1) << " calls " << std::hex << "0x" << (pc << 1) << "\n";
				}
				else {
				invalid_op:
					printf("Warning: Unimplemented opcode 0x%08x at PC=0x%x\n", opcode, inst_pc);
					break;
				}
			} break;
			}

			(repeat_flag && !is_rpt) && (ACC() ? (--ACC(), pc = inst_pc) : repeat_flag = 0);
		}
	}; // namespace casioemu
}; // namespace casioemu