#include <cstdint>
#include <cstdio>
#include <iostream>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif
#ifndef HIBYTE
#define HIBYTE(x) ((uint8_t)(((x) >> 8) & 0xFF))
#endif
#define READ_WORD_BE(ptr) \
	((ptr)[0] << 12 |     \
		(ptr)[1] << 8 |   \
		(ptr)[2] << 4 |   \
		(ptr)[3])

char* decodeeps(char* rom, int pc, bool& l) {
	// unsigned int* v6; // edi  // KhÃ´ng dÃ¹ng, cÃ³ thá»?bá»?
	// unsigned __int16 v9;      // a
	// int v11;                  // eax
	// int v13;                  // [esp+Ch] [ebp-20h]
	// int v14;                  // [esp+10h] [ebp-1Ch]
	// unsigned __int16 v15;     // [esp+14h] [ebp-18h]
	// int v16;                  // [esp+14h] [ebp-18h]
	// char v7[200];

	uint16_t v15 = READ_WORD_BE(&rom[(pc) << 2]);
	uint16_t v9  = READ_WORD_BE(&rom[(pc + 1) << 2]);
	int v11 = 0, v13 = 0, v14 = 0, v16 = 0;
	char v7[200];
	l = false;
	if (v15 > 2u) {
		if (v15 == 11262) {
			snprintf(v7, sizeof(v7), "RET");
		}
		else {
			if (v15 != 11263) {
			LABEL_19:
				if (v15 >> 4 == 2) {
					snprintf(v7, sizeof(v7), "LJMP\t0x%05X", v9 | ((v15 & 0xF) << 16));
					l = true;
				}
				else if (v15 >> 4 == 3) {
					snprintf(v7, sizeof(v7), "LCALL\t0x%05X", v9 | ((v15 & 0xF) << 16));
					l = true;
				}
				else {
					v16 = HIBYTE(v15);
					switch (v15 >> 8) {
					case 2u:
						snprintf(v7, sizeof(v7), "OR\tA, 0x%02X", (uint8_t)v15);
						break;
					case 3u:
						snprintf(v7, sizeof(v7), "OR\t0x%02X, A", (uint8_t)v15);
						break;
					case 4u:
						snprintf(v7, sizeof(v7), "AND\tA, 0x%02X", (uint8_t)v15);
						break;
					case 5u:
						snprintf(v7, sizeof(v7), "AND\t0x%02X, A", (uint8_t)v15);
						break;
					case 6u:
						snprintf(v7, sizeof(v7), "XOR\tA, 0x%02X", (uint8_t)v15);
						break;
					case 7u:
						snprintf(v7, sizeof(v7), "XOR\t0x%02X, A", (uint8_t)v15);
						break;
					case 8u:
						snprintf(v7, sizeof(v7), "COMA\t0x%02X", (uint8_t)v15);
						break;
					case 9u:
						snprintf(v7, sizeof(v7), "COM\t0x%02X", (uint8_t)v15);
						break;
					case 0xAu:
						snprintf(v7, sizeof(v7), "RRCA\t0x%02X", (uint8_t)v15);
						break;
					case 0xBu:
						snprintf(v7, sizeof(v7), "RRC\t0x%02X", (uint8_t)v15);
						break;
					case 0xCu:
						snprintf(v7, sizeof(v7), "RLCA\t0x%02X", (uint8_t)v15);
						break;
					case 0xDu:
						snprintf(v7, sizeof(v7), "RLC\t0x%02X", (uint8_t)v15);
						break;
					case 0xEu:
						snprintf(v7, sizeof(v7), "SWAPA\t0x%02X", (uint8_t)v15);
						break;
					case 0xFu:
						snprintf(v7, sizeof(v7), "SWAP\t0x%02X", (uint8_t)v15);
						break;
					case 0x10u:
						snprintf(v7, sizeof(v7), "ADD\tA, 0x%02X", (uint8_t)v15);
						break;
					case 0x11u:
						snprintf(v7, sizeof(v7), "ADD\t0x%02X, A", (uint8_t)v15);
						break;
					case 0x12u:
						snprintf(v7, sizeof(v7), "ADC\tA, 0x%02X", (uint8_t)v15);
						break;
					case 0x13u:
						snprintf(v7, sizeof(v7), "ADC\t0x%02X, A", (uint8_t)v15);
						break;
					case 0x14u:
						snprintf(v7, sizeof(v7), "ADDDC\tA, 0x%02X", (uint8_t)v15);
						break;
					case 0x15u:
						snprintf(v7, sizeof(v7), "ADDDC\t0x%02X, A", (uint8_t)v15);
						break;
					case 0x16u:
						snprintf(v7, sizeof(v7), "SUB\tA, 0x%02X", (uint8_t)v15);
						break;
					case 0x17u:
						snprintf(v7, sizeof(v7), "SUB\t0x%02X, A", (uint8_t)v15);
						break;
					case 0x18u:
						snprintf(v7, sizeof(v7), "SUBB\tA, 0x%02X", (uint8_t)v15);
						break;
					case 0x19u:
						snprintf(v7, sizeof(v7), "SUBB\t0x%02X, A", (uint8_t)v15);
						break;
					case 0x1Au:
						snprintf(v7, sizeof(v7), "SUBDB\tA, 0x%02X", (uint8_t)v15);
						break;
					case 0x1Bu:
						snprintf(v7, sizeof(v7), "SUBDB\t0x%02X, A", (uint8_t)v15);
						break;
					case 0x1Cu:
						snprintf(v7, sizeof(v7), "INCA\t0x%02X", (uint8_t)v15);
						break;
					case 0x1Du:
						snprintf(v7, sizeof(v7), "INC\t0x%02X", (uint8_t)v15);
						break;
					case 0x1Eu:
						snprintf(v7, sizeof(v7), "DECA\t0x%02X", (uint8_t)v15);
						break;
					case 0x1Fu:
						snprintf(v7, sizeof(v7), "DEC\t0x%02X", (uint8_t)v15);
						break;
					case 0x20u:
						snprintf(v7, sizeof(v7), "MOV\tA, 0x%02X", (uint8_t)v15);
						break;
					case 0x21u:
						snprintf(v7, sizeof(v7), "MOV\t0x%02X, A", (uint8_t)v15);
						break;
					case 0x22u:
						snprintf(v7, sizeof(v7), "SHRA\t0x%02X", (uint8_t)v15);
						break;
					case 0x23u:
						snprintf(v7, sizeof(v7), "SHLA\t0x%02X", (uint8_t)v15);
						break;
					case 0x24u:
						snprintf(v7, sizeof(v7), "CLR\t0x%02X", (uint8_t)v15);
						break;
					case 0x25u:
						snprintf(v7, sizeof(v7), "TEST\t0x%02X", (uint8_t)v15);
						break;
					case 0x26u:
						snprintf(v7, sizeof(v7), "MUL\tA, 0x%02X", (uint8_t)v15);
						break;
					case 0x27u:
						snprintf(v7, sizeof(v7), "RPT\t0x%02X", (uint8_t)v15);
						break;
					case 0x2Cu:
						snprintf(v7, sizeof(v7), "TBRD\t0, 0x%02X", (uint8_t)v15);
						break;
					case 0x2Du:
						snprintf(v7, sizeof(v7), "TBRD\t1, 0x%02X", (uint8_t)v15);
						break;
					case 0x2Eu:
						snprintf(v7, sizeof(v7), "TBRD\t2, 0x%02X", (uint8_t)v15);
						break;
					case 0x2Fu:
						snprintf(v7, sizeof(v7), "TBRD\tA, 0x%02X", (uint8_t)v15);
						break;
					case 0x40u:
						snprintf(v7, sizeof(v7), "TBPTL\t#0x%02X", (uint8_t)v15);
						break;
					case 0x41u:
						snprintf(v7, sizeof(v7), "TBPTM\t#0x%02X", (uint8_t)v15);
						break;
					case 0x42u:
						snprintf(v7, sizeof(v7), "TBPTH\t#0x%02X", (uint8_t)v15);
						break;
					case 0x43u:
						snprintf(v7, sizeof(v7), "BANK\t#0x%02X", (uint8_t)v15);
						break;
					case 0x44u:
						snprintf(v7, sizeof(v7), "OR\tA, #0x%02X", (uint8_t)v15);
						break;
					case 0x45u:
						snprintf(v7, sizeof(v7), "AND\tA, #0x%02X", (uint8_t)v15);
						break;
					case 0x46u:
						snprintf(v7, sizeof(v7), "XOR\tA, #0x%02X", (uint8_t)v15);
						break;
					case 0x47u:
						snprintf(v7, sizeof(v7), "JGE\tA, #0x%02X, 0x%04X", (uint8_t)v15, v9);
						l = true;
						break;
					case 0x48u:
						snprintf(v7, sizeof(v7), "JLE\tA, #0x%02X, 0x%04X", (uint8_t)v15, v9);
						l = true;
						break;
					case 0x49u:
						snprintf(v7, sizeof(v7), "JE\tA, #0x%02X, 0x%04X", (uint8_t)v15, v9);
						l = true;
						break;
					case 0x4Au:
						snprintf(v7, sizeof(v7), "ADD\tA, #0x%02X", (uint8_t)v15);
						break;
					case 0x4Bu:
						snprintf(v7, sizeof(v7), "ADC\tA, #0x%02X", (uint8_t)v15);
						break;
					case 0x4Cu:
						snprintf(v7, sizeof(v7), "SUB\tA, #0x%02X", (uint8_t)v15);
						break;
					case 0x4Du:
						snprintf(v7, sizeof(v7), "SUBB\tA, #0x%02X", (uint8_t)v15);
						break;
					case 0x4Eu:
						snprintf(v7, sizeof(v7), "MOV\tA, #0x%02X", (uint8_t)v15);
						break;
					case 0x4Fu:
						snprintf(v7, sizeof(v7), "MUL\tA, #0x%02X", (uint8_t)v15);
						break;
					case 0x50u:
						snprintf(v7, sizeof(v7), "JDNZ\tA, 0x%02X, 0x%04X", (uint8_t)v15, v9);
						l = true;
						break;
					case 0x51u:
						snprintf(v7, sizeof(v7), "JDNZ\t0x%02X, 0x%04X", (uint8_t)v15, v9);
						l = true;
						break;
					case 0x52u:
						snprintf(v7, sizeof(v7), "JINZ\tA, 0x%02X, 0x%04X", (uint8_t)v15, v9);
						l = true;
						break;
					case 0x53u:
						snprintf(v7, sizeof(v7), "JINZ\t0x%02X, 0x%04X", (uint8_t)v15, v9);
						l = true;
						break;
					case 0x55u:
						snprintf(v7, sizeof(v7), "JGE\tA, 0x%02X, 0x%04X", (uint8_t)v15, v9);
						l = true;
						break;
					case 0x56u:
						snprintf(v7, sizeof(v7), "JLE\tA, 0x%02X, 0x%04X", (uint8_t)v15, v9);
						l = true;
						break;
					case 0x57u:
						snprintf(v7, sizeof(v7), "JE\tA, 0x%02X, 0x%04X", (uint8_t)v15, v9);
						l = true;
						break;
					default:
						v13 = v16 & 7;
						switch (v15 >> 11) {
						case 0xBu:
							snprintf(v7, sizeof(v7), "JBC\t0x%02X, %01X, 0x%04X", (uint8_t)v15, v13, v9);
							l = true;
							break;
						case 0xCu:
							snprintf(v7, sizeof(v7), "JBS\t0x%02X, %01X, 0x%04X", (uint8_t)v15, v13, v9);
							l = true;
							break;
						case 0xDu:
							snprintf(v7, sizeof(v7), "BC\t0x%02X, %01X", (uint8_t)v15, v13);
							break;
						case 0xEu:
							snprintf(v7, sizeof(v7), "BS\t0x%02X, %01X", (uint8_t)v15, v13);
							break;
						case 0xFu:
							snprintf(v7, sizeof(v7), "BTG\t0x%02X, %01X", (uint8_t)v15, v13);
							break;
						default:
							if ((v15 & 0xFFFFF000) == 12288) {
								snprintf(v7, sizeof(v7), "S0CALL\t0x%04X", v15 & 0xFFF);
							}
							else {
								v11 = v15 & 0x1FFF;
								switch (v15 >> 13) {
								case 4u:
									snprintf(v7, sizeof(v7), "MOVRP\t0x%02X, 0x%02X", v16, (uint8_t)v15);
									break;
								case 5u:
									snprintf(v7, sizeof(v7), "MOVPR\t0x%02X, 0x%02X", (uint8_t)v15, v16);
									break;
								case 6u:
									snprintf(v7, sizeof(v7), "SJMP\t0x%04X", v11);
									break;
								case 7u:
									snprintf(v7, sizeof(v7), "SCALL\t0x%04X", v11);
									break;
								default:
									snprintf(v7, sizeof(v7), "Unknown instruction");
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
			snprintf(v7, sizeof(v7), "RETI");
		}
	}
	else if (v15 == 2) {
		snprintf(v7, sizeof(v7), "SLEP");
	}
	else if (v15) {
		if (v15 != 1)
			goto LABEL_19;
		snprintf(v7, sizeof(v7), "WDTC");
	}
	else {
		snprintf(v7, sizeof(v7), "NOP");
	}
LABEL_105:
#ifdef _WIN32
	return _strdup(v7);
#else
	return strdup(v7);
#endif
}