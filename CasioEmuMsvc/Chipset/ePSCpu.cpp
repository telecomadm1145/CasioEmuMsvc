#include "ePSCpu.h"
#include "MMU.hpp"
#include <array>

using WCHAR = char;

#define ___security_check_cookie_4(...)
#define debug_printf2(x, ...) debug_printf(__VA_ARGS__)

static char Sfr_in_str[128][20]{
	"INDF0",
	"FSR0",
	"BSR",

	"INDF1",
	"FSR1",
	"BSR1",

	"STKPTR",

	"PCL",
	"PCM",
	"PCH",

	"ACC",

	"TABPTRL",
	"TABPTRM",
	"TABPTRH",

	"LCDDATA",

	"STATUS",

	"INDF2",
	"FSR2",
	"BSR2",
};

inline byte* casioemu::ePSCPU::reg(byte param_1) {
	uint uVar1;
	byte* pbVar2;
	uVar1 = (uint)param_1;
	switch (uVar1) {
	case 0x0:
		if ((BSR == 0x0) && (FSR < 0x80)) {
			pbVar2 = &EmuMem + FSR;
		}
		else {
			pbVar2 = &EmuMem + (uint)FSR + (uint)BSR * 0x80;
		}
		return pbVar2;
	case 0x3:
		FSR1 |= 0x80;
		pbVar2 = &EmuMem + (uint)FSR1 + (uint)BSR1 * 0x80;
		return pbVar2;
	case 0xe:
		pbVar2 = &VRam + (uint)LCDARL + (LCDARH & 0x3) * 0x60;
		return pbVar2;
	case 0x10:
		FSR2 |= 0x80;
		pbVar2 = &EmuMem + (uint)FSR2 + (uint)BSR2 * 0x80;
		return pbVar2;
	}
	if (0x7f < param_1) {
		pbVar2 = &EmuMem + uVar1 + (uint)BSR * 0x80;
		return pbVar2;
	}
	// this isn't present in original HP300S+ emulator, idk will it be useful or not.
	if (STATUS & 0x80) {
		if (param_1 >= 0x25u && param_1 <= 0x3Fu)
			return &wbk[param_1 - 0x25u];
	}
	pbVar2 = &EmuMem + uVar1;
	return pbVar2;
}

inline void casioemu::ePSCPU::post_pid(char param_1) {
	byte bVar1;

	if (param_1 == '\0') {
		if (((POST_ID & 0x1) != 0x0) && ((POST_ID & 0x10) != 0x0)) {
			FSR += '\x01';
		}
		if (((POST_ID & 0x1) == 0x1) && ((POST_ID & 0x10) == 0x0)) {
			FSR += -0x1;
			return;
		}
	}
	else if (param_1 == '\x03') {
		bVar1 = POST_ID >> 0x1 & 0x1;
		if (((bVar1 != 0x0) && ((POST_ID & 0x20) != 0x0)) && (FSR1 += '\x01', FSR1 == '\0')) {
			BSR1 += '\x01';
			FSR1 = -0x80;
		}
		if (((bVar1 == 0x1) && ((POST_ID & 0x20) == 0x0)) && (FSR1 += -0x1, FSR1 == '\x7f')) {
			FSR1 = 0xff;
			BSR1 += -0x1;
			return;
		}
	}
	else if (param_1 == '\x10') {
		bVar1 = POST_ID >> 0x3 & 0x1;
		if (((bVar1 != 0x0) && ((char)POST_ID < '\0')) && (FSR2 += '\x01', FSR2 == '\0')) {
			BSR2 += '\x01';
			FSR2 = -0x80;
		}
		if (((bVar1 == 0x1) && ((char)POST_ID >= 0)) && (FSR2 += -0x1, FSR2 == '\x7f')) {
			FSR2 = 0xff;
			BSR2 += -0x1;
			return;
		}
	}
	else if (param_1 == '\x0e') {
		bVar1 = POST_ID >> 0x2 & 0x1;
		if (((bVar1 != 0x0) && ((POST_ID & 0x40) != 0x0)) && (LCDARL += '\x01', LCDARL == 'a')) {
			LCDARL = '\0';
		}
		if (((bVar1 == 0x1) && ((POST_ID & 0x40) == 0x0)) && (LCDARL += -0x1, LCDARL == 0xff)) {
			LCDARL = 'a';
		}
	}
	return;
}

inline void casioemu::ePSCPU::auto_carry(char param_1) {
	if (param_1 == '\a') {
		if ((STATUS & 0x1) != 0x0) {
			PCM += '\x01';
			return;
		}
	}
	else if (param_1 == '\b') {
		if ((STATUS & 0x1) != 0x0) {
			PCH += '\x01';
			return;
		}
	}
	else if (param_1 == '\v') {
		if ((STATUS & 0x1) != 0x0) {
			TABPTRM += '\x01';
			return;
		}
	}
	else if (param_1 == '\f') {
		if ((STATUS & 0x1) != 0x0) {
			TABPTRH += '\x01';
			return;
		}
	}
	else if (param_1 == '\x01') {
		if ((STATUS & 0x1) != 0x0) {
			BSR += '\x01';
			return;
		}
	}
	else if (param_1 == '\x04') {
		if ((STATUS & 0x1) != 0x0) {
			BSR1 += '\x01';
			FSR1 |= 0x80;
			return;
		}
	}
	else if ((param_1 == '\x11') && ((STATUS & 0x1) != 0x0)) {
		BSR2 += '\x01';
		FSR2 |= 0x80;
	}
	return;
}

inline void casioemu::ePSCPU::auto_borrow(char param_1)

{
	char cVar1;

	if (param_1 == '\a') {
		if ((STATUS & 0x1) == 0x0) {
			PCM += -0x1;
			return;
		}
	}
	else if (param_1 == '\v') {
		if ((STATUS & 0x1) == 0x0) {
			TABPTRM += -0x2;
			return;
		}
	}
	else if (param_1 == '\f') {
		if ((STATUS & 0x1) == 0x0) {
			TABPTRH += -0x1;
			return;
		}
	}
	else if (param_1 == '\x01') {
		if (((STATUS & 0x1) == 0x0) && (BSR += -0x1, BSR != '\0')) {
			FSR |= 0x80;
			return;
		}
	}
	else {
		if (param_1 == '\x04') {
			if ((STATUS & 0x1) != 0x0) {
				return;
			}
			BSR1 += -0x1;
			cVar1 = BSR1;
		}
		else {
			if (param_1 != '\x11') {
				return;
			}
			if ((STATUS & 0x1) != 0x0) {
				return;
			}
			BSR2 += -0x1;
			cVar1 = BSR2;
		}
		if (cVar1 != '\0') {
			FSR1 |= 0x80;
		}
	}
	return;
}
namespace detail {

	using casioemu::ePSCPU;

	//--------------------------------------------------------------
	// ָ����ڵ�
	//--------------------------------------------------------------
	struct InstTbl {
		/* �ӱ����죺depth = 0 ��ʾ�������²�� */
		constexpr InstTbl(const InstTbl (*next)[16]) noexcept
			: next(next), depth(0) {}

		/* Ҷ�ڵ㹹�죺depth = 0xFF ��ʾ���˽�����handler Ϊ����ָ�� */
		constexpr InstTbl(ePSCPU::OP_Handler op) noexcept
			: handler(op), depth(0xFF) {}

		/* Ĭ�Ϲ��죺δָ֪�� */
		constexpr InstTbl() noexcept
			: handler(&ePSCPU::OP_UD), depth(0xFF) {}

		union {
			const InstTbl (*next)[16];
			ePSCPU::OP_Handler handler;
		};
		std::uint8_t depth;
	};

	//--------------------------------------------------------------
	// 000_ : �����ӱ�
	//--------------------------------------------------------------
	inline constexpr InstTbl TB_000_[16]{
		&ePSCPU::OP_NOP, &ePSCPU::OP_WDTC, &ePSCPU::OP_SLEP};

	//--------------------------------------------------------------
	// 00__ : �����ӱ�
	//--------------------------------------------------------------
	inline constexpr InstTbl TB_00__[16]{
		&TB_000_,
		{},
		&ePSCPU::OP_LJMP,
		&ePSCPU::OP_LCALL,
	};

	//--------------------------------------------------------------
	// 0___ : һ���ӱ�
	//--------------------------------------------------------------
	inline constexpr InstTbl TB_0___[16]{
		/* 0x0 */ &TB_00__, &ePSCPU::OP_SFR4, &ePSCPU::OP_OR_A, &ePSCPU::OP_OR_R,
		/* 0x4 */ &ePSCPU::OP_AND_A, &ePSCPU::OP_AND_r, &ePSCPU::OP_XOR_A, &ePSCPU::OP_XOR_r,
		/* 0x8 */ &ePSCPU::OP_COMA, &ePSCPU::OP_COM, &ePSCPU::OP_RRCA, &ePSCPU::OP_RRC,
		/* 0xC */ &ePSCPU::OP_RLCA, &ePSCPU::OP_RLC, &ePSCPU::OP_SWAPA, &ePSCPU::OP_SWAP};

	//--------------------------------------------------------------
	// 1___ : һ���ӱ�
	//--------------------------------------------------------------
	inline constexpr InstTbl TB_1___[16]{
		&ePSCPU::OP_ADD_A, &ePSCPU::OP_ADD_r, &ePSCPU::OP_ADC_A, &ePSCPU::OP_ADC_r,
		&ePSCPU::OP_ADDDC_A, &ePSCPU::OP_ADDDC_r, &ePSCPU::OP_SUB_A, &ePSCPU::OP_SUB_r,
		&ePSCPU::OP_SUBB_A, &ePSCPU::OP_SUBB_r, &ePSCPU::OP_SUBDB_A, &ePSCPU::OP_SUBDB_r,
		&ePSCPU::OP_INCA, &ePSCPU::OP_INC, &ePSCPU::OP_DECA, &ePSCPU::OP_DEC};

	//--------------------------------------------------------------
	// 2B__ : �����ӱ� (2Bxx)
	//--------------------------------------------------------------
	inline constexpr InstTbl TB_2B__[16]{
		{}, {}, {}, {}, {}, {}, {}, {},
		{}, {}, {}, {}, {}, {}, &ePSCPU::OP_RETI, &ePSCPU::OP_RET};

	//--------------------------------------------------------------
	// 2___ : һ���ӱ�
	//--------------------------------------------------------------
	inline constexpr InstTbl TB_2___[16]{
		&ePSCPU::OP_MOV_A, &ePSCPU::OP_MOV_r, &ePSCPU::OP_SHRA, &ePSCPU::OP_SHLA,
		&ePSCPU::OP_CLR, &ePSCPU::OP_TEST, &ePSCPU::OP_MOVL_r, &ePSCPU::OP_RPT,
		&ePSCPU::OP_MOVH_r, &ePSCPU::OP_MOVL_A, &ePSCPU::OP_MOVH_A, &TB_2B__,
		&ePSCPU::OP_TBRD, &ePSCPU::OP_TBRD, &ePSCPU::OP_TBRD, &ePSCPU::OP_TBRD_A};

	//--------------------------------------------------------------
	// 4___ : һ���ӱ�
	//--------------------------------------------------------------
	inline constexpr InstTbl TB_4___[16]{
		&ePSCPU::OP_TBPTRL, &ePSCPU::OP_TBPTRM, &ePSCPU::OP_TBPTRH, &ePSCPU::OP_BANK,
		&ePSCPU::OP_OR_k, &ePSCPU::OP_AND_k, &ePSCPU::OP_XOR_k, &ePSCPU::OP_JGE_A_k,
		&ePSCPU::OP_JLE_A_k, &ePSCPU::OP_JE_A_k, &ePSCPU::OP_ADD_k, &ePSCPU::OP_ADC_k,
		&ePSCPU::OP_SUB_k, &ePSCPU::OP_SUBB_k, &ePSCPU::OP_MOV_k, &ePSCPU::OP_SFL4};

	//--------------------------------------------------------------
	// 5___ : һ���ӱ�
	//--------------------------------------------------------------
	inline constexpr InstTbl TB_5___[16]{
		&ePSCPU::OP_JDNZ_A, &ePSCPU::OP_JDNZ_r, &ePSCPU::OP_EXL_r, &ePSCPU::OP_EXH_r,
		&ePSCPU::OP_EX_r, &ePSCPU::OP_JGE_A_r, &ePSCPU::OP_JLE_A_r, &ePSCPU::OP_JE_A_r,
		&ePSCPU::OP_JBC, &ePSCPU::OP_JBC, &ePSCPU::OP_JBC, &ePSCPU::OP_JBC,
		&ePSCPU::OP_JBC, &ePSCPU::OP_JBC, &ePSCPU::OP_JBC, &ePSCPU::OP_JBC};

	//--------------------------------------------------------------
	// 6___ : һ���ӱ�
	//--------------------------------------------------------------
	inline constexpr InstTbl TB_6___[16]{
		/* 0x0-0x7 JBS */ &ePSCPU::OP_JBS, &ePSCPU::OP_JBS, &ePSCPU::OP_JBS, &ePSCPU::OP_JBS,
		&ePSCPU::OP_JBS, &ePSCPU::OP_JBS, &ePSCPU::OP_JBS, &ePSCPU::OP_JBS,

		/* 0x8-0xF BC  */ &ePSCPU::OP_BC, &ePSCPU::OP_BC, &ePSCPU::OP_BC, &ePSCPU::OP_BC,
		&ePSCPU::OP_BC, &ePSCPU::OP_BC, &ePSCPU::OP_BC, &ePSCPU::OP_BC};

	//--------------------------------------------------------------
	// 7___ : һ���ӱ�
	//--------------------------------------------------------------
	inline constexpr InstTbl TB_7___[16]{
		/* 0x0-0x7 BS  */ &ePSCPU::OP_BS, &ePSCPU::OP_BS, &ePSCPU::OP_BS, &ePSCPU::OP_BS,
		&ePSCPU::OP_BS, &ePSCPU::OP_BS, &ePSCPU::OP_BS, &ePSCPU::OP_BS,

		/* 0x8-0xF BTG */ &ePSCPU::OP_BTG, &ePSCPU::OP_BTG, &ePSCPU::OP_BTG, &ePSCPU::OP_BTG,
		&ePSCPU::OP_BTG, &ePSCPU::OP_BTG, &ePSCPU::OP_BTG, &ePSCPU::OP_BTG};

	//--------------------------------------------------------------
	// ���������ұ�
	//--------------------------------------------------------------
	inline constexpr InstTbl main_lookup_tbl[16]{
		&TB_0___, &TB_1___, &TB_2___, &ePSCPU::OP_S0CALL,
		&TB_4___, &TB_5___, &TB_6___, &TB_7___,
		&ePSCPU::OP_MOVRP, &ePSCPU::OP_MOVRP, &ePSCPU::OP_MOVPR, &ePSCPU::OP_MOVPR,
		&ePSCPU::OP_SJMP, &ePSCPU::OP_SJMP, &ePSCPU::OP_SCALL, &ePSCPU::OP_SCALL};

} // namespace detail

void casioemu::ePSCPU::Next() {
	if (run_stat > ST_SLEEP)
		return;
	Timer0Next();
	if (run_stat > ST_FAST)
		return;
	detail::InstTbl cur = &detail::main_lookup_tbl;
	auto p = Rom + (PC() << 1);
	auto ptr = p;
	do {
		cur = (*cur.next)[*(ptr++)];
	} while (cur.depth != 0xff);
	(this->*(cur.handler))(p);
}

void casioemu::ePSCPU::Reset() {
	run_stat = ST_SLOW;
	memset(regs, 0, 0x80);
	memset(ram, 0, 0x80 * 64);
	memset(stackram, 0, sizeof(stackram));
	memset(vram, 0, 0x60 * 4);
	INDF0 = INDF1 = INDF2 = 1;
	FSR1 = FSR2 = 0x80;
	// STATUS = 0xc0;
	POST_ID = 0x70;
	DCRA = DCRB = 0xff;
	DCRC = 0xf;
	DCRDE = 0x33;
	RepeatCount = 0;
	CycleCounter = 0;
	InstFlags = 0;
}

// Port����
inline void casioemu::ePSCPU::InvalidateTimerSetting(uint32_t idx) {
}

inline void casioemu::ePSCPU::UpdateTimerSetting(uint32_t idx) {
}

inline void casioemu::ePSCPU::InvalidatePORTA() {
	if (portacalc)
		portacalc();
}

void casioemu::ePSCPU::RaisePAINT(int source) {
	if (CPUCON & 0b100) { // GLINT = 1
		// CPUCON &= ~0b100; // GLINT = 0
		run_stat = ST_SLOW;
		*(ushort*)(&StackRam + (uint)STKPTR * 0x2) = (ushort)PCM * 0x100 + (ushort)PCL + 0x2;
		CycleCounter += 0x2;
		PCL = 2;
		PCM = 0;
		PCH = 0;
		STKPTR = STKPTR + 0x1 & 0x1f;
	}
}

void casioemu::ePSCPU::RaiseTMINT(int source) {
	if (CPUCON & 0b100) {	   // GLINT = 1
		INTSTA |= 1 << source; // Timer Interrupt Status Register.
		// CPUCON &= ~0b100;	   // GLINT = 0
		// run_stat = ST_SLOW;
		*(ushort*)(&StackRam + (uint)STKPTR * 0x2) = (ushort)PCM * 0x100 + (ushort)PCL + 0x2;
		CycleCounter += 0x2;
		PCL = 8;
		PCM = 0;
		PCH = 0;
		STKPTR = STKPTR + 0x1 & 0x1f;
	}
}

void casioemu::ePSCPU::Timer0Next() {
	{
		auto& timer0cnt = *(ushort*)&T0CL;
		auto& timer0reload = *(ushort*)&TRL0L;
		auto prescale = TR0CON & 0b11;
		if (TR0CON & 0b100000) // Event counter mode
			return;			   // we don't impl that.

		if (TR0CON & 0b100) { // ����ģʽ
		}
		else { // ����ģʽ...
		}

		if (TR0CON & 0b1000) // T0EN
		{
			if (t0tick++ > (1 << (prescale * 2))) {
				if (!(timer0cnt--)) {
					timer0cnt = timer0reload;					   // ���������
					if (TR0CON & 0b10000 && (run_stat <= ST_FAST)) // TMR0IE
					{
						RaiseTMINT(0);
					}
				}
				t0tick = 0;
			}
		}
	}
	{
		auto prescale = TR1CON & 0b11;

		if (TR1CON & 0b1000) // T1EN
		{
			if (t1_pre++ > 64) // ����
			{
				if (t1tick++ > (1 << (prescale * 2 + 1))) {
					if (!(t1tick2--)) {
						t1tick2 = TRL1;									// ���������
						if (run_stat <= ST_FAST || TR1CON & 0b10000000) // ���ѹ���
							if (TR1CON & 0b10000)						// TMR1IE
							{
								if (TR1CON & 0b10000000)
									run_stat = ST_SLOW;
								RaiseTMINT(1);
							}
					}
					t1tick = 0;
				}
				t1_pre = 0;
			}
		}
	}
}

inline void casioemu::ePSCPU::OP_NOP(byte* param_1) {
	int iVar1;
	do {
		Sleep(0x0);
		CycleCounter += 0x1;
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	iVar1 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar1;
	PCM = (char)((uint)iVar1 >> 0x8);
	PCH = (char)((uint)iVar1 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_WDTC(byte* param_1) {
	int iVar1;
	STATUS |= 0xc0;
	// RepeatCount = 0; // RPT WDTC��ʲô����
	CycleCounter += 0x1;
	iVar1 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar1;
	PCM = (char)((uint)iVar1 >> 0x8);
	PCH = (char)((uint)iVar1 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_SLEP(byte* param_1) {
	run_stat = ((CPUCON & 0x2) != 0x2) ? ST_SLEEP : ST_STOP;
	if (run_stat == ST_SLEEP) {
		debug_printf("entering SLEEP mode!!!\n");
	}
	else if (run_stat == ST_STOP) {
		debug_printf("entering STOP mode!!!\n");
	}
	Sleep(0x1);
	// RepeatCount = 0; // RPT SLEP��ʲô����
	CycleCounter += 0x1;
	int iVar1 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar1;
	PCM = (char)((uint)iVar1 >> 0x8);
	PCH = (char)((uint)iVar1 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_BANK(byte* param_1) {
	int iVar1;

	BSR = *(char*)(param_1 + 0x2) << 0x4 | *(byte*)(param_1 + 0x3);
	CycleCounter += 0x1;
	iVar1 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar1;
	PCM = (char)((uint)iVar1 >> 0x8);
	PCH = (char)((uint)iVar1 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_S0CALL(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	int iVar4;

	bVar1 = *(byte*)(param_1 + 0x1);
	bVar2 = *(byte*)(param_1 + 0x2);
	bVar3 = *(byte*)(param_1 + 0x3);
	*(ushort*)(&StackRam + (uint)STKPTR * 0x2) = (ushort)PCM * 0x100 + (ushort)PCL + 0x1;
	CycleCounter += 0x1;
	iVar4 = ((uint)bVar1 << 0x4 | (uint)bVar2) << 0x4;
	PCL = (byte)iVar4 | bVar3;
	STKPTR = STKPTR + 0x1 & 0x1f;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = 0x0;
	return;
}

inline void casioemu::ePSCPU::OP_SCALL(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte bVar4;
	int iVar5;

	bVar1 = *param_1;
	bVar2 = param_1[0x1];
	bVar3 = param_1[0x2];
	bVar4 = param_1[0x3];
	*(ushort*)(&StackRam + (uint)STKPTR * 0x2) = (ushort)PCM * 0x100 + (ushort)PCL + 0x1;
	CycleCounter += 0x1;
	iVar5 = ((((uint)bVar1 << 0x4 | (uint)bVar2) & 0x1f) << 0x4 | (uint)bVar3) << 0x4;
	PCL = (byte)iVar5 | bVar4;
	STKPTR = STKPTR + 0x1 & 0x1f;
	PCM = PCM & 0xe0 | (byte)((uint)iVar5 >> 0x8);
	PCH = 0x0;
	return;
}

inline void casioemu::ePSCPU::OP_LCALL(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte bVar4;
	int iVar5;

	bVar1 = *(byte*)(param_1 + 0x4);
	bVar2 = *(byte*)(param_1 + 0x5);
	bVar3 = *(byte*)(param_1 + 0x6);
	bVar4 = *(byte*)(param_1 + 0x7);
	*(ushort*)(&StackRam + (uint)STKPTR * 0x2) = (ushort)PCM * 0x100 + (ushort)PCL + 0x2;
	CycleCounter += 0x2;
	iVar5 = (((uint)bVar1 << 0x4 | (uint)bVar2) << 0x4 | (uint)bVar3) << 0x4;
	PCL = (byte)iVar5 | bVar4;
	STKPTR = STKPTR + 0x1 & 0x1f;
	PCM = (char)((uint)iVar5 >> 0x8);
	PCH = (char)((uint)iVar5 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_RET(byte* param_1)

{
	undefined2 uVar1;

	STKPTR = STKPTR - 0x1 & 0x1f;
	uVar1 = *(undefined2*)(&StackRam + (uint)STKPTR * 0x2);
	*(undefined2*)(&StackRam + (uint)STKPTR * 0x2) = 0x0;
	CycleCounter += 0x1;
	PCL = (char)uVar1;
	PCM = (char)((ushort)uVar1 >> 0x8);
	PCH = 0x0;
	return;
}

inline void casioemu::ePSCPU::OP_RETI(byte* param_1) {
	ushort uVar1;
	WCHAR local_204[0x100];
	uint local_4;
	STKPTR = STKPTR - 0x1 & 0x1f;
	uVar1 = *(ushort*)(&StackRam + (uint)STKPTR * 0x2);
	*(undefined2*)(&StackRam + (uint)STKPTR * 0x2) = 0x0;
	CPUCON |= 0x4;
	PCL = (undefined)uVar1;
	PCH = 0x0;
	PCM = (undefined)(uVar1 >> 0x8);
	DAT_004202b7 = 0x0;
	debug_printf(L"RETI pc %x\n", (uint)uVar1);
	CycleCounter += 0x1;
	return;
}

inline void casioemu::ePSCPU::OP_SJMP(byte* param_1)

{
	int iVar1;

	CycleCounter += 0x1;
	iVar1 = ((((uint)*param_1 << 0x4 | (uint)param_1[0x1]) & 0x1f) << 0x4 | (uint)param_1[0x2]) << 0x4;
	PCL = (byte)iVar1 | param_1[0x3];
	PCM = PCM & 0xe0 | (byte)((uint)iVar1 >> 0x8);
	PCH = 0x0;
	return;
}

inline void casioemu::ePSCPU::OP_LJMP(byte* param_1)

{
	int iVar1;

	CycleCounter += 0x2;
	iVar1 = (((uint) * (byte*)(param_1 + 0x4) << 0x4 | (uint) * (byte*)(param_1 + 0x5)) << 0x4 |
				(uint) * (byte*)(param_1 + 0x6))
			<< 0x4;
	PCL = (byte)iVar1 | *(byte*)(param_1 + 0x7);
	PCM = (char)((uint)iVar1 >> 0x8);
	PCH = (char)((uint)iVar1 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_JGE_A_k(byte* param_1)

{
	uint uVar1;
	uint uVar2;

	if (ACC < (byte)(*(char*)(param_1 + 0x2) << 0x4 | *(byte*)(param_1 + 0x3))) {
		uVar2 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x2;
		PCM = (byte)(uVar2 >> 0x8);
	}
	else {
		uVar1 = (((uint) * (byte*)(param_1 + 0x4) << 0x4 | (uint) * (byte*)(param_1 + 0x5)) << 0x4 |
					(uint) * (byte*)(param_1 + 0x6))
				<< 0x4;
		uVar2 = uVar1 | *(byte*)(param_1 + 0x7);
		PCM = (byte)(uVar1 >> 0x8);
	}
	CycleCounter += 0x2;
	PCL = (char)uVar2;
	PCH = (char)(uVar2 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_JLE_A_k(byte* param_1)

{
	uint uVar1;
	uint uVar2;

	if ((byte)(*(char*)(param_1 + 0x2) << 0x4 | *(byte*)(param_1 + 0x3)) < ACC) {
		uVar2 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x2;
		PCM = (byte)(uVar2 >> 0x8);
	}
	else {
		uVar1 = (((uint) * (byte*)(param_1 + 0x4) << 0x4 | (uint) * (byte*)(param_1 + 0x5)) << 0x4 |
					(uint) * (byte*)(param_1 + 0x6))
				<< 0x4;
		uVar2 = uVar1 | *(byte*)(param_1 + 0x7);
		PCM = (byte)(uVar1 >> 0x8);
	}
	CycleCounter += 0x2;
	PCL = (char)uVar2;
	PCH = (char)(uVar2 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_JE_A_k(byte* param_1)

{
	uint uVar1;
	uint uVar2;

	if (ACC == (byte)(*(char*)(param_1 + 0x2) << 0x4 | *(byte*)(param_1 + 0x3))) {
		uVar1 = (((uint) * (byte*)(param_1 + 0x4) << 0x4 | (uint) * (byte*)(param_1 + 0x5)) << 0x4 |
					(uint) * (byte*)(param_1 + 0x6))
				<< 0x4;
		uVar2 = uVar1 | *(byte*)(param_1 + 0x7);
		PCM = (byte)(uVar1 >> 0x8);
	}
	else {
		uVar2 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x2;
		PCM = (byte)(uVar2 >> 0x8);
	}
	CycleCounter += 0x2;
	PCL = (char)uVar2;
	PCH = (char)(uVar2 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_MOV_k(byte* param_1)

{
	int iVar1;

	do {
		CycleCounter += 0x1;
		ACC = *(char*)(param_1 + 0x2) << 0x4 | *(byte*)(param_1 + 0x3);
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	iVar1 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar1;
	PCM = (char)((uint)iVar1 >> 0x8);
	PCH = (char)((uint)iVar1 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_TBPTRL(byte* param_1)

{
	int iVar1;

	TABPTRL = *(char*)(param_1 + 0x2) << 0x4 | *(byte*)(param_1 + 0x3);
	CycleCounter += 0x1;
	iVar1 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar1;
	PCM = (char)((uint)iVar1 >> 0x8);
	PCH = (char)((uint)iVar1 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_TBPTRM(byte* param_1)

{
	int iVar1;

	TABPTRM = *(char*)(param_1 + 0x2) << 0x4 | *(byte*)(param_1 + 0x3);
	CycleCounter += 0x1;
	iVar1 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar1;
	PCM = (char)((uint)iVar1 >> 0x8);
	PCH = (char)((uint)iVar1 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_TBPTRH(byte* param_1)

{
	int iVar1;

	TABPTRH = *(char*)(param_1 + 0x2) << 0x4 | *(byte*)(param_1 + 0x3);
	CycleCounter += 0x1;
	iVar1 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar1;
	PCM = (char)((uint)iVar1 >> 0x8);
	PCH = (char)((uint)iVar1 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_OR_k(byte* param_1)

{
	int iVar1;

	ACC |= *(char*)(param_1 + 0x2) << 0x4 | *(byte*)(param_1 + 0x3);
	if (ACC == 0x0) {
		STATUS |= 0x4;
	}
	else {
		STATUS &= 0xfb;
	}
	CycleCounter += 0x1;
	iVar1 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar1;
	PCM = (char)((uint)iVar1 >> 0x8);
	PCH = (char)((uint)iVar1 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_AND_k(byte* param_1)

{
	int iVar1;

	ACC &= *(char*)(param_1 + 0x2) << 0x4 | *(byte*)(param_1 + 0x3);
	if (ACC == 0x0) {
		STATUS |= 0x4;
	}
	else {
		STATUS &= 0xfb;
	}
	CycleCounter += 0x1;
	iVar1 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar1;
	PCM = (char)((uint)iVar1 >> 0x8);
	PCH = (char)((uint)iVar1 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_XOR_k(byte* param_1)

{
	int iVar1;

	ACC ^= *(char*)(param_1 + 0x2) << 0x4 | *(byte*)(param_1 + 0x3);
	if (ACC == 0x0) {
		STATUS |= 0x4;
	}
	else {
		STATUS &= 0xfb;
	}
	CycleCounter += 0x1;
	iVar1 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar1;
	PCM = (char)((uint)iVar1 >> 0x8);
	PCH = (char)((uint)iVar1 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_ADD_k(byte* param_1)

{
	ushort uVar1;
	int iVar2;
	byte bVar3;

	uVar1 = (ushort)ACC;
	do {
		uVar1 = (uVar1 & 0xff) +
				(ushort)(byte)(*(char*)(param_1 + 0x2) << 0x4 | *(byte*)(param_1 + 0x3));
		if ((uVar1 & 0xff00) == 0x0) {
			bVar3 = STATUS & 0xfe;
		}
		else {
			bVar3 = STATUS | 0x1;
		}
		if ((char)uVar1 == '\0') {
			STATUS = bVar3 | 0x4;
		}
		else {
			STATUS = bVar3 & 0xfb;
		}
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x1;
	ACC = (char)uVar1;
	iVar2 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar2;
	PCM = (char)((uint)iVar2 >> 0x8);
	PCH = (char)((uint)iVar2 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_ADC_k(byte* param_1)

{
	int iVar1;
	byte bVar2;
	ushort uVar3;

	uVar3 = (ushort)ACC;
	do {
		uVar3 = (ushort)(STATUS & 0x1) + (uVar3 & 0xff) +
				(ushort)(byte)(*(char*)(param_1 + 0x2) << 0x4 | *(byte*)(param_1 + 0x3));
		if ((uVar3 & 0xff00) == 0x0) {
			bVar2 = STATUS & 0xfe;
		}
		else {
			bVar2 = STATUS | 0x1;
		}
		if ((char)uVar3 == '\0') {
			STATUS = bVar2 | 0x4;
		}
		else {
			STATUS = bVar2 & 0xfb;
		}
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	ACC = (char)uVar3;
	CycleCounter += 0x1;
	iVar1 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar1;
	PCM = (char)((uint)iVar1 >> 0x8);
	PCH = (char)((uint)iVar1 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_SUB_k(byte* param_1)

{
	int iVar1;
	byte bVar2;
	ushort uVar3;

	uVar3 = (ushort)ACC;
	do {
		uVar3 = (ushort)(byte)(*(char*)(param_1 + 0x2) << 0x4 | *(byte*)(param_1 + 0x3)) -
				(uVar3 & 0xff);
		if ((uVar3 & 0xff00) == 0xff00) {
			bVar2 = STATUS & 0xfe;
		}
		else {
			bVar2 = STATUS | 0x1;
		}
		if ((char)uVar3 == '\0') {
			STATUS = bVar2 | 0x4;
		}
		else {
			STATUS = bVar2 & 0xfb;
		}
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x1;
	ACC = (char)uVar3;
	iVar1 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar1;
	PCM = (char)((uint)iVar1 >> 0x8);
	PCH = (char)((uint)iVar1 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_SUBB_k(byte* param_1)

{
	int iVar1;
	byte bVar2;
	ushort uVar3;

	uVar3 = (ushort)ACC;
	do {
		uVar3 = ((ushort)(byte)(*(char*)(param_1 + 0x2) << 0x4 | *(byte*)(param_1 + 0x3)) -
					(ushort)((STATUS & 0x1) != 0x1)) -
				(uVar3 & 0xff);
		if ((uVar3 & 0xff00) == 0xff00) {
			bVar2 = STATUS & 0xfe;
		}
		else {
			bVar2 = STATUS | 0x1;
		}
		if ((char)uVar3 == '\0') {
			STATUS = bVar2 | 0x4;
		}
		else {
			STATUS = bVar2 & 0xfb;
		}
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	ACC = (char)uVar3;
	CycleCounter += 0x1;
	iVar1 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar1;
	PCM = (char)((uint)iVar1 >> 0x8);
	PCH = (char)((uint)iVar1 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_UD(byte* param_1)

{
	int iVar1;

	CycleCounter += 0x1;
	iVar1 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar1;
	PCM = (char)((uint)iVar1 >> 0x8);
	PCH = (char)((uint)iVar1 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_RPT(byte* param_1)

{
	byte* pbVar1;
	int iVar2;

	pbVar1 = reg(*(char*)(param_1 + 0x2) << 0x4 | *(byte*)(param_1 + 0x3));
	CycleCounter += 0x1;
	RepeatCount = (ushort)*pbVar1;
	iVar2 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar2;
	PCM = (char)((uint)iVar2 >> 0x8);
	PCH = (char)((uint)iVar2 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_TEST(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;

	bVar1 = *(byte*)(param_1 + 0x3);
	bVar2 = *(char*)(param_1 + 0x2) << 0x4;
	do {
		pbVar3 = reg(bVar2 | bVar1);
		if (*pbVar3 == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		post_pid(bVar2 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_JDNZ_A(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte bVar4;
	byte bVar5;
	uint uVar6;
	byte bVar7;
	byte* pbVar8;
	uint uVar9;
	uint uVar10;
	uint uVar11;

	bVar1 = *(byte*)(param_1 + 0x3);
	bVar2 = *(byte*)(param_1 + 0x5);
	uVar11 = (uint) * (byte*)(param_1 + 0x4) << 0x4;
	uVar10 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar3 = *(byte*)(param_1 + 0x7);
	bVar5 = (byte)uVar10;
	bVar4 = *(byte*)(param_1 + 0x6);
	uVar10 = uVar10 & 0xff | (uint)bVar1;
	do {
		pbVar8 = reg(bVar5 | bVar1);
		bVar7 = *pbVar8 - 0x1;
		ACC = bVar7;
		debug_printf("JDNZ A=%x r=%x(%s) addr0x%x", (uint)bVar7, uVar10, Sfr_in_str + uVar10,
			((uVar11 | bVar2) << 0x4 | (uint)bVar4) << 0x4 | (uint)bVar3);
		if (bVar7 == 0x0) {
			uVar9 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x2;
			PCM = (byte)(uVar9 >> 0x8);
		}
		else {
			uVar6 = ((uVar11 | bVar2) << 0x4 | (uint)bVar4) << 0x4;
			uVar9 = uVar6 | bVar3;
			PCM = (byte)(uVar6 >> 0x8);
		}
		PCL = (byte)uVar9;
		PCH = (undefined)(uVar9 >> 0x10);
		post_pid(bVar5 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x2;
	return;
}

inline void casioemu::ePSCPU::OP_JDNZ_r(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte bVar4;
	byte bVar5;
	byte bVar6;
	uint uVar7;
	byte* pbVar8;
	uint uVar9;
	uint uVar10;
	uint uVar11;
	uint uVar12;

	bVar2 = *(byte*)(param_1 + 0x3);
	bVar3 = *(byte*)(param_1 + 0x5);
	uVar11 = (uint) * (byte*)(param_1 + 0x4) << 0x4;
	uVar10 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar4 = *(byte*)(param_1 + 0x7);
	bVar5 = *(byte*)(param_1 + 0x6);
	uVar12 = uVar10 & 0xff | (uint)bVar2;
	do {
		bVar6 = (byte)uVar10;
		pbVar8 = reg(bVar6 | bVar2);
		bVar1 = *pbVar8;
		pbVar8 = reg(bVar6 | bVar2);
		*pbVar8 = bVar1 - 0x1;
		debug_printf("JDNZ r=0x%x(%s) addr 0x%x", uVar12, Sfr_in_str + uVar12,
			((uVar11 | bVar3) << 0x4 | (uint)bVar5) << 0x4 | (uint)bVar4);
		pbVar8 = reg(bVar6 | bVar2);
		if (*pbVar8 == 0x0) {
			uVar9 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x2;
			PCM = (byte)(uVar9 >> 0x8);
		}
		else {
			uVar7 = ((uVar11 | bVar3) << 0x4 | (uint)bVar5) << 0x4;
			uVar9 = uVar7 | bVar4;
			PCM = (byte)(uVar7 >> 0x8);
		}
		PCL = (byte)uVar9;
		PCH = (undefined)(uVar9 >> 0x10);
		post_pid(bVar6 | bVar2);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x2;
	return;
}

inline void casioemu::ePSCPU::OP_JGE_A_r(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte bVar4;
	byte bVar5;
	uint uVar6;
	byte* pbVar7;
	uint uVar8;
	uint uVar9;
	uint uVar10;

	bVar1 = *(byte*)(param_1 + 0x3);
	bVar2 = *(byte*)(param_1 + 0x5);
	uVar10 = (uint) * (byte*)(param_1 + 0x4) << 0x4;
	uVar9 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar3 = *(byte*)(param_1 + 0x7);
	bVar5 = (byte)uVar9;
	bVar4 = *(byte*)(param_1 + 0x6);
	uVar9 = uVar9 & 0xff | (uint)bVar1;
	do {
		debug_printf("JGE A=0x%x r=0x%x(%s) 0x%x", (uint)ACC, uVar9, Sfr_in_str + uVar9,
			((uVar10 | bVar2) << 0x4 | (uint)bVar4) << 0x4 | (uint)bVar3);
		pbVar7 = reg(bVar5 | bVar1);
		if (ACC < *pbVar7) {
			uVar8 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x2;
			PCM = (byte)(uVar8 >> 0x8);
		}
		else {
			uVar6 = ((uVar10 | bVar2) << 0x4 | (uint)bVar4) << 0x4;
			uVar8 = uVar6 | bVar3;
			PCM = (byte)(uVar6 >> 0x8);
		}
		PCL = (byte)uVar8;
		PCH = (undefined)(uVar8 >> 0x10);
		post_pid(bVar5 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x2;
	return;
}

inline void casioemu::ePSCPU::OP_JLE_A_r(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte bVar4;
	byte bVar5;
	uint uVar6;
	byte* pbVar7;
	uint uVar8;
	uint uVar9;
	uint uVar10;

	bVar1 = *(byte*)(param_1 + 0x3);
	bVar2 = *(byte*)(param_1 + 0x5);
	uVar10 = (uint) * (byte*)(param_1 + 0x4) << 0x4;
	uVar9 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar3 = *(byte*)(param_1 + 0x7);
	bVar5 = (byte)uVar9;
	bVar4 = *(byte*)(param_1 + 0x6);
	uVar9 = uVar9 & 0xff | (uint)bVar1;
	do {
		debug_printf("JLE A=0x%x r=0x%x(%s) addr 0x%x", (uint)ACC, uVar9, Sfr_in_str + uVar9,
			((uVar10 | bVar2) << 0x4 | (uint)bVar4) << 0x4 | (uint)bVar3);
		pbVar7 = reg(bVar5 | bVar1);
		if (*pbVar7 < ACC) {
			uVar8 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x2;
			PCM = (byte)(uVar8 >> 0x8);
		}
		else {
			uVar6 = ((uVar10 | bVar2) << 0x4 | (uint)bVar4) << 0x4;
			uVar8 = uVar6 | bVar3;
			PCM = (byte)(uVar6 >> 0x8);
		}
		PCL = (byte)uVar8;
		PCH = (undefined)(uVar8 >> 0x10);
		post_pid(bVar5 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x2;
	return;
}

inline void casioemu::ePSCPU::OP_JE_A_r(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte bVar4;
	byte bVar5;
	uint uVar6;
	byte* pbVar7;
	uint uVar8;
	uint uVar9;
	uint uVar10;

	bVar1 = *(byte*)(param_1 + 0x3);
	bVar2 = *(byte*)(param_1 + 0x5);
	uVar10 = (uint) * (byte*)(param_1 + 0x4) << 0x4;
	uVar9 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar3 = *(byte*)(param_1 + 0x7);
	bVar5 = (byte)uVar9;
	bVar4 = *(byte*)(param_1 + 0x6);
	uVar9 = uVar9 & 0xff | (uint)bVar1;
	do {
		debug_printf("JE A=0x%x r=0x%x(%s) addr 0x%x", (uint)ACC, uVar9, Sfr_in_str + uVar9,
			((uVar10 | bVar2) << 0x4 | (uint)bVar4) << 0x4 | (uint)bVar3);
		pbVar7 = reg(bVar5 | bVar1);
		if (ACC == *pbVar7) {
			uVar6 = ((uVar10 | bVar2) << 0x4 | (uint)bVar4) << 0x4;
			uVar8 = uVar6 | bVar3;
			PCM = (byte)(uVar6 >> 0x8);
		}
		else {
			uVar8 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x2;
			PCM = (byte)(uVar8 >> 0x8);
		}
		PCL = (byte)uVar8;
		PCH = (undefined)(uVar8 >> 0x10);
		post_pid(bVar5 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x2;
	return;
}

inline void casioemu::ePSCPU::OP_JBC(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	uint uVar4;
	uint uVar5;
	uint uVar6;
	uint uVar7;
	undefined auStack_210[0x3];
	byte local_20d;
	char(*local_20c)[0x14];
	undefined4 local_208;
	WCHAR local_204[0x100];
	uintptr_t local_4;

	local_4 = g_stack_cookie ^ (uintptr_t)auStack_210;
	bVar1 = *(byte*)(param_1 + 0x3);
	uVar5 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar5;
	local_20d = *(char*)(param_1 + 0x1) - 0x8;
	uVar7 = uVar5 & 0xff | (uint)bVar1;
	uVar6 = (((uint) * (byte*)(param_1 + 0x4) << 0x4 | (uint) * (byte*)(param_1 + 0x5)) << 0x4 |
				(uint) * (byte*)(param_1 + 0x6))
				<< 0x4 |
			(uint) * (byte*)(param_1 + 0x7);
	uVar5 = (uint)local_20d;
	local_20c = Sfr_in_str + uVar7;
	do {
		debug_printf("JBC r=0x%x(%s) b=0x%x addr 0x%x", uVar7, local_20c, uVar5, uVar6);
		if ((bVar2 | bVar1) == 0x31) {
			// local_208 = CONCAT31(local_208._1_3_, PORTC | PBCON);
			InvalidatePORTA();
			debug_printf("JBC PORTA %x DCRB&PORTB %x DCRA %x", (uint)PACON,
				(uint)(PORTC & PBCON), (uint)PAWAKE);
		}
		pbVar3 = reg(bVar2 | bVar1);
		uVar4 = uVar6;
		if ((*pbVar3 >> (local_20d & 0x1f) & 0x1) != 0x0) {
			uVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x2;
		}
		PCL = (byte)uVar4;
		PCH = (undefined)(uVar4 >> 0x10);
		PCM = (byte)(uVar4 >> 0x8);
		if (((bVar2 | bVar1) == 0x43) && (local_20d == 0x0)) {
			debug_printf2(local_204, L"r43 is %x\n", (uint)gpr_43);
		}
		post_pid(bVar2 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x2;
	___security_check_cookie_4(local_4 ^ (uintptr_t)auStack_210);
	return;
}

inline void casioemu::ePSCPU::OP_JBS(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte bVar4;
	byte bVar5;
	byte bVar6;
	uint uVar7;
	byte* pbVar8;
	uint uVar9;
	uint uVar10;
	uint uVar11;

	bVar1 = *(byte*)(param_1 + 0x3);
	bVar2 = *(byte*)(param_1 + 0x1);
	uVar10 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar6 = (byte)uVar10;
	bVar3 = *(byte*)(param_1 + 0x5);
	uVar11 = (uint) * (byte*)(param_1 + 0x4) << 0x4;
	bVar4 = *(byte*)(param_1 + 0x6);
	bVar5 = *(byte*)(param_1 + 0x7);
	uVar10 = uVar10 & 0xff | (uint)bVar1;
	do {
		debug_printf("JBS r=%x(%s) b=%x addr 0x%x", uVar10, Sfr_in_str + uVar10, (uint)bVar2,
			((uVar11 | bVar3) << 0x4 | (uint)bVar4) << 0x4 | (uint)bVar5);
		if ((bVar6 | bVar1) == 0x31) {
			InvalidatePORTA();
			debug_printf("JBS PORTA %x DCRB&PORTB %x DCRA %x", (uint)PACON,
				(uint)(PORTC & PBCON), (uint)PAWAKE);
		}
		pbVar8 = reg(bVar6 | bVar1);
		if (((*pbVar8 >> (bVar2 & 0x1f)) & 0x1) == 0x0) {
			uVar9 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x2;
			PCM = (byte)(uVar9 >> 0x8);
		}
		else {
			uVar7 = ((uVar11 | bVar3) << 0x4 | (uint)bVar4) << 0x4;
			uVar9 = uVar7 | bVar5;
			PCM = (byte)(uVar7 >> 0x8);
		}
		PCL = (byte)uVar9;
		PCH = (undefined)(uVar9 >> 0x10);
		post_pid(bVar6 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x2;
	return;
}

inline void casioemu::ePSCPU::OP_MOV_A(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	byte bVar5;
	uint uVar6;
	const char* pcVar7;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar6 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar6;
	bVar5 = bVar2 | bVar1;
	pbVar3 = reg(bVar2 | bVar1);
	uVar6 = uVar6 & 0xff | (uint)bVar1;
	debug_printf("MOV A r=%x(%s) %x", uVar6, Sfr_in_str + uVar6, (uint)*pbVar3);
	do {
		if (bVar5 == 0x31) {
			InvalidatePORTA();
			debug_printf("MOV A PORTA %x DCRB&PORTB %x DCRA %x", (uint)PACON,
				(uint)(PORTC & PBCON), (uint)PAWAKE);
		}
		else if (bVar5 == 0x3a) {
			PCCON = 0x0;
		}
		pbVar3 = reg(bVar5);
		ACC = *pbVar3;
		if (bVar5 == 0x3) {
			pcVar7 = "MOV A INDF1,BSR1 %x, FSR1 %x,value %x";
			bVar1 = BSR1;
			bVar2 = FSR1;
		LAB_00408f9c:
			debug_printf(pcVar7, (uint)bVar1, (uint)bVar2, (uint)ACC);
		}
		else if ((bVar5 == 0x10) || (bVar5 == 0x11)) {
			pcVar7 = "MOV A INDF2,BSR2 %x, FSR2 %x,value %x";
			bVar1 = BSR2;
			bVar2 = FSR2;
			goto LAB_00408f9c;
		}
		if (ACC == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		post_pid(bVar5);
		if ((RepeatCount == 0x0) || (RepeatCount += -0x1, RepeatCount == 0x0)) {
			iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
			PCL = (char)iVar4;
			CycleCounter += 0x1;
			PCM = (char)((uint)iVar4 >> 0x8);
			PCH = (char)((uint)iVar4 >> 0x10);
			return;
		}
	} while (true);
}

inline void casioemu::ePSCPU::OP_MOV_r(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	byte bVar5;
	uint uVar6;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar6 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar6;
	bVar5 = bVar2 | bVar1;
	uVar6 = uVar6 & 0xff | (uint)bVar1;
	debug_printf("MOV r=%x(%s) A %x", uVar6, Sfr_in_str + uVar6, (uint)ACC);
	do {
		pbVar3 = reg(bVar2 | bVar1);
		*pbVar3 = ACC;
		if ((((bVar5 == 0x26) || (bVar5 == 0x27)) || (bVar5 == 0x25)) &&
			(((uint)TRL0H * 0x100 + (uint)TRL0L != 0x0 && ((TR0CON & 0x8) != 0x0)))) {
			UpdateTimerSetting(0x0);
		}
		if (((bVar5 == 0x2b) || (bVar5 == 0x2a)) &&
			((TRL1 != '\0' && ((TR1CON & 0x8) != 0x0)))) {
			UpdateTimerSetting(0x1);
		}
		if (bVar5 == 0x3) {
			debug_printf("MOV r a:INDF1,BSR1 %x, FSR1 %x,value %x", (uint)BSR1, (uint)FSR1, (uint)ACC);
		}
		post_pid(bVar2 | bVar1);
		CycleCounter += 0x1;
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_MOVRP(byte* param_1)

{
	byte bVar1;
	uint uVar2;
	byte* pbVar3;
	byte bVar4;
	uint extraout_ECX;
	uint uVar5;
	int iVar6;
	undefined auStack_210[0x3];
	byte local_20d;
	uint local_20c;
	undefined4 local_208;
	WCHAR local_204[0x100];
	uintptr_t local_4;

	local_4 = g_stack_cookie ^ (uintptr_t)auStack_210;
	bVar1 = (*param_1 << 0x4 | param_1[0x1]) + 0x80;
	local_20c = (uint)(byte)param_1[0x2] << 0x4 | (uint)(byte)param_1[0x3];
	uVar5 = (uint)bVar1;
	uVar2 = (uint)(byte)param_1[0x2] << 0x4 & 0xff | (uint)(byte)param_1[0x3];
	debug_printf("MOVRP p=%x(%s) r=%x(%s)", uVar5, Sfr_in_str + uVar5, uVar2, Sfr_in_str + uVar2);
	uVar2 = uVar5;
	do {
		bVar4 = (byte)uVar2;
		if (bVar4 == 0x31) {
			// local_208 = CONCAT31(local_208._1_3_, PORTC | PBCON);
			InvalidatePORTA();
			// debug_printf("MOVRP p=%x(%s) PORTA %x DCRB&PORTB %x DCRA %x", uVar5, Sfr_in_str + uVar5,(uint)PACON, (uint)(PORTC & PBCON), (uint)PAWAKE);
			bVar4 = (byte)local_20c;
		}
		pbVar3 = reg(bVar4);
		local_20d = *pbVar3;
		pbVar3 = reg(bVar1);
		*pbVar3 = local_20d;
		if (bVar1 == 0xe) {
			// debug_printf("LCDDATA %x ARH(%x) ARL(%x)",
			//	(uint)(byte)(&VRam)[(uint)LCDARL + (LCDARH & 0x3) * 0x60], (uint)LCDARH,
			//	(uint)LCDARL);
		}
		else if (bVar1 == 0x0) {
			// debug_printf2(local_204, L"BSR 0 FSR f2, data p %x\n", (uint)DAT_0041e192);
		}
		if (((((char)local_20c == '\0') || (bVar1 == 0x0)) || ((char)local_20c == '\x03')) ||
			(bVar1 == 0x3)) {
			// debug_printf2(local_204, L"BSR 0 FSR %x, data r %x\n", (uint)FSR, (uint)(byte)(&EmuMem)[FSR]);
			pbVar3 = reg(bVar1);
			// debug_printf("MOVRP BSR %x FSR %x,BSR1 %x,FSR1 %x,BSR2 %x,FSR2 %x,data %x", (uint)BSR, (uint)FSR, (uint)BSR1, (uint)FSR1, (uint)BSR2, (uint)FSR2, (uint)*pbVar3);
		}
		post_pid((char)local_20c);
		post_pid(bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, uVar2 = local_20c, RepeatCount != 0x0));
	if (bVar1 == 0xe) {
		InstFlags |= 0x2;
		uVar2 = 0xf0;
		iVar6 = 0xa;
		do {
			uVar5 = uVar2;
			// debug_printf("BANK 0 r%x, data %x", uVar2, (uint)(byte)(&EmuMem)[uVar2]);
			// debug_printf2(local_204, L"BANK 0 r%x, data %x\n", uVar5, (uint)(byte)(&EmuMem)[uVar5]);
			uVar2 = (uint)(byte)((char)uVar2 + 0x1);
			iVar6 += -0x1;
		} while (iVar6 != 0x0);
	}
	CycleCounter += 0x1;
	iVar6 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCM = (byte)((uint)iVar6 >> 0x8);
	PCL = (byte)iVar6;
	PCH = (undefined)((uint)iVar6 >> 0x10);
	___security_check_cookie_4(local_4 ^ (uintptr_t)auStack_210);
	return;
}

inline void casioemu::ePSCPU::OP_MOVPR(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	uint uVar4;
	byte* pbVar5;
	int iVar6;
	uint uVar7;
	undefined auStack_20c[0x2];
	byte local_20a;
	byte local_209;
	undefined4 local_208;
	WCHAR local_204[0x100];
	uintptr_t local_4;

	local_4 = g_stack_cookie ^ (uintptr_t)auStack_20c;
	bVar1 = param_1[0x3];
	bVar2 = (byte)((uint)(byte)param_1[0x2] << 0x4);
	uVar7 = (uint)(byte)param_1[0x2] << 0x4 & 0xff | (uint)bVar1;
	local_20a = (*param_1 << 0x4 | param_1[0x1]) + 0x60;
	uVar4 = (uint)local_20a;
	debug_printf("MOVPR r=%x(%s) p=%x(%s)", uVar7, Sfr_in_str + uVar7, uVar4, Sfr_in_str + uVar4);
	while (true) {
		bVar3 = (byte)uVar4;
		if ((byte)uVar4 == 0x31) {
			// local_208 = CONCAT31(local_208._1_3_, PORTC | PBCON);
			InvalidatePORTA();
			debug_printf("MOVPR r=%x(%s) PORTA %x DCRB&PORTB %x DCRA %x", uVar7, Sfr_in_str + uVar7,
				(uint)PACON, (uint)(PORTC & PBCON), (uint)PAWAKE);
			bVar3 = local_20a;
		}
		pbVar5 = reg(bVar3);
		local_209 = *pbVar5;
		pbVar5 = reg(bVar2 | bVar1);
		*pbVar5 = local_209;
		if (local_20a == 0x3) {
			debug_printf2(local_204, L"BSR 0 FSR %x, data r %x\n", (uint)FSR, (uint)(byte)(&EmuMem)[FSR]);
			pbVar5 = reg(0x3);
			debug_printf("MOVPR r p BSR1 %x,FSR1 %x,data %x", (uint)BSR1, (uint)FSR1, (uint)*pbVar5);
		}
		post_pid(bVar2 | bVar1);
		post_pid(local_20a);
		if ((RepeatCount == 0x0) || (RepeatCount += -0x1, RepeatCount == 0x0))
			break;
		uVar4 = (uint)local_20a;
	}
	CycleCounter += 0x1;
	iVar6 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (byte)iVar6;
	PCM = (byte)((uint)iVar6 >> 0x8);
	PCH = (undefined)((uint)iVar6 >> 0x10);
	___security_check_cookie_4(local_4 ^ (uintptr_t)auStack_20c);
	return;
}

inline void casioemu::ePSCPU::OP_CLR(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	uint uVar5;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar5 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar5;
	uVar5 = uVar5 & 0xff | (uint)bVar1;
	debug_printf("CLR r=%x(%s)", uVar5, Sfr_in_str + uVar5);
	do {
		pbVar3 = reg(bVar2 | bVar1);
		*pbVar3 = 0x0;
		STATUS |= 0x4;
		if ((bVar2 | bVar1) == 0x31) {
			InvalidatePORTA();
			debug_printf("CLR PORTA %x DCRB&PORTB %x DCRA %x", (uint)PACON,
				(uint)(PORTC & PBCON), (uint)PAWAKE);
		}
		else if ((bVar2 | bVar1) == 0x3) {
			debug_printf("CLR INDF1 BSR1 %x,FSR1 %x", (uint)BSR1, (uint)FSR1);
		}
		post_pid(bVar2 | bVar1);
		CycleCounter += 0x1;
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_TBRD(byte* param_1)

{
	char cVar1;
	byte* pbVar2;
	uint uVar3;
	int iVar4;
	int iVar5;
	uint uVar6;
	undefined auStack_21c[0x3];
	byte local_219;
	int local_218;
	uint local_214;
	uint local_210;
	char(*local_20c)[0x14];
	uint local_208;
	WCHAR local_204[0x100];
	uintptr_t local_4;

	local_4 = g_stack_cookie ^ (uintptr_t)auStack_21c;
	local_219 = *(char*)(param_1 + 0x1) - 0xc;
	uVar3 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	local_210 = uVar3 | *(byte*)(param_1 + 0x3);
	uVar6 = uVar3 & 0xff | (uint) * (byte*)(param_1 + 0x3);
	uVar3 = ((uint)TABPTRH * 0x100 + (uint)TABPTRM) * 0x100 + (uint)TABPTRL;
	local_20c = Sfr_in_str + uVar6;
	local_208 = (uint)local_219;
	local_218 = uVar3 - 0x1;
	iVar4 = uVar3 * 0x2 + 0x2;
	do {
		iVar5 = iVar4;
		if ((uVar3 & 0x1) != 0x0) {
			iVar5 = local_218 * 0x2;
		}
		local_214 = (uint) * (byte*)(Rom + 0x1 + iVar5) | (uint) * (byte*)(Rom + iVar5) << 0x4;
		pbVar2 = reg((byte)local_210);
		*pbVar2 = (byte)local_214;
		local_214 = (uint) * (byte*)(Rom + 0x1 + iVar5) | (uint) * (byte*)(Rom + iVar5) << 0x4;
		cVar1 = (char)local_210;
		if ((((char)local_210 == '\0') && (BSR == '\0')) && ((byte)(FSR + 0x10) < 0xa)) {
			debug_printf2(local_204, L"TBRD,BSR 0 FSR %x, data %x\n", (uint)FSR, (uint)(byte)(&EmuMem)[FSR]);
			cVar1 = (char)local_210;
		}
		if (local_219 != 0x0) {
			if (local_219 == 0x1) {
				uVar3 += 0x1;
				iVar4 += 0x2;
				local_218 += 0x1;
			}
			else if (local_219 == 0x2) {
				uVar3 -= 0x1;
				iVar4 += -0x2;
				local_218 += -0x1;
			}
		}
		TABPTRM = (byte)(uVar3 >> 0x8);
		TABPTRH = (byte)(uVar3 >> 0x10);
		TABPTRL = (byte)uVar3;
		debug_printf("TBRD i=%x r=%x(%s) addr %x, value %x", local_208, uVar6, local_20c, uVar3,
			local_214 & 0xff);
		if (cVar1 == '\x03') {
			debug_printf("TBRD BSR1 %x,FSR1 %x ", (uint)BSR1, (uint)FSR1);
		}
		post_pid(cVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCM = (byte)((uint)iVar4 >> 0x8);
	PCL = (byte)iVar4;
	PCH = (undefined)((uint)iVar4 >> 0x10);
	___security_check_cookie_4(local_4 ^ (uintptr_t)auStack_21c);
	return;
}

inline void casioemu::ePSCPU::OP_TBRD_A(byte* param_1)

{
	char cVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	byte bVar5;
	uint uVar6;
	uint uVar7;

	uVar6 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar5 = (byte)uVar6 | *(byte*)(param_1 + 0x3);
	uVar6 = uVar6 & 0xff | (uint) * (byte*)(param_1 + 0x3);
	uVar7 = ((uint)TABPTRH * 0x100 + (uint)TABPTRM) * 0x100 + (uint)TABPTRL + (uint)ACC;
	do {
		iVar4 = uVar7 * 0x2 + 0x2;
		if ((uVar7 & 0x1) != 0x0) {
			iVar4 = uVar7 * 0x2 + -0x2;
		}
		cVar1 = *(char*)(Rom + iVar4);
		bVar2 = *(byte*)(Rom + 0x1 + iVar4);
		pbVar3 = reg(bVar5);
		*pbVar3 = cVar1 << 0x4 | bVar2;
		debug_printf("TBRD A=%x r=%x(%s) addr1 %x, addr2 %x, value %x", (uint)ACC, uVar6,
			Sfr_in_str + uVar6, uVar7, iVar4,
			(*(byte*)(Rom + iVar4) & 0xf) << 0x4 | (uint) * (byte*)(Rom + 0x1 + iVar4));
		bVar2 = bVar5;
		if (bVar5 == 0x3) {
			debug_printf("TBRD BSR1 %x,FSR1 %x ", (uint)BSR1, (uint)FSR1);
		}
		post_pid(bVar2);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_OR_A(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	uint uVar5;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar5 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar5;
	uVar5 = uVar5 & 0xff | (uint)bVar1;
	do {
		pbVar3 = reg(bVar2 | bVar1);
		ACC |= *pbVar3;
		if (ACC == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		debug_printf("OR A r=%x(%s)", uVar5, Sfr_in_str + uVar5);
		post_pid(bVar2 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_OR_R(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	byte bVar5;
	uint uVar6;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar6 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar6;
	bVar5 = bVar2 | bVar1;
	uVar6 = uVar6 & 0xff | (uint)bVar1;
	do {
		if ((bVar5 == 0x3) || (bVar5 == 0x10)) {
			pbVar3 = reg(bVar2 | bVar1);
			debug_printf("OR r: BSR1 %x,FSR1 %x,BSR2 %x,FSR2 %x,a %x,before data %x,z flag %x", (uint)BSR1,
				(uint)FSR1, (uint)BSR2, (uint)FSR2, (uint)ACC, (uint)*pbVar3, STATUS >> 0x2 & 0x1);
		}
		pbVar3 = reg(bVar2 | bVar1);
		*pbVar3 = *pbVar3 | ACC;
		pbVar3 = reg(bVar2 | bVar1);
		if (*pbVar3 == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		debug_printf("OR r=%x(%s) A %x", uVar6, Sfr_in_str + uVar6, (uint)ACC);
		if ((bVar5 == 0x3) || (bVar5 == 0x10)) {
			pbVar3 = reg(bVar2 | bVar1);
			debug_printf("OR r: BSR1 %x,FSR1 %x,BSR2 %x,FSR2 %x,a %x,after data %x,z flag %x", (uint)BSR1,
				(uint)FSR1, (uint)BSR2, (uint)FSR2, (uint)ACC, (uint)*pbVar3, STATUS >> 0x2 & 0x1);
		}
		post_pid(bVar2 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	InstFlags |= 0x2;
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_AND_A(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	uint uVar5;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar5 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar5;
	uVar5 = uVar5 & 0xff | (uint)bVar1;
	debug_printf("AND A r=%x(%s)", uVar5, Sfr_in_str + uVar5);
	do {
		pbVar3 = reg(bVar2 | bVar1);
		ACC &= *pbVar3;
		if (ACC == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		post_pid(bVar2 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_AND_r(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	byte bVar5;
	uint uVar6;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar6 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar6;
	bVar5 = bVar2 | bVar1;
	uVar6 = uVar6 & 0xff | (uint)bVar1;
	debug_printf("AND r=%x(%s) A", uVar6, Sfr_in_str + uVar6);
	do {
		if ((bVar5 == 0x3) || (bVar5 == 0x10)) {
			pbVar3 = reg(bVar2 | bVar1);
			debug_printf("AND r: BSR1 %x,FSR1 %x,BSR2 %x,FSR2 %x,a %x,before data %x,z flag %x", (uint)BSR1, (uint)FSR1, (uint)BSR2, (uint)FSR2, (uint)ACC, (uint)*pbVar3, STATUS >> 0x2 & 0x1);
		}
		pbVar3 = reg(bVar2 | bVar1);
		*pbVar3 = *pbVar3 & ACC;
		pbVar3 = reg(bVar2 | bVar1);
		if (*pbVar3 == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		if ((bVar5 == 0x3) || (bVar5 == 0x10)) {
			pbVar3 = reg(bVar2 | bVar1);
			debug_printf("AND r: BSR1 %x,FSR1 %x,BSR2 %x,FSR2 %x,a %x,after data %x,z flag %x", (uint)BSR1,
				(uint)FSR1, (uint)BSR2, (uint)FSR2, (uint)ACC, (uint)*pbVar3, STATUS >> 0x2 & 0x1);
		}
		post_pid(bVar2 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_XOR_A(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	uint uVar5;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar5 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar5;
	uVar5 = uVar5 & 0xff | (uint)bVar1;
	do {
		debug_printf("XOR A r=%x(%s)", uVar5, Sfr_in_str + uVar5);
		pbVar3 = reg(bVar2 | bVar1);
		ACC ^= *pbVar3;
		if (ACC == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		post_pid(bVar2 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_XOR_r(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	uint uVar5;
	uint uVar6;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar5 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	uVar6 = uVar5 & 0xff | (uint)bVar1;
	do {
		debug_printf("XOR r=%x(%s) A", uVar6, Sfr_in_str + uVar6);
		bVar2 = (byte)uVar5;
		pbVar3 = reg(bVar2 | bVar1);
		*pbVar3 = *pbVar3 ^ ACC;
		pbVar3 = reg(bVar2 | bVar1);
		if (*pbVar3 == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		post_pid(bVar2 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_COMA(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	uint uVar5;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar5 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar5;
	uVar5 = uVar5 & 0xff | (uint)bVar1;
	do {
		pbVar3 = reg(bVar2 | bVar1);
		ACC = ~*pbVar3;
		if (ACC == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		debug_printf("COMA r=%x(%s) ", uVar5, Sfr_in_str + uVar5);
		post_pid(bVar2 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_COM(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte* pbVar4;
	int iVar5;
	uint uVar6;
	uint uVar7;

	bVar2 = *(byte*)(param_1 + 0x3);
	uVar6 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	uVar7 = uVar6 & 0xff | (uint)bVar2;
	do {
		bVar3 = (byte)uVar6;
		pbVar4 = reg(bVar3 | bVar2);
		bVar1 = *pbVar4;
		pbVar4 = reg(bVar3 | bVar2);
		*pbVar4 = ~bVar1;
		pbVar4 = reg(bVar3 | bVar2);
		if (*pbVar4 == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		debug_printf("COM r=%x(%s) ", uVar7, Sfr_in_str + uVar7);
		post_pid(bVar3 | bVar2);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	iVar5 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar5;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar5 >> 0x8);
	PCH = (char)((uint)iVar5 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_INCA(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte* pbVar4;
	int iVar5;
	uint uVar6;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar6 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar6;
	uVar6 = uVar6 & 0xff | (uint)bVar1;
	do {
		pbVar4 = reg(bVar2 | bVar1);
		bVar3 = *pbVar4;
		pbVar4 = reg(bVar2 | bVar1);
		ACC = (char)(bVar3 + 0x1);
		if ((((bVar2 | bVar1) == 0x4) || ((bVar2 | bVar1) == 0x11)) && (*pbVar4 == 0xff)) {
			if (ACC == '\0') {
				STATUS |= 0x4;
			}
			else {
				STATUS &= 0xfb;
			}
		}
		if ((bVar3 + 0x1 & 0xff00) == 0x0) {
			bVar3 = STATUS & 0xfe;
		}
		else {
			bVar3 = STATUS | 0x1;
		}
		if (ACC == '\0') {
			STATUS = bVar3 | 0x4;
		}
		else {
			STATUS = bVar3 & 0xfb;
		}
		debug_printf("INCA r=%x(%s) ", uVar6, Sfr_in_str + uVar6);
		post_pid(bVar2 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	iVar5 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar5;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar5 >> 0x8);
	PCH = (char)((uint)iVar5 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_INC(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte* pbVar4;
	int iVar5;
	uint extraout_EDX = 0;
	byte bVar6;
	uint uVar7;
	byte local_4;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar7 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar3 = (byte)uVar7;
	bVar6 = bVar3 | bVar1;
	uVar7 = uVar7 & 0xff | (uint)bVar1;
	do {
		pbVar4 = reg(bVar3 | bVar1);
		debug_printf("INC r=%x(%s) value %x", uVar7, Sfr_in_str + uVar7, (uint)*pbVar4);
		pbVar4 = reg(bVar3 | bVar1);
		bVar2 = *pbVar4;
		local_4 = (byte)(bVar2 + 0x1);
		if (bVar6 != 0x13) {
			pbVar4 = reg(bVar3 | bVar1);
			*pbVar4 = local_4;
		}
		if ((bVar2 + 0x1 & 0xff00) == 0x0) {
			STATUS &= 0xfe;
		}
		else {
			STATUS |= 0x1;
		}
		if (local_4 == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		auto_carry(bVar3 | bVar1);
		if (bVar6 == 0xa) {
			debug_printf("INC A %x, i %x ,Z flag %x", (uint)ACC, extraout_EDX & 0xffff, STATUS >> 0x2 & 0x1);
		}
		else if (bVar6 == 0x11) {
			debug_printf("INC FSR2 %x", (uint)FSR2);
		}
		post_pid(bVar3 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	iVar5 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar5;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar5 >> 0x8);
	PCH = (char)((uint)iVar5 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_ADD_A(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	ushort uVar5;
	uint uVar6;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar6 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar6;
	uVar6 = uVar6 & 0xff | (uint)bVar1;
	do {
		debug_printf("ADD A r=%x(%s ", uVar6, Sfr_in_str + uVar6);
		pbVar3 = reg(bVar2 | bVar1);
		uVar5 = (ushort)ACC;
		ACC = (byte)(uVar5 + *pbVar3);
		if ((uVar5 + *pbVar3 & 0xff00) == 0x0) {
			STATUS &= 0xfe;
		}
		else {
			STATUS |= 0x1;
		}
		if (ACC == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		post_pid(bVar2 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_ADD_r(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte* pbVar4;
	int iVar5;
	byte bVar6;
	ushort uVar7;
	uint uVar8;
	uint uVar9;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar8 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	uVar9 = uVar8 & 0xff | (uint)bVar1;
	do {
		debug_printf("ADD r=%x(%s) A", uVar9, Sfr_in_str + uVar9);
		bVar3 = (byte)uVar8;
		pbVar4 = reg(bVar3 | bVar1);
		bVar2 = *pbVar4;
		uVar7 = (ushort)ACC;
		pbVar4 = reg(bVar3 | bVar1);
		bVar6 = (byte)(bVar2 + uVar7);
		*pbVar4 = bVar6;
		if ((bVar2 + uVar7 & 0xff00) == 0x0) {
			STATUS &= 0xfe;
		}
		else {
			STATUS |= 0x1;
		}
		if (bVar6 == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		bVar3 |= bVar1;
		auto_carry(bVar3);
		post_pid(bVar3);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	iVar5 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar5;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar5 >> 0x8);
	PCH = (char)((uint)iVar5 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_ADC_A(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	byte bVar5;
	ushort uVar6;
	uint uVar7;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar7 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar7;
	uVar7 = uVar7 & 0xff | (uint)bVar1;
	do {
		debug_printf("ADC A r=%x(%s) ", uVar7, Sfr_in_str + uVar7);
		pbVar3 = reg(bVar2 | bVar1);
		uVar6 = (ushort)(STATUS & 0x1) + (ushort)*pbVar3 + (ushort)ACC;
		ACC = (byte)uVar6;
		if ((uVar6 & 0xff00) == 0x0) {
			bVar5 = STATUS & 0xfe;
		}
		else {
			bVar5 = STATUS | 0x1;
		}
		if (ACC == 0x0) {
			STATUS = bVar5 | 0x4;
		}
		else {
			STATUS = bVar5 & 0xfb;
		}
		post_pid(bVar2 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_ADC_r(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	byte bVar5;
	ushort uVar6;
	uint uVar7;
	uint uVar8;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar7 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	uVar8 = uVar7 & 0xff | (uint)bVar1;
	do {
		debug_printf("ADC r=%x(%s) A", uVar8, Sfr_in_str + uVar8);
		bVar2 = (byte)uVar7;
		pbVar3 = reg(bVar2 | bVar1);
		uVar6 = (ushort)(STATUS & 0x1) + (ushort)*pbVar3 + (ushort)ACC;
		pbVar3 = reg(bVar2 | bVar1);
		bVar5 = (byte)uVar6;
		*pbVar3 = bVar5;
		if ((uVar6 & 0xff00) == 0x0) {
			STATUS &= 0xfe;
		}
		else {
			STATUS |= 0x1;
		}
		if (bVar5 == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		bVar2 |= bVar1;
		auto_carry(bVar2);
		post_pid(bVar2);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_DECA(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	uint uVar5;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar5 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar5;
	uVar5 = uVar5 & 0xff | (uint)bVar1;
	do {
		pbVar3 = reg(bVar2 | bVar1);
		ACC = (char)(*pbVar3 - 0x1);
		if ((*pbVar3 - 0x1 & 0xff00) == 0xff00) {
			STATUS &= 0xfe;
		}
		else {
			STATUS |= 0x1;
		}
		if (ACC == '\0') {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		debug_printf("DECA r=%x(%s)", uVar5, Sfr_in_str + uVar5);
		post_pid(bVar2 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_DEC(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte* pbVar4;
	int iVar5;
	byte bVar6;
	uint uVar7;
	uint uVar8;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar7 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	uVar8 = uVar7 & 0xff | (uint)bVar1;
	do {
		debug_printf("DEC r=%x(%s)", uVar8, Sfr_in_str + uVar8);
		bVar3 = (byte)uVar7;
		pbVar4 = reg(bVar3 | bVar1);
		bVar2 = *pbVar4;
		pbVar4 = reg(bVar3 | bVar1);
		bVar6 = (byte)(bVar2 - 0x1);
		*pbVar4 = bVar6;
		if ((bVar2 - 0x1 & 0xff00) == 0xff00) {
			STATUS &= 0xfe;
		}
		else {
			STATUS |= 0x1;
		}
		if (bVar6 == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		auto_borrow(bVar3 | bVar1);
		post_pid(bVar3 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	iVar5 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar5;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar5 >> 0x8);
	PCH = (char)((uint)iVar5 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_SUB_A(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	uint uVar4;
	int iVar5;
	uint uVar6;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar6 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar6;
	uVar6 = uVar6 & 0xff | (uint)bVar1;
	do {
		if ((bVar2 | bVar1) == 0x11) {
			debug_printf("SUB A FSR2, A %x FSR2 %x", (uint)ACC, (uint)FSR2);
		}
		pbVar3 = reg(bVar2 | bVar1);
		uVar4 = (uint)(ushort)((ushort)*pbVar3 - (ushort)ACC);
		ACC = (byte)((ushort)*pbVar3 - (ushort)ACC);
		if ((uVar4 & 0xff00) == 0xff00) {
			STATUS &= 0xfe;
		}
		else {
			STATUS |= 0x1;
		}
		if (ACC == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		debug_printf("SUB A r=%x(%s)", uVar6, Sfr_in_str + uVar6);
		if ((bVar2 | bVar1) == 0x11) {
			debug_printf("SUB A FSR2, after A %x FSR2 %x", uVar4 & 0xff, (uint)FSR2);
		}
		post_pid(bVar2 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x1;
	iVar5 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar5;
	PCM = (char)((uint)iVar5 >> 0x8);
	PCH = (char)((uint)iVar5 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_SUB_r(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	ushort uVar4;
	byte* pbVar5;
	int iVar6;
	byte bVar7;
	uint uVar8;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar8 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar3 = (byte)uVar8;
	uVar8 = uVar8 & 0xff | (uint)bVar1;
	do {
		debug_printf("SUB r=%x(%s) A", uVar8, Sfr_in_str + uVar8);
		if ((bVar3 | bVar1) == 0x3) {
			pbVar5 = reg(bVar3 | bVar1);
			debug_printf("SUB INDF1 A: BSR1 %x,FSR1 %x,before indf1 data %x, a %x", (uint)BSR1, (uint)FSR1,
				(uint)*pbVar5, (uint)ACC);
		}
		pbVar5 = reg(bVar3 | bVar1);
		bVar2 = *pbVar5;
		uVar4 = (ushort)ACC;
		pbVar5 = reg(bVar3 | bVar1);
		bVar7 = (byte)(bVar2 - uVar4);
		*pbVar5 = bVar7;
		if ((bVar2 - uVar4 & 0xff00) == 0xff00) {
			STATUS &= 0xfe;
		}
		else {
			STATUS |= 0x1;
		}
		if (bVar7 == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		auto_borrow(bVar3 | bVar1);
		if ((bVar3 | bVar1) == 0x3) {
			pbVar5 = reg(bVar3 | bVar1);
			debug_printf("SUB INDF1 A: BSR1 %x,FSR1 %x,after indf1 data %x", (uint)BSR1, (uint)FSR1,
				(uint)*pbVar5);
		}
		post_pid(bVar3 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	iVar6 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar6;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar6 >> 0x8);
	PCH = (char)((uint)iVar6 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_SUBB_A(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	ushort uVar3;
	byte* pbVar4;
	int iVar5;
	byte bVar6;
	uint uVar7;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar7 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar7;
	uVar7 = uVar7 & 0xff | (uint)bVar1;
	do {
		bVar6 = STATUS & 0x1;
		debug_printf("SUBB A r=%x(%s)", uVar7, Sfr_in_str + uVar7);
		pbVar4 = reg(bVar2 | bVar1);
		uVar3 = ((ushort)*pbVar4 - (ushort)ACC) - (ushort)(bVar6 != 0x1);
		ACC = (byte)uVar3;
		if ((uVar3 & 0xff00) == 0xff00) {
			STATUS &= 0xfe;
		}
		else {
			STATUS |= 0x1;
		}
		if (ACC == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		post_pid(bVar2 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x1;
	iVar5 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar5;
	PCM = (char)((uint)iVar5 >> 0x8);
	PCH = (char)((uint)iVar5 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_SUBB_r(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	ushort uVar3;
	byte* pbVar4;
	int iVar5;
	byte bVar6;
	uint uVar7;
	uint uVar8;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar7 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	uVar8 = uVar7 & 0xff | (uint)bVar1;
	do {
		bVar6 = STATUS & 0x1;
		debug_printf("SUBB r=%x(%s) A", uVar8, Sfr_in_str + uVar8);
		bVar2 = (byte)uVar7;
		pbVar4 = reg(bVar2 | bVar1);
		uVar3 = ((ushort)*pbVar4 - (ushort)ACC) - (ushort)(bVar6 != 0x1);
		pbVar4 = reg(bVar2 | bVar1);
		bVar6 = (byte)uVar3;
		*pbVar4 = bVar6;
		if ((uVar3 & 0xff00) == 0xff00) {
			STATUS &= 0xfe;
		}
		else {
			STATUS |= 0x1;
		}
		if (bVar6 == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		auto_borrow(bVar2 | bVar1);
		post_pid(bVar2 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	iVar5 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar5;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar5 >> 0x8);
	PCH = (char)((uint)iVar5 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_ADDDC_A(byte* param_1)

{
	byte bVar1;
	ushort uVar2;
	byte* pbVar3;
	int iVar4;
	ushort uVar5;
	uint uVar6;

	uVar6 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar1 = (byte)uVar6 | *(byte*)(param_1 + 0x3);
	uVar6 = uVar6 & 0xff | (uint) * (byte*)(param_1 + 0x3);
	do {
		debug_printf("ADDDC A r=%x(%s)", uVar6, Sfr_in_str + uVar6);
		pbVar3 = reg(bVar1);
		uVar2 = (ushort)(STATUS & 0x1) + (ushort)ACC;
		uVar5 = uVar2 + *pbVar3;
		if ((0x9 < ((byte)uVar5 & 0xf)) || (0xf < (ushort)((uVar2 & 0xf) + (*pbVar3 & 0xf)))) {
			uVar5 += 0x6;
		}
		if ((0x90 < (uVar5 & 0xf0)) || (0xff < uVar5)) {
			uVar5 += 0x60;
		}
		ACC = (byte)uVar5;
		if ((uVar5 & 0xff00) == 0x0) {
			STATUS &= 0xfe;
		}
		else {
			STATUS |= 0x1;
		}
		if (ACC == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		post_pid(bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_ADDDC_r(byte* param_1)

{
	char cVar1;
	byte* pbVar2;
	uint uVar3;
	int iVar4;
	byte bVar5;
	uint uVar6;
	uint uVar7;
	uint uVar8;
	uint local_208;
	WCHAR local_204[0x100];
	uintptr_t local_4;

	local_4 = g_stack_cookie ^ (uintptr_t)&local_208;
	uVar6 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	local_208 = uVar6 | *(byte*)(param_1 + 0x3);
	uVar6 = uVar6 & 0xff | (uint) * (byte*)(param_1 + 0x3);
	do {
		debug_printf("ADDDC r=%x(%s) A", uVar6, Sfr_in_str + uVar6);
		pbVar2 = reg(uVar6);
		uVar7 = (uint)*pbVar2;
		uVar3 = (uint)(ushort)((ushort)(STATUS & 0x1) + (ushort)ACC);
		uVar8 = uVar3 + uVar7;
		if ((0x9 < ((byte)uVar8 & 0xf)) || (0xf < (uVar3 & 0xf) + (uVar7 & 0xf))) {
			uVar8 += 0x6;
		}
		if ((0x90 < (uVar8 & 0xf0)) || (0xff < (ushort)uVar8)) {
			uVar8 += 0x60;
		}
		pbVar2 = reg((byte)local_208);
		*pbVar2 = (byte)uVar8;
		if ((uVar8 & 0xff00) == 0x0) {
			bVar5 = STATUS & 0xfe;
		}
		else {
			bVar5 = STATUS | 0x1;
		}
		if ((byte)uVar8 == 0x0) {
			STATUS = bVar5 | 0x4;
		}
		else {
			STATUS = bVar5 & 0xfb;
		}
		cVar1 = (byte)local_208;
		if ((((byte)local_208 == '\0') && (BSR == '\0')) && ((byte)(FSR + 0x10) < 0xa)) {
			debug_printf2(local_204, L"ADDDC R A %x,c %x,temp %x,BSR 0 FSR %x, data %x\n", (uint)ACC,
				STATUS & 0x1, uVar7, (uint)FSR, (uint)(byte)(&EmuMem)[FSR]);
		}
		post_pid(cVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (byte)iVar4;
	PCM = (byte)((uint)iVar4 >> 0x8);
	PCH = (undefined)((uint)iVar4 >> 0x10);
	___security_check_cookie_4(local_4 ^ (uintptr_t)&local_208);
	return;
}

inline void casioemu::ePSCPU::OP_SUBDB_A(byte* param_1)

{
	byte bVar1;
	byte* pbVar2;
	int iVar3;
	ushort uVar4;
	ushort uVar5;

	bVar1 = *(char*)(param_1 + 0x2) << 0x4 | *(byte*)(param_1 + 0x3);
	do {
		pbVar2 = reg(bVar1);
		uVar4 = (0x9a - (ushort)((STATUS & 0x1) != 0x1)) - (ushort)ACC;
		uVar5 = uVar4 + *pbVar2;
		if ((0x9 < ((byte)uVar5 & 0xf)) || (0xf < (ushort)((uVar4 & 0xf) + (*pbVar2 & 0xf)))) {
			uVar5 += 0x6;
		}
		if ((0x90 < (uVar5 & 0xf0)) || (0xff < uVar5)) {
			uVar5 += 0x60;
		}
		ACC = (byte)uVar5;
		if ((uVar5 & 0xff00) == 0x0) {
			STATUS &= 0xfe;
		}
		else {
			STATUS |= 0x1;
		}
		if (ACC == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		post_pid(bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	iVar3 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar3;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar3 >> 0x8);
	PCH = (char)((uint)iVar3 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_SUBDB_r(byte* param_1)

{
	byte* pbVar1;
	int iVar2;
	byte bVar3;
	ushort uVar4;
	ushort uVar5;

	bVar3 = *(char*)(param_1 + 0x2) << 0x4 | *(byte*)(param_1 + 0x3);
	do {
		pbVar1 = reg(bVar3);
		uVar4 = (0x9a - (ushort)((STATUS & 0x1) != 0x1)) - (ushort)ACC;
		uVar5 = uVar4 + *pbVar1;
		if ((0x9 < ((byte)uVar5 & 0xf)) || (0xf < (ushort)((uVar4 & 0xf) + (*pbVar1 & 0xf)))) {
			uVar5 += 0x6;
		}
		if ((0x90 < (uVar5 & 0xf0)) || (0xff < uVar5)) {
			uVar5 += 0x60;
		}
		pbVar1 = reg(bVar3);
		*pbVar1 = (byte)uVar5;
		if ((uVar5 & 0xff00) == 0x0) {
			STATUS &= 0xfe;
		}
		else {
			STATUS |= 0x1;
		}
		if ((byte)uVar5 == 0x0) {
			STATUS |= 0x4;
		}
		else {
			STATUS &= 0xfb;
		}
		post_pid(bVar3);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	iVar2 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar2;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar2 >> 0x8);
	PCH = (char)((uint)iVar2 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_RRCA(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	uint uVar5;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar5 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar5;
	uVar5 = uVar5 & 0xff | (uint)bVar1;
	do {
		pbVar3 = reg(bVar2 | bVar1);
		ACC = STATUS << 0x7 | *pbVar3 >> 0x1;
		STATUS ^= (STATUS ^ *pbVar3) & 0x1;
		debug_printf("RRCA r=%x(%s)", uVar5, Sfr_in_str + uVar5);
		post_pid(bVar2 | bVar1);
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_RRC(byte* param_1)

{
	byte bVar1;
	byte* pbVar2;
	int iVar3;
	byte bVar4;
	uint uVar5;
	byte bVar6;

	uVar5 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar4 = (byte)uVar5 | *(byte*)(param_1 + 0x3);
	uVar5 = uVar5 & 0xff | (uint) * (byte*)(param_1 + 0x3);
	do {
		pbVar2 = reg(bVar4);
		bVar1 = *pbVar2;
		bVar6 = STATUS << 0x7;
		pbVar2 = reg(bVar4);
		*pbVar2 = bVar6 | bVar1 >> 0x1;
		STATUS ^= (STATUS ^ bVar1) & 0x1;
		debug_printf("RRC r=%x(%s)", uVar5, Sfr_in_str + uVar5);
		post_pid(bVar4);
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	iVar3 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar3;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar3 >> 0x8);
	PCH = (char)((uint)iVar3 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_RLCA(byte* param_1)

{
	byte bVar1;
	byte* pbVar2;
	int iVar3;
	uint uVar4;

	uVar4 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar1 = (byte)uVar4 | *(byte*)(param_1 + 0x3);
	uVar4 = uVar4 & 0xff | (uint) * (byte*)(param_1 + 0x3);
	do {
		pbVar2 = reg(bVar1);
		ACC = STATUS & 0x1 | *pbVar2 * '\x02';
		STATUS = STATUS & 0xfe | *pbVar2 >> 0x7;
		debug_printf("RLCA r=%x(%s)", uVar4, Sfr_in_str + uVar4);
		post_pid(bVar1);
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	iVar3 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar3;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar3 >> 0x8);
	PCH = (char)((uint)iVar3 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_RLC(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte bVar4;
	byte* pbVar5;
	int iVar6;
	byte bVar7;
	uint uVar8;

	bVar2 = *(byte*)(param_1 + 0x3);
	uVar8 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar3 = (byte)uVar8;
	bVar7 = bVar3 | bVar2;
	uVar8 = uVar8 & 0xff | (uint)bVar2;
	do {
		if ((bVar7 == 0x3) || (bVar7 == 0x10)) {
			pbVar5 = reg(bVar3 | bVar2);
			debug_printf("RLC r: BSR1 %x,FSR1 %x,BSR2 %x,FSR2 %x,before data %x, c flag %x", (uint)BSR1,
				(uint)FSR1, (uint)BSR2, (uint)FSR2, (uint)*pbVar5, STATUS & 0x1);
		}
		pbVar5 = reg(bVar3 | bVar2);
		bVar1 = *pbVar5;
		bVar4 = STATUS & 0x1;
		pbVar5 = reg(bVar3 | bVar2);
		*pbVar5 = bVar4 | bVar1 * '\x02';
		STATUS = STATUS & 0xfe | bVar1 >> 0x7;
		if ((bVar7 == 0x3) || (bVar7 == 0x10)) {
			pbVar5 = reg(bVar3 | bVar2);
			debug_printf("RLC r: BSR1 %x,FSR1 %x,BSR2 %x,FSR2 %x,after data %x,c flag %x", (uint)BSR1,
				(uint)FSR1, (uint)BSR2, (uint)FSR2, (uint)*pbVar5, STATUS & 0x1);
		}
		debug_printf("RLC r=%x(%s)", uVar8, Sfr_in_str + uVar8);
		post_pid(bVar3 | bVar2);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	iVar6 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar6;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar6 >> 0x8);
	PCH = (char)((uint)iVar6 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_SHRA(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	uint uVar5;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar5 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar5;
	uVar5 = uVar5 & 0xff | (uint)bVar1;
	do {
		pbVar3 = reg(bVar2 | bVar1);
		ACC = STATUS << 0x7 | *pbVar3 >> 0x1;
		debug_printf("SHRA r=%x(%s)", uVar5, Sfr_in_str + uVar5);
		post_pid(bVar2 | bVar1);
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_SHLA(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	uint uVar5;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar5 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar5;
	uVar5 = uVar5 & 0xff | (uint)bVar1;
	do {
		pbVar3 = reg(bVar2 | bVar1);
		ACC = STATUS & 0x1 | *pbVar3 * '\x02';
		debug_printf("SHLA r=%x(%s)", uVar5, Sfr_in_str + uVar5);
		post_pid(bVar2 | bVar1);
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_EX_r(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	uint uVar4;
	byte* pbVar5;
	int iVar6;
	uint uVar7;

	bVar2 = *(byte*)(param_1 + 0x3);
	uVar7 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	uVar4 = uVar7 & 0xff | (uint)bVar2;
	debug_printf("EX r=%x(%s)", uVar4, Sfr_in_str + uVar4);
	do {
		bVar3 = (byte)uVar7;
		pbVar5 = reg(bVar3 | bVar2);
		bVar1 = *pbVar5;
		pbVar5 = reg(bVar3 | bVar2);
		*pbVar5 = ACC;
		ACC = bVar1;
		post_pid(bVar3 | bVar2);
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	iVar6 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar6;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar6 >> 0x8);
	PCH = (char)((uint)iVar6 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_BC(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	byte bVar5;
	uint uVar6;
	uint uVar7;
	undefined auStack_20c[0x2];
	byte local_20a;
	byte local_209;
	undefined4 local_208;
	WCHAR local_204[0x100];
	uintptr_t local_4;

	local_4 = g_stack_cookie ^ (uintptr_t)auStack_20c;
	bVar1 = *(byte*)(param_1 + 0x3);
	local_20a = *(char*)(param_1 + 0x1) - 0x8;
	uVar7 = (uint)local_20a;
	uVar6 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar6;
	bVar5 = bVar2 | bVar1;
	uVar6 = uVar6 & 0xff | (uint)bVar1;
	local_209 = ~('\x01' << (local_20a & 0x1f));
	do {
		pbVar3 = reg(bVar2 | bVar1);
		*pbVar3 = *pbVar3 & local_209;
		if (bVar5 == 0x31) {
			// local_208 = CONCAT31(local_208._1_3_, PORTC | PBCON);
			InvalidatePORTA();
			debug_printf("BC PORTA %x DCRB&PORTB %x DCRA %x", (uint)PACON,
				(uint)(PORTC & PBCON), (uint)PAWAKE);
		}
		else if (bVar5 == 0x43) {
			if (local_20a == 0x0) {
				debug_printf2(local_204, L"bit clear of r43 %x\n", (uint)gpr_43);
			}
		}
		else if ((bVar5 == 0x20) && (local_20a == 0x1)) {
			InvalidateTimerSetting(0x0);
			InvalidateTimerSetting(0x1);
			InvalidateTimerSetting(0x2);
		}
		debug_printf("BC r(b) r=%x(%s) b=%x", uVar6, Sfr_in_str + uVar6, uVar7);
		post_pid(bVar2 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCM = (byte)((uint)iVar4 >> 0x8);
	PCL = (byte)iVar4;
	PCH = (undefined)((uint)iVar4 >> 0x10);
	___security_check_cookie_4(local_4 ^ (uintptr_t)auStack_20c);
	return;
}

inline void casioemu::ePSCPU::OP_BS(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte* pbVar4;
	int iVar5;
	byte bVar6;
	uint uVar7;
	undefined auStack_210[0x3];
	byte local_20d;
	uint local_20c;
	undefined4 local_208;
	WCHAR local_204[0x100];
	uintptr_t local_4;

	local_4 = g_stack_cookie ^ (uintptr_t)auStack_210;
	bVar1 = *(byte*)(param_1 + 0x3);
	bVar2 = *(byte*)(param_1 + 0x1);
	uVar7 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar3 = (byte)uVar7;
	bVar6 = bVar3 | bVar1;
	local_20d = '\x01' << (bVar2 & 0x1f);
	uVar7 = uVar7 & 0xff | (uint)bVar1;
	local_20c = (uint)bVar2;
	do {
		pbVar4 = reg(bVar3 | bVar1);
		*pbVar4 = *pbVar4 | local_20d;
		if (bVar6 == 0x31) {
			// local_208 = CONCAT31(local_208._1_3_, PORTC | PBCON);
			InvalidatePORTA();
			debug_printf("BS PORTA %x DCRB&PORTB %x DCRA %x", (uint)PACON,
				(uint)(PORTC & PBCON), (uint)PAWAKE);
		}
		else {
			if ((bVar6 == 0x3) || (bVar6 == 0x10)) {
				pbVar4 = reg(bVar3 | bVar1);
				debug_printf("BS r: BSR1 %x,FSR1 %x,BSR2 %x,FSR2 %x,after data %x", (uint)BSR1, (uint)FSR1,
					(uint)BSR2, (uint)FSR2, (uint)*pbVar4);
			}
			if ((bVar6 == 0x2a) && ((TR1CON & 0x8) != 0x0)) {
				UpdateTimerSetting(0x1);
			}
		}
		debug_printf("BS r(b) r=%x(%s) b=%x", uVar7, Sfr_in_str + uVar7, (uint)bVar2);
		if (bVar6 == 0x2a) {
			if ((char)local_20c == '\x04') {
				debug_printf("TR1CON is %x", (uint)TR1CON);
			}
		}
		else if ((bVar6 == 0x43) && ((char)local_20c == '\0')) {
			debug_printf2(local_204, L"bit set of r43 %x\n", (uint)gpr_43);
		}
		post_pid(bVar3 | bVar1);
	} while ((RepeatCount != 0x0) && (RepeatCount += -0x1, RepeatCount != 0x0));
	CycleCounter += 0x1;
	iVar5 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCM = (byte)((uint)iVar5 >> 0x8);
	PCL = (byte)iVar5;
	PCH = (undefined)((uint)iVar5 >> 0x10);
	___security_check_cookie_4(local_4 ^ (uintptr_t)auStack_210);
	return;
}

inline void casioemu::ePSCPU::OP_BTG(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte* pbVar4;
	int iVar5;
	uint uVar6;

	bVar1 = *(byte*)(param_1 + 0x3);
	bVar3 = *(char*)(param_1 + 0x1) - 0x8;
	uVar6 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar6;
	uVar6 = uVar6 & 0xff | (uint)bVar1;
	do {
		pbVar4 = reg(bVar2 | bVar1);
		*pbVar4 = *pbVar4 ^ '\x01' << (bVar3 & 0x1f);
		debug_printf("BTG r(b) r=%x(%s) b=%x", uVar6, Sfr_in_str + uVar6, (uint)bVar3);
		post_pid(bVar2 | bVar1);
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	iVar5 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar5;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar5 >> 0x8);
	PCH = (char)((uint)iVar5 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_EXL_r(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte* pbVar4;
	int iVar5;
	byte bVar6;
	uint uVar7;

	uVar7 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar3 = (byte)uVar7 | *(byte*)(param_1 + 0x3);
	uVar7 = uVar7 & 0xff | (uint) * (byte*)(param_1 + 0x3);
	do {
		pbVar4 = reg(bVar3);
		bVar1 = *pbVar4;
		pbVar4 = reg(bVar3);
		bVar6 = *pbVar4 ^ ACC;
		bVar2 = *pbVar4;
		pbVar4 = reg(bVar3);
		*pbVar4 = bVar6 & 0xf ^ bVar2;
		ACC = ACC & 0xf0 | bVar1 & 0xf;
		debug_printf("EXL r=%x(%s)", uVar7, Sfr_in_str + uVar7);
		post_pid(bVar3);
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	iVar5 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar5;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar5 >> 0x8);
	PCH = (char)((uint)iVar5 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_EXH_r(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	byte bVar5;
	byte bVar6;
	uint uVar7;

	uVar7 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar5 = (byte)uVar7 | *(byte*)(param_1 + 0x3);
	uVar7 = uVar7 & 0xff | (uint) * (byte*)(param_1 + 0x3);
	do {
		pbVar3 = reg(bVar5);
		bVar1 = *pbVar3;
		pbVar3 = reg(bVar5);
		bVar2 = *pbVar3;
		bVar6 = ACC << 0x4;
		pbVar3 = reg(bVar5);
		*pbVar3 = bVar6 | bVar2 & 0xf;
		ACC = ACC & 0xf0 | bVar1 >> 0x4;
		debug_printf("EXH r=%x(%s)", uVar7, Sfr_in_str + uVar7);
		post_pid(bVar5);
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_MOVL_r(byte* param_1)

{
	byte bVar1;
	byte* pbVar2;
	int iVar3;
	byte bVar4;
	uint uVar5;
	byte bVar6;

	uVar5 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar4 = (byte)uVar5 | *(byte*)(param_1 + 0x3);
	uVar5 = uVar5 & 0xff | (uint) * (byte*)(param_1 + 0x3);
	do {
		pbVar2 = reg(bVar4);
		bVar6 = *pbVar2 ^ ACC;
		bVar1 = *pbVar2;
		pbVar2 = reg(bVar4);
		*pbVar2 = bVar6 & 0xf ^ bVar1;
		debug_printf("MOVL r=%x(%s) A", uVar5, Sfr_in_str + uVar5);
		post_pid(bVar4);
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	iVar3 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar3;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar3 >> 0x8);
	PCH = (char)((uint)iVar3 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_MOVH_r(byte* param_1)

{
	byte bVar1;
	byte* pbVar2;
	int iVar3;
	byte bVar4;
	byte bVar5;
	uint uVar6;

	uVar6 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar4 = (byte)uVar6 | *(byte*)(param_1 + 0x3);
	uVar6 = uVar6 & 0xff | (uint) * (byte*)(param_1 + 0x3);
	do {
		pbVar2 = reg(bVar4);
		bVar1 = *pbVar2;
		bVar5 = ACC << 0x4;
		pbVar2 = reg(bVar4);
		*pbVar2 = bVar1 & 0xf | bVar5;
		debug_printf("MOVH r=%x(%s) A", uVar6, Sfr_in_str + uVar6);
		post_pid(bVar4);
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	iVar3 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar3;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar3 >> 0x8);
	PCH = (char)((uint)iVar3 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_MOVL_A(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	uint uVar5;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar5 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar5;
	uVar5 = uVar5 & 0xff | (uint)bVar1;
	do {
		pbVar3 = reg(bVar2 | bVar1);
		ACC = *pbVar3 & 0xf;
		debug_printf("MOVL A r=%x(%s)", uVar5, Sfr_in_str + uVar5);
		post_pid(bVar2 | bVar1);
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_MOVH_A(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	uint uVar5;

	bVar1 = *(byte*)(param_1 + 0x3);
	uVar5 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar2 = (byte)uVar5;
	uVar5 = uVar5 & 0xff | (uint)bVar1;
	do {
		pbVar3 = reg(bVar2 | bVar1);
		ACC = *pbVar3 >> 0x4;
		debug_printf("MOVH A r=%x(%s)", uVar5, Sfr_in_str + uVar5);
		post_pid(bVar2 | bVar1);
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	CycleCounter += 0x1;
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_SFR4(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte* pbVar3;
	int iVar4;
	byte bVar5;
	uint uVar6;

	uVar6 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar5 = (byte)uVar6 | *(byte*)(param_1 + 0x3);
	uVar6 = uVar6 & 0xff | (uint) * (byte*)(param_1 + 0x3);
	do {
		bVar2 = ACC;
		pbVar3 = reg(bVar5);
		ACC ^= (*pbVar3 ^ ACC) & 0xf;
		pbVar3 = reg(bVar5);
		bVar1 = *pbVar3;
		pbVar3 = reg(bVar5);
		*pbVar3 = bVar1 >> 0x4;
		pbVar3 = reg(bVar5);
		bVar1 = *pbVar3;
		pbVar3 = reg(bVar5);
		*pbVar3 = bVar2 << 0x4 | bVar1 & 0xf;
		debug_printf("SFR4 r=%x(%s)", uVar6, Sfr_in_str + uVar6);
		post_pid(bVar5);
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	iVar4 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar4;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar4 >> 0x8);
	PCH = (char)((uint)iVar4 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_SFL4(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte* pbVar4;
	int iVar5;
	uint uVar6;
	uint uVar7;
	byte local_1;

	bVar2 = *(byte*)(param_1 + 0x3);
	uVar6 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	uVar7 = uVar6 & 0xff | (uint)bVar2;
	do {
		bVar3 = (byte)uVar6;
		local_1 = ACC & 0xf;
		pbVar4 = reg(bVar3 | bVar2);
		ACC = *pbVar4 >> 0x4 | ACC & 0xf0;
		pbVar4 = reg(bVar3 | bVar2);
		bVar1 = *pbVar4;
		pbVar4 = reg(bVar3 | bVar2);
		*pbVar4 = bVar1 << 0x4;
		pbVar4 = reg(bVar3 | bVar2);
		bVar1 = *pbVar4;
		pbVar4 = reg(bVar3 | bVar2);
		*pbVar4 = bVar1 & 0xf0 | local_1;
		debug_printf("SFL4 r=%x(%s)", uVar7, Sfr_in_str + uVar7);
		post_pid(bVar3 | bVar2);
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	iVar5 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar5;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar5 >> 0x8);
	PCH = (char)((uint)iVar5 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_SWAP(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte* pbVar4;
	int iVar5;
	uint uVar6;

	bVar2 = *(byte*)(param_1 + 0x3);
	uVar6 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	bVar3 = (byte)uVar6;
	pbVar4 = reg(bVar3 | bVar2);
	bVar1 = *pbVar4;
	uVar6 = uVar6 & 0xff | (uint)bVar2;
	do {
		pbVar4 = reg(bVar3 | bVar2);
		*pbVar4 = bVar1 << 0x4 | bVar1 >> 0x4;
		debug_printf("SWAP r=%x(%s)", uVar6, Sfr_in_str + uVar6);
		post_pid(bVar3 | bVar2);
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	iVar5 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar5;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar5 >> 0x8);
	PCH = (char)((uint)iVar5 >> 0x10);
	return;
}

inline void casioemu::ePSCPU::OP_SWAPA(byte* param_1)

{
	byte bVar1;
	byte bVar2;
	byte bVar3;
	byte* pbVar4;
	int iVar5;
	uint uVar6;
	uint uVar7;

	bVar2 = *(byte*)(param_1 + 0x3);
	uVar6 = (uint) * (byte*)(param_1 + 0x2) << 0x4;
	uVar7 = uVar6 & 0xff | (uint)bVar2;
	do {
		bVar3 = (byte)uVar6;
		pbVar4 = reg(bVar3 | bVar2);
		bVar1 = *pbVar4;
		pbVar4 = reg(bVar3 | bVar2);
		ACC = *pbVar4 << 0x4 | bVar1 >> 0x4;
		debug_printf("SWAPA r=%x(%s)", uVar7, Sfr_in_str + uVar7);
		post_pid(bVar3 | bVar2);
		if (RepeatCount == 0x0)
			break;
		RepeatCount += -0x1;
	} while (RepeatCount != 0x0);
	iVar5 = (ushort)((ushort)PCM * 0x100 + (ushort)PCL) + 0x1;
	PCL = (char)iVar5;
	CycleCounter += 0x1;
	PCM = (char)((uint)iVar5 >> 0x8);
	PCH = (char)((uint)iVar5 >> 0x10);
	return;
}