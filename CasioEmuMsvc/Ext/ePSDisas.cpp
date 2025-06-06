#include <cstdint>
#include <cstdio>
#include <minwindef.h>
#define READ_WORD_BE(ptr)         \
	((uint32_t)(ptr)[0] << 16 |   \
		(uint32_t)(ptr)[1] << 8 | \
		(uint32_t)(ptr)[2] << 4 | \
		(uint32_t)(ptr)[3])

char* decodeeps(char* rom, int pc) {
	unsigned int* v6;	  // edi
	unsigned __int16 v9;  // a
	int v11;			  // eax
	int v13;			  // [esp+Ch] [ebp-20h]
	int v14;			  // [esp+10h] [ebp-1Ch]
	unsigned __int16 v15; // [esp+14h] [ebp-18h]
	int v16;			  // [esp+14h] [ebp-18h]
	char v7[200];

	v15 = READ_WORD_BE(&rom[pc]);
	v9 = READ_WORD_BE(&rom[pc + 1]);
	if (v15 > 2u) {
		if (v15 == 11262) {
			sprintf_s(v7, "%05X : \tRET\n", pc);
		}
		else {
			if (v15 != 11263) {
			LABEL_19:
				if (v15 >> 4 == 2) {
					sprintf_s(v7, "%05X : \tLJMP\t0x%05X\n", pc, v9 | ((v15 & 0xF) << 16));
				}
				else if (v15 >> 4 == 3) {
					sprintf_s(v7, "%05X : \tLCALL\t0x%05X\n", pc, v9 | ((v15 & 0xF) << 16));
				}
				else {
					v16 = HIBYTE(v15);
					switch (v15 >> 8) {
					case 2u:
						sprintf_s(v7, "%05X : \tOR\tA, 0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 3u:
						sprintf_s(v7, "%05X : \tOR\t0x%02X, A\n", pc, (unsigned __int8)v15);
						break;
					case 4u:
						sprintf_s(v7, "%05X : \tAND\tA, 0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 5u:
						sprintf_s(v7, "%05X : \tAND\t0x%02X, A\n", pc, (unsigned __int8)v15);
						break;
					case 6u:
						sprintf_s(v7, "%05X : \tXOR\tA, 0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 7u:
						sprintf_s(v7, "%05X : \tXOR\t0x%02X, A\n", pc, (unsigned __int8)v15);
						break;
					case 8u:
						sprintf_s(v7, "%05X : \tCOMA\t0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 9u:
						sprintf_s(v7, "%05X : \tCOM\t0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0xAu:
						sprintf_s(v7, "%05X : \tRRCA\t0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0xBu:
						sprintf_s(v7, "%05X : \tRRC\t0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0xCu:
						sprintf_s(v7, "%05X : \tRLCA\t0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0xDu:
						sprintf_s(v7, "%05X : \tRLC\t0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0xEu:
						sprintf_s(v7, "%05X : \tSWAPA\t0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0xFu:
						sprintf_s(v7, "%05X : \tSWAP\t0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x10u:
						sprintf_s(v7, "%05X : \tADD\tA, 0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x11u:
						sprintf_s(v7, "%05X : \tADD\t0x%02X, A\n", pc, (unsigned __int8)v15);
						break;
					case 0x12u:
						sprintf_s(v7, "%05X : \tADC\tA, 0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x13u:
						sprintf_s(v7, "%05X : \tADC\t0x%02X, A\n", pc, (unsigned __int8)v15);
						break;
					case 0x14u:
						sprintf_s(v7, "%05X : \tADDDC\tA, 0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x15u:
						sprintf_s(v7, "%05X : \tADDDC\t0x%02X, A\n", pc, (unsigned __int8)v15);
						break;
					case 0x16u:
						sprintf_s(v7, "%05X : \tSUB\tA, 0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x17u:
						sprintf_s(v7, "%05X : \tSUB\t0x%02X, A\n", pc, (unsigned __int8)v15);
						break;
					case 0x18u:
						sprintf_s(v7, "%05X : \tSUBB\tA, 0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x19u:
						sprintf_s(v7, "%05X : \tSUBB\t0x%02X, A\n", pc, (unsigned __int8)v15);
						break;
					case 0x1Au:
						sprintf_s(v7, "%05X : \tSUBDB\tA, 0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x1Bu:
						sprintf_s(v7, "%05X : \tSUBDB\t0x%02X, A\n", pc, (unsigned __int8)v15);
						break;
					case 0x1Cu:
						sprintf_s(v7, "%05X : \tINCA\t0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x1Du:
						sprintf_s(v7, "%05X : \tINC\t0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x1Eu:
						sprintf_s(v7, "%05X : \tDECA\t0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x1Fu:
						sprintf_s(v7, "%05X : \tDEC\t0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x20u:
						sprintf_s(v7, "%05X : \tMOV\tA, 0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x21u:
						sprintf_s(v7, "%05X : \tMOV\t0x%02X, A\n", pc, (unsigned __int8)v15);
						break;
					case 0x22u:
						sprintf_s(v7, "%05X : \tSHRA\t0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x23u:
						sprintf_s(v7, "%05X : \tSHLA\t0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x24u:
						sprintf_s(v7, "%05X : \tCLR\t0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x25u:
						sprintf_s(v7, "%05X : \tTEST\t0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x26u:
						sprintf_s(v7, "%05X : \tMUL\tA, 0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x27u:
						sprintf_s(v7, "%05X : \tRPT\t0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x2Cu:
						sprintf_s(v7, "%05X : \tTBRD\t0, 0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x2Du:
						sprintf_s(v7, "%05X : \tTBRD\t1, 0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x2Eu:
						sprintf_s(v7, "%05X : \tTBRD\t2, 0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x2Fu:
						sprintf_s(v7, "%05X : \tTBRD\tA, 0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x40u:
						sprintf_s(v7, "%05X : \tTBPTL\t#0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x41u:
						sprintf_s(v7, "%05X : \tTBPTM\t#0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x42u:
						sprintf_s(v7, "%05X : \tTBPTH\t#0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x43u:
						sprintf_s(v7, "%05X : \tBANK\t#0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x44u:
						sprintf_s(v7, "%05X : \tOR\tA, #0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x45u:
						sprintf_s(v7, "%05X : \tAND\tA, #0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x46u:
						sprintf_s(v7, "%05X : \tXOR\tA, #0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x47u:
						sprintf_s(v7, "%05X : \tJGE\tA, #0x%02X, 0x%04X\n", pc, (unsigned __int8)v15, v9);
						break;
					case 0x48u:
						sprintf_s(v7, "%05X : \tJLE\tA, #0x%02X, 0x%04X\n", pc, (unsigned __int8)v15, v9);
						break;
					case 0x49u:
						sprintf_s(v7, "%05X : \tJE\tA, #0x%02X, 0x%04X\n", pc, (unsigned __int8)v15, v9);
						break;
					case 0x4Au:
						sprintf_s(v7, "%05X : \tADD\tA, #0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x4Bu:
						sprintf_s(v7, "%05X : \tADC\tA, #0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x4Cu:
						sprintf_s(v7, "%05X : \tSUB\tA, #0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x4Du:
						sprintf_s(v7, "%05X : \tSUBB\tA, #0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x4Eu:
						sprintf_s(v7, "%05X : \tMOV\tA, #0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x4Fu:
						sprintf_s(v7, "%05X : \tMUL\tA, #0x%02X\n", pc, (unsigned __int8)v15);
						break;
					case 0x50u:
						sprintf_s(v7, "%05X : \tJDNZ\tA, 0x%02X, 0x%04X\n", pc, (unsigned __int8)v15, v9);
						break;
					case 0x51u:
						sprintf_s(v7, "%05X : \tJDNZ\t0x%02X, 0x%04X\n", pc, (unsigned __int8)v15, v9);
						break;
					case 0x52u:
						sprintf_s(v7, "%05X : \tJINZ\tA, 0x%02X, 0x%04X\n", pc, (unsigned __int8)v15, v9);
						break;
					case 0x53u:
						sprintf_s(v7, "%05X : \tJINZ\t0x%02X, 0x%04X\n", pc, (unsigned __int8)v15, v9);
						break;
					case 0x55u:
						sprintf_s(v7, "%05X : \tJGE\tA, 0x%02X, 0x%04X\n", pc, (unsigned __int8)v15, v9);
						break;
					case 0x56u:
						sprintf_s(v7, "%05X : \tJLE\tA, 0x%02X, 0x%04X\n", pc, (unsigned __int8)v15, v9);
						break;
					case 0x57u:
						sprintf_s(v7, "%05X : \tJE\tA, 0x%02X, 0x%04X\n", pc, (unsigned __int8)v15, v9);
						break;
					default:
						v13 = v16 & 7;
						switch (v15 >> 11) {
						case 0xBu:
							sprintf_s(v7, "%05X : \tJBC\t0x%02X, %01X, 0x%04X\n", pc, (unsigned __int8)v15, v13, v9);
							break;
						case 0xCu:
							sprintf_s(v7, "%05X : \tJBS\t0x%02X, %01X, 0x%04X\n", pc, (unsigned __int8)v15, v13, v9);
							break;
						case 0xDu:
							sprintf_s(v7, "%05X : \tBC\t0x%02X, %01X\n", pc, (unsigned __int8)v15, v13);
							break;
						case 0xEu:
							sprintf_s(v7, "%05X : \tBS\t0x%02X, %01X\n", pc, (unsigned __int8)v15, v13);
							break;
						case 0xFu:
							sprintf_s(v7, "%05X : \tBTG\t0x%02X, %01X\n", pc, (unsigned __int8)v15, v13);
							break;
						default:
							if ((v15 & 0xFFFFF000) == 12288) {
								sprintf_s(v7, "%05X : \tS0CALL\t0x%04X\n", pc, v15 & 0xFFF);
							}
							else {
								v11 = v15 & 0x1FFF;
								switch (v15 >> 13) {
								case 4u:
									sprintf_s(v7, "%05X : \tMOVRP\t0x%02X, 0x%02X\n", pc, v16, (unsigned __int8)v15);
									break;
								case 5u:
									sprintf_s(v7, "%05X : \tMOVPR\t0x%02X, 0x%02X\n", pc, (unsigned __int8)v15, v16);
									break;
								case 6u:
									sprintf_s(v7, "%05X : \tSJMP\t0x%04X\n", pc, v11);
									break;
								case 7u:
									sprintf_s(v7, "%05X : \tSCALL\t0x%04X\n", pc, v11);
									break;
								default:
									sprintf_s(v7, "Unknown instruction\n");
									break;
								}
							}
							break;
						}
						break;
					}
				}
				goto LABEL_105;
			}
			sprintf_s(v7, "%05X : \tRETI\n", pc);
		}
	}
	else if (v15 == 2) {
		sprintf_s(v7, "%05X : \tSLEP\n", pc);
	}
	else if (v15) {
		if (v15 != 1)
			goto LABEL_19;
		sprintf_s(v7, "%05X : \tWDTC\n", pc);
	}
	else {
		sprintf_s(v7, "%05X : \tNOP\n", pc);
	}
LABEL_105:
	// Íê³É
	return strdup(v7);
}