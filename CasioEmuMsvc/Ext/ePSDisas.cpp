#include <cstdint>
#include <cstdio>
#include <iostream>
#include <windows.h>
#define READ_WORD_BE(ptr) \
	((ptr)[0] << 12 |     \
		(ptr)[1] << 8 |   \
		(ptr)[2] << 4 |   \
		(ptr)[3])

char* decodeeps(char* rom, int pc, bool& l) {
	unsigned int* v6;	  // edi
	unsigned __int16 v9;  // a
	int v11;			  // eax
	int v13;			  // [esp+Ch] [ebp-20h]
	int v14;			  // [esp+10h] [ebp-1Ch]
	unsigned __int16 v15; // [esp+14h] [ebp-18h]
	int v16;			  // [esp+14h] [ebp-18h]
	char v7[200];

	v15 = READ_WORD_BE(&rom[(pc) << 2]);
	v9 = READ_WORD_BE(&rom[(pc + 1) << 2]);
	l = false;
	if (v15 > 2u) {
		if (v15 == 11262) {
			sprintf_s(v7, "RET");
		}
		else {
			if (v15 != 11263) {
			LABEL_19:
				if (v15 >> 4 == 2) {
					sprintf_s(v7, "LJMP\t0x%05X", v9 | ((v15 & 0xF) << 16));
					l = true;
				}
				else if (v15 >> 4 == 3) {
					sprintf_s(v7, "LCALL\t0x%05X", v9 | ((v15 & 0xF) << 16));
					l = true;
				}
				else {
					v16 = HIBYTE(v15);
					switch (v15 >> 8) {
					case 2u:
						sprintf_s(v7, "OR\tA, 0x%02X", (unsigned __int8)v15);
						break;
					case 3u:
						sprintf_s(v7, "OR\t0x%02X, A", (unsigned __int8)v15);
						break;
					case 4u:
						sprintf_s(v7, "AND\tA, 0x%02X", (unsigned __int8)v15);
						break;
					case 5u:
						sprintf_s(v7, "AND\t0x%02X, A", (unsigned __int8)v15);
						break;
					case 6u:
						sprintf_s(v7, "XOR\tA, 0x%02X", (unsigned __int8)v15);
						break;
					case 7u:
						sprintf_s(v7, "XOR\t0x%02X, A", (unsigned __int8)v15);
						break;
					case 8u:
						sprintf_s(v7, "COMA\t0x%02X", (unsigned __int8)v15);
						break;
					case 9u:
						sprintf_s(v7, "COM\t0x%02X", (unsigned __int8)v15);
						break;
					case 0xAu:
						sprintf_s(v7, "RRCA\t0x%02X", (unsigned __int8)v15);
						break;
					case 0xBu:
						sprintf_s(v7, "RRC\t0x%02X", (unsigned __int8)v15);
						break;
					case 0xCu:
						sprintf_s(v7, "RLCA\t0x%02X", (unsigned __int8)v15);
						break;
					case 0xDu:
						sprintf_s(v7, "RLC\t0x%02X", (unsigned __int8)v15);
						break;
					case 0xEu:
						sprintf_s(v7, "SWAPA\t0x%02X", (unsigned __int8)v15);
						break;
					case 0xFu:
						sprintf_s(v7, "SWAP\t0x%02X", (unsigned __int8)v15);
						break;
					case 0x10u:
						sprintf_s(v7, "ADD\tA, 0x%02X", (unsigned __int8)v15);
						break;
					case 0x11u:
						sprintf_s(v7, "ADD\t0x%02X, A", (unsigned __int8)v15);
						break;
					case 0x12u:
						sprintf_s(v7, "ADC\tA, 0x%02X", (unsigned __int8)v15);
						break;
					case 0x13u:
						sprintf_s(v7, "ADC\t0x%02X, A", (unsigned __int8)v15);
						break;
					case 0x14u:
						sprintf_s(v7, "ADDDC\tA, 0x%02X", (unsigned __int8)v15);
						break;
					case 0x15u:
						sprintf_s(v7, "ADDDC\t0x%02X, A", (unsigned __int8)v15);
						break;
					case 0x16u:
						sprintf_s(v7, "SUB\tA, 0x%02X", (unsigned __int8)v15);
						break;
					case 0x17u:
						sprintf_s(v7, "SUB\t0x%02X, A", (unsigned __int8)v15);
						break;
					case 0x18u:
						sprintf_s(v7, "SUBB\tA, 0x%02X", (unsigned __int8)v15);
						break;
					case 0x19u:
						sprintf_s(v7, "SUBB\t0x%02X, A", (unsigned __int8)v15);
						break;
					case 0x1Au:
						sprintf_s(v7, "SUBDB\tA, 0x%02X", (unsigned __int8)v15);
						break;
					case 0x1Bu:
						sprintf_s(v7, "SUBDB\t0x%02X, A", (unsigned __int8)v15);
						break;
					case 0x1Cu:
						sprintf_s(v7, "INCA\t0x%02X", (unsigned __int8)v15);
						break;
					case 0x1Du:
						sprintf_s(v7, "INC\t0x%02X", (unsigned __int8)v15);
						break;
					case 0x1Eu:
						sprintf_s(v7, "DECA\t0x%02X", (unsigned __int8)v15);
						break;
					case 0x1Fu:
						sprintf_s(v7, "DEC\t0x%02X", (unsigned __int8)v15);
						break;
					case 0x20u:
						sprintf_s(v7, "MOV\tA, 0x%02X", (unsigned __int8)v15);
						break;
					case 0x21u:
						sprintf_s(v7, "MOV\t0x%02X, A", (unsigned __int8)v15);
						break;
					case 0x22u:
						sprintf_s(v7, "SHRA\t0x%02X", (unsigned __int8)v15);
						break;
					case 0x23u:
						sprintf_s(v7, "SHLA\t0x%02X", (unsigned __int8)v15);
						break;
					case 0x24u:
						sprintf_s(v7, "CLR\t0x%02X", (unsigned __int8)v15);
						break;
					case 0x25u:
						sprintf_s(v7, "TEST\t0x%02X", (unsigned __int8)v15);
						break;
					case 0x26u:
						sprintf_s(v7, "MUL\tA, 0x%02X", (unsigned __int8)v15);
						break;
					case 0x27u:
						sprintf_s(v7, "RPT\t0x%02X", (unsigned __int8)v15);
						break;
					case 0x2Cu:
						sprintf_s(v7, "TBRD\t0, 0x%02X", (unsigned __int8)v15);
						break;
					case 0x2Du:
						sprintf_s(v7, "TBRD\t1, 0x%02X", (unsigned __int8)v15);
						break;
					case 0x2Eu:
						sprintf_s(v7, "TBRD\t2, 0x%02X", (unsigned __int8)v15);
						break;
					case 0x2Fu:
						sprintf_s(v7, "TBRD\tA, 0x%02X", (unsigned __int8)v15);
						break;
					case 0x40u:
						sprintf_s(v7, "TBPTL\t#0x%02X", (unsigned __int8)v15);
						break;
					case 0x41u:
						sprintf_s(v7, "TBPTM\t#0x%02X", (unsigned __int8)v15);
						break;
					case 0x42u:
						sprintf_s(v7, "TBPTH\t#0x%02X", (unsigned __int8)v15);
						break;
					case 0x43u:
						sprintf_s(v7, "BANK\t#0x%02X", (unsigned __int8)v15);
						break;
					case 0x44u:
						sprintf_s(v7, "OR\tA, #0x%02X", (unsigned __int8)v15);
						break;
					case 0x45u:
						sprintf_s(v7, "AND\tA, #0x%02X", (unsigned __int8)v15);
						break;
					case 0x46u:
						sprintf_s(v7, "XOR\tA, #0x%02X", (unsigned __int8)v15);
						break;
					case 0x47u:
						sprintf_s(v7, "JGE\tA, #0x%02X, 0x%04X", (unsigned __int8)v15, v9);
						l = true;
						break;
					case 0x48u:
						sprintf_s(v7, "JLE\tA, #0x%02X, 0x%04X", (unsigned __int8)v15, v9);
						l = true;
						break;
					case 0x49u:
						sprintf_s(v7, "JE\tA, #0x%02X, 0x%04X", (unsigned __int8)v15, v9);
						l = true;
						break;
					case 0x4Au:
						sprintf_s(v7, "ADD\tA, #0x%02X", (unsigned __int8)v15);
						break;
					case 0x4Bu:
						sprintf_s(v7, "ADC\tA, #0x%02X", (unsigned __int8)v15);
						break;
					case 0x4Cu:
						sprintf_s(v7, "SUB\tA, #0x%02X", (unsigned __int8)v15);
						break;
					case 0x4Du:
						sprintf_s(v7, "SUBB\tA, #0x%02X", (unsigned __int8)v15);
						break;
					case 0x4Eu:
						sprintf_s(v7, "MOV\tA, #0x%02X", (unsigned __int8)v15);
						break;
					case 0x4Fu:
						sprintf_s(v7, "MUL\tA, #0x%02X", (unsigned __int8)v15);
						break;
					case 0x50u:
						sprintf_s(v7, "JDNZ\tA, 0x%02X, 0x%04X", (unsigned __int8)v15, v9);
						l = true;
						break;
					case 0x51u:
						sprintf_s(v7, "JDNZ\t0x%02X, 0x%04X", (unsigned __int8)v15, v9);
						l = true;
						break;
					case 0x52u:
						sprintf_s(v7, "JINZ\tA, 0x%02X, 0x%04X", (unsigned __int8)v15, v9);
						l = true;
						break;
					case 0x53u:
						sprintf_s(v7, "JINZ\t0x%02X, 0x%04X", (unsigned __int8)v15, v9);
						l = true;
						break;
					case 0x55u:
						sprintf_s(v7, "JGE\tA, 0x%02X, 0x%04X", (unsigned __int8)v15, v9);
						l = true;
						break;
					case 0x56u:
						sprintf_s(v7, "JLE\tA, 0x%02X, 0x%04X", (unsigned __int8)v15, v9);
						l = true;
						break;
					case 0x57u:
						sprintf_s(v7, "JE\tA, 0x%02X, 0x%04X", (unsigned __int8)v15, v9);
						l = true;
						break;
					default:
						v13 = v16 & 7;
						switch (v15 >> 11) {
						case 0xBu:
							sprintf_s(v7, "JBC\t0x%02X, %01X, 0x%04X", (unsigned __int8)v15, v13, v9);
							l = true;
							break;
						case 0xCu:
							sprintf_s(v7, "JBS\t0x%02X, %01X, 0x%04X", (unsigned __int8)v15, v13, v9);
							l = true;
							break;
						case 0xDu:
							sprintf_s(v7, "BC\t0x%02X, %01X", (unsigned __int8)v15, v13);
							break;
						case 0xEu:
							sprintf_s(v7, "BS\t0x%02X, %01X", (unsigned __int8)v15, v13);
							break;
						case 0xFu:
							sprintf_s(v7, "BTG\t0x%02X, %01X", (unsigned __int8)v15, v13);
							break;
						default:
							if ((v15 & 0xFFFFF000) == 12288) {
								sprintf_s(v7, "S0CALL\t0x%04X", v15 & 0xFFF);
							}
							else {
								v11 = v15 & 0x1FFF;
								switch (v15 >> 13) {
								case 4u:
									sprintf_s(v7, "MOVRP\t0x%02X, 0x%02X", v16, (unsigned __int8)v15);
									break;
								case 5u:
									sprintf_s(v7, "MOVPR\t0x%02X, 0x%02X", (unsigned __int8)v15, v16);
									break;
								case 6u:
									sprintf_s(v7, "SJMP\t0x%04X", v11);
									break;
								case 7u:
									sprintf_s(v7, "SCALL\t0x%04X", v11);
									break;
								default:
									sprintf_s(v7, "Unknown instruction");
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
			sprintf_s(v7, "RETI");
		}
	}
	else if (v15 == 2) {
		sprintf_s(v7, "SLEP");
	}
	else if (v15) {
		if (v15 != 1)
			goto LABEL_19;
		sprintf_s(v7, "WDTC");
	}
	else {
		sprintf_s(v7, "NOP");
	}
LABEL_105:
	// Íê³É
	return strdup(v7);
}