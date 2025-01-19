#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include <map>
/* Have zero padding at first if necessary.
   The number with maximum length is (1 << (binlen - 1)), equal to the smallest
   number. It is a power of 2, thus first digit cannot be hexadecimal.
   The number has binlen digits. So, hexlen = ceil(binlen/4).
*/
inline std::string signedtohex(int n, int binlen) {
	binlen--;
	bool ispositive = (n >> binlen) == 0;
	if (!ispositive)
		n = (2 << binlen) - n;
	std::string retval = "";
	binlen = 1 + binlen / 4;
	for (int x = 0; x < binlen; x++) {
		retval = "0123456789ABCDEF"[n & 0xF] + retval;
		n >>= 4;
	}
	return ispositive ? retval : ("-" + retval);
}
inline std::string tohex(int n, int len) {
	std::string retval = "";
	for (int x = 0; x < len; x++) {
		retval = "0123456789ABCDEF"[n & 0xF] + retval;
		n >>= 4;
	}
	return retval;
}
inline std::string tobin(int n, int len) {
	std::string retval = "";
	for (int x = 0; x < len; x++) {
		retval = "01"[n & 1] + retval;
		n >>= 1;
	}
	return retval;
}
std::map<int,bool> p_labels;
inline void LABEL_FUNCTION(auto x) {
	p_labels[x] = true;
}
inline void LABEL_LABEL(auto x) {
	p_labels[x];
}

void decode(std::ostream& out, uint8_t*& buf, uint32_t pc) {
	static const char* cond[16] = {"GE ", "LT ", "GT ", "LE ", "GES", "LTS", "GTS", "LES",
		"NE ", "EQ ", "NV ", "OV ", "PS ", "NS ", "   ", "NOP"};

	// Handles vector table
	if (pc <= 0xFE) {
		if (pc == 0) {
			out << "spinit  ";
		}
		else if (pc == 2) {
			out << "start   $";
			LABEL_FUNCTION(*(uint16_t*)buf);
		}
		else if (pc == 4) {
			out << "brk     $";
			LABEL_FUNCTION(*(uint16_t*)buf);
		}
		else if (pc == 6) {
			out << "nmice   ";
		}
		else if (pc == 8) {
			out << "nmi     $";
			LABEL_FUNCTION(*(uint16_t*)buf);
		}
		else {
			if (pc <= 0x7E) {
				auto id = (pc - 0xa) >> 1;
				out << "mi  " << std::setw(2) << (id) << "  $";
			}
			else {
				out << "swi " << std::setw(2) << ((pc - 0x80) >> 1) << "  $";
			}
		}
		out << tohex((buf[0] | (buf[1] << 8)) >> 1 << 1, 4) << "\n";
		buf += 2;
		return;
	}

	std::string dsr_prefix = "";
	if ((buf[0] & 0b00000000) == 0b00000000 && (buf[1] & 0b11111111) == 0b11100011) {
		int i = buf[0] >> 0 & 0b11111111;
		dsr_prefix = tohex(i, 1) + ":";
		buf += 2;
	}
	if ((buf[0] & 0b00001111) == 0b00001111 && (buf[1] & 0b11111111) == 0b10010000) {
		int d = buf[0] >> 4 & 0b1111;
		dsr_prefix = "R" + std::to_string(d) + ":";
		buf += 2;
	}
	if ((buf[0] & 0b11111111) == 0b10011111 && (buf[1] & 0b11111111) == 0b11111110) {
		dsr_prefix = "DSR:";
		buf += 2;
	}

	if ((buf[0] & 0b00001111) == 0b00000001 && (buf[1] & 0b11110000) == 0b10000000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 0 & 0b1111;
		buf += 2;
		out << "ADD     R" << (n) << ", R" << (m);
		return;
	}
	if ((buf[0] & 0b00000000) == 0b00000000 && (buf[1] & 0b11110000) == 0b00010000) {
		int i = buf[0] >> 0 & 0b11111111, n = buf[1] >> 0 & 0b1111;
		buf += 2;
		out << "ADD     R" << (n) << ", " << tohex(i, 2);
		return;
	}
	if ((buf[0] & 0b00011111) == 0b00000110 && (buf[1] & 0b11110001) == 0b11110000) {
		int m = buf[0] >> 5 & 0b111, n = buf[1] >> 1 & 0b111;
		out << "ADD     ER" << (n * 2) << ", ER" << (m * 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b10000000) == 0b10000000 && (buf[1] & 0b11110001) == 0b11100000) {
		int i = buf[0] >> 0 & 0b1111111, n = buf[1] >> 1 & 0b111;
		out << "ADD     ER" << (n * 2) << ", " << (i << 25 >> 25);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00000110 && (buf[1] & 0b11110000) == 0b10000000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 0 & 0b1111;
		out << "ADDC    R" << (n) << ", R" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00000000) == 0b00000000 && (buf[1] & 0b11110000) == 0b01100000) {
		int i = buf[0] >> 0 & 0b11111111, n = buf[1] >> 0 & 0b1111;
		out << "ADDC    R" << (n) << ", " << (i);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00000010 && (buf[1] & 0b11110000) == 0b10000000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 0 & 0b1111;
		out << "AND     R" << (n) << ", R" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00000000) == 0b00000000 && (buf[1] & 0b11110000) == 0b00100000) {
		int i = buf[0] >> 0 & 0b11111111, n = buf[1] >> 0 & 0b1111;
		out << "AND     R" << (n) << ", " << tohex(i, 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00000111 && (buf[1] & 0b11110000) == 0b10000000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 0 & 0b1111;
		buf += 2;
		out << "CMP     R" << (n) << ", R" << (m);
		return;
	}
	if ((buf[0] & 0b00000000) == 0b00000000 && (buf[1] & 0b11110000) == 0b01110000) {
		int i = buf[0] >> 0 & 0b11111111, n = buf[1] >> 0 & 0b1111;
		buf += 2;
		out << "CMP     R" << (n) << ", " << tohex(i, 2);
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00000101 && (buf[1] & 0b11110000) == 0b10000000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 0 & 0b1111;
		out << "CMPC    R" << (n) << ", R" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00000000) == 0b00000000 && (buf[1] & 0b11110000) == 0b01010000) {
		int i = buf[0] >> 0 & 0b11111111, n = buf[1] >> 0 & 0b1111;
		out << "CMPC    R" << (n) << ", "<< tohex(i, 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00011111) == 0b00000101 && (buf[1] & 0b11110001) == 0b11110000) {
		int m = buf[0] >> 5 & 0b111, n = buf[1] >> 1 & 0b111;
		out << "MOV     ER" << (n * 2) << ", ER" << (m * 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b10000000) == 0b00000000 && (buf[1] & 0b11110001) == 0b11100000) {
		int i = buf[0] >> 0 & 0b1111111, n = buf[1] >> 1 & 0b111;
		out << "MOV     ER" << (n * 2) << ", "<< tohex(i, 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00000000 && (buf[1] & 0b11110000) == 0b10000000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 0 & 0b1111;
		out << "MOV     R" << (n) << ", R" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00000000) == 0b00000000 && (buf[1] & 0b11110000) == 0b00000000) {
		int i = buf[0] >> 0 & 0b11111111, n = buf[1] >> 0 & 0b1111;
		out << "MOV     R" << (n) << ", " << tohex(i, 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00000011 && (buf[1] & 0b11110000) == 0b10000000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 0 & 0b1111;
		out << "OR      R" << (n) << ", R" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00000000) == 0b00000000 && (buf[1] & 0b11110000) == 0b00110000) {
		int i = buf[0] >> 0 & 0b11111111, n = buf[1] >> 0 & 0b1111;
		out << "OR      R" << (n) << ", " << tohex(i, 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00000100 && (buf[1] & 0b11110000) == 0b10000000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 0 & 0b1111;
		out << "XOR     R" << (n) << ", R" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00000000) == 0b00000000 && (buf[1] & 0b11110000) == 0b01000000) {
		int i = buf[0] >> 0 & 0b11111111, n = buf[1] >> 0 & 0b1111;
		out << "XOR     R" << (n) << ", " << tohex(i, 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00011111) == 0b00000111 && (buf[1] & 0b11110001) == 0b11110000) {
		int m = buf[0] >> 5 & 0b111, n = buf[1] >> 1 & 0b111;
		out << "CMP     ER" << (n * 2) << ", ER" << (m * 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00001000 && (buf[1] & 0b11110000) == 0b10000000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 0 & 0b1111;
		buf += 2;
		out << "SUB     R" << (n) << ", R" << (m);
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00001001 && (buf[1] & 0b11110000) == 0b10000000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 0 & 0b1111;
		out << "SUBC    R" << (n) << ", R" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00001010 && (buf[1] & 0b11110000) == 0b10000000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 0 & 0b1111;
		buf += 2;
		out << "SLL     R" << (n) << ", R" << (m);
		return;
	}
	if ((buf[0] & 0b10001111) == 0b00001010 && (buf[1] & 0b11110000) == 0b10010000) {
		int n = buf[1] >> 0 & 0b1111, w = buf[0] >> 4 & 0b111;
		out << "SLL     R" << (n) << ", " << (w);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00001011 && (buf[1] & 0b11110000) == 0b10000000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 0 & 0b1111;
		out << "SLLC    R" << (n) << ", R" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b10001111) == 0b00001011 && (buf[1] & 0b11110000) == 0b10010000) {
		int n = buf[1] >> 0 & 0b1111, w = buf[0] >> 4 & 0b111;
		out << "SLLC    R" << (n) << ", " << (w);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00001110 && (buf[1] & 0b11110000) == 0b10000000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 0 & 0b1111;
		out << "SRA     R" << (n) << ", R" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b10001111) == 0b00001110 && (buf[1] & 0b11110000) == 0b10010000) {
		int n = buf[1] >> 0 & 0b1111, w = buf[0] >> 4 & 0b111;
		out << "SRA     R" << (n) << ", " << (w);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00001100 && (buf[1] & 0b11110000) == 0b10000000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 0 & 0b1111;
		out << "SRL     R" << (n) << ", R" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b10001111) == 0b00001100 && (buf[1] & 0b11110000) == 0b10010000) {
		int n = buf[1] >> 0 & 0b1111, w = buf[0] >> 4 & 0b111;
		out << "SRL     R" << (n) << ", " << (w);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00001101 && (buf[1] & 0b11110000) == 0b10000000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 0 & 0b1111;
		out << "SRLC    R" << (n) << ", R" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b10001111) == 0b00001101 && (buf[1] & 0b11110000) == 0b10010000) {
		int n = buf[1] >> 0 & 0b1111, w = buf[0] >> 4 & 0b111;
		out << "SRLC    R" << (n) << ", " << (w);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00110010 && (buf[1] & 0b11110001) == 0b10010000) {
		int n = buf[1] >> 1 & 0b111;
		out << "L       ER" << (n * 2) << ", " << dsr_prefix << "[EA]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b01010010 && (buf[1] & 0b11110001) == 0b10010000) {
		int n = buf[1] >> 1 & 0b111;
		out << "L       ER" << (n * 2) << ", " << dsr_prefix << "[EA+]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00011111) == 0b00000010 && (buf[1] & 0b11110001) == 0b10010000) {
		int m = buf[0] >> 5 & 0b111, n = buf[1] >> 1 & 0b111;
		out << "L       ER" << (n * 2) << ", " << dsr_prefix << "[ER" << (m * 2) << "]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11000000) == 0b00000000 && (buf[1] & 0b11110001) == 0b10110000) {
		int D = buf[0] >> 0 & 0b111111, n = buf[1] >> 1 & 0b111;
		out << "L       ER" << (n * 2) << ", " << dsr_prefix << "ER12[" << (signedtohex(D, 6)) << "]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11000000) == 0b01000000 && (buf[1] & 0b11110001) == 0b10110000) {
		int D = buf[0] >> 0 & 0b111111, n = buf[1] >> 1 & 0b111;
		out << "L       ER" << (n * 2) << ", " << dsr_prefix << "ER14[" << (signedtohex(D, 6)) << "]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00110000 && (buf[1] & 0b11110000) == 0b10010000) {
		int n = buf[1] >> 0 & 0b1111;
		out << "L       R" << (n) << ", " << dsr_prefix << "[EA]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b01010000 && (buf[1] & 0b11110000) == 0b10010000) {
		int n = buf[1] >> 0 & 0b1111;
		out << "L       R" << (n) << ", " << dsr_prefix << "[EA+]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00011111) == 0b00000000 && (buf[1] & 0b11110000) == 0b10010000) {
		int m = buf[0] >> 5 & 0b111, n = buf[1] >> 0 & 0b1111;
		out << "L       R" << (n) << ", " << dsr_prefix << "[ER" << (m * 2) << "]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11000000) == 0b00000000 && (buf[1] & 0b11110000) == 0b11010000) {
		int D = buf[0] >> 0 & 0b111111, n = buf[1] >> 0 & 0b1111;
		out << "L       R" << (n) << ", " << dsr_prefix << "ER12[" << (signedtohex(D, 6)) << "]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11000000) == 0b01000000 && (buf[1] & 0b11110000) == 0b11010000) {
		int D = buf[0] >> 0 & 0b111111, n = buf[1] >> 0 & 0b1111;
		out << "L       R" << (n) << ", " << dsr_prefix << "ER14[" << (signedtohex(D, 6)) << "]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00110100 && (buf[1] & 0b11110011) == 0b10010000) {
		int n = buf[1] >> 2 & 0b11;
		out << "L       XR" << (n * 4) << ", " << dsr_prefix << "[EA]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b01010100 && (buf[1] & 0b11110011) == 0b10010000) {
		int n = buf[1] >> 2 & 0b11;
		out << "L       XR" << (n * 4) << ", " << dsr_prefix << "[EA+]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00110110 && (buf[1] & 0b11110111) == 0b10010000) {
		int n = buf[1] >> 3 & 0b1;
		out << "L       QR" << (n * 8) << ", " << dsr_prefix << "[EA]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b01010110 && (buf[1] & 0b11110111) == 0b10010000) {
		int n = buf[1] >> 3 & 0b1;
		out << "L       QR" << (n * 8) << ", " << dsr_prefix << "[EA+]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00110011 && (buf[1] & 0b11110001) == 0b10010000) {
		int n = buf[1] >> 1 & 0b111;
		out << "ST      ER" << (n * 2) << ", " << dsr_prefix << "[EA]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b01010011 && (buf[1] & 0b11110001) == 0b10010000) {
		int n = buf[1] >> 1 & 0b111;
		out << "ST      ER" << (n * 2) << ", " << dsr_prefix << "[EA+]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00011111) == 0b00000011 && (buf[1] & 0b11110001) == 0b10010000) {
		int m = buf[0] >> 5 & 0b111, n = buf[1] >> 1 & 0b111;
		out << "ST      ER" << (n * 2) << ", " << dsr_prefix << "[ER" << (m * 2) << "]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11000000) == 0b10000000 && (buf[1] & 0b11110001) == 0b10110000) {
		int D = buf[0] >> 0 & 0b111111, n = buf[1] >> 1 & 0b111;
		out << "ST      ER" << (n * 2) << ", " << dsr_prefix << "ER12[" << (signedtohex(D, 6)) << "]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11000000) == 0b11000000 && (buf[1] & 0b11110001) == 0b10110000) {
		int D = buf[0] >> 0 & 0b111111, n = buf[1] >> 1 & 0b111;
		out << "ST      ER" << (n * 2) << ", " << dsr_prefix << "ER14[" << (signedtohex(D, 6)) << "]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00110001 && (buf[1] & 0b11110000) == 0b10010000) {
		int n = buf[1] >> 0 & 0b1111;
		out << "ST      R" << (n) << ", " << dsr_prefix << "[EA]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b01010001 && (buf[1] & 0b11110000) == 0b10010000) {
		int n = buf[1] >> 0 & 0b1111;
		out << "ST      R" << (n) << ", " << dsr_prefix << "[EA+]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00011111) == 0b00000001 && (buf[1] & 0b11110000) == 0b10010000) {
		int m = buf[0] >> 5 & 0b111, n = buf[1] >> 0 & 0b1111;
		out << "ST      R" << (n) << ", " << dsr_prefix << "[ER" << (m * 2) << "]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11000000) == 0b10000000 && (buf[1] & 0b11110000) == 0b11010000) {
		int D = buf[0] >> 0 & 0b111111, n = buf[1] >> 0 & 0b1111;
		out << "ST      R" << (n) << ", " << dsr_prefix << "ER12[" << (signedtohex(D, 6)) << "]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11000000) == 0b11000000 && (buf[1] & 0b11110000) == 0b11010000) {
		int D = buf[0] >> 0 & 0b111111, n = buf[1] >> 0 & 0b1111;
		out << "ST      R" << (n) << ", " << dsr_prefix << "ER14[" << (signedtohex(D, 6)) << "]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00110101 && (buf[1] & 0b11110011) == 0b10010000) {
		int n = buf[1] >> 2 & 0b11;
		out << "ST      XR" << (n * 4) << ", " << dsr_prefix << "[EA]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b01010101 && (buf[1] & 0b11110011) == 0b10010000) {
		int n = buf[1] >> 2 & 0b11;
		out << "ST      XR" << (n * 4) << ", " << dsr_prefix << "[EA+]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00110111 && (buf[1] & 0b11110111) == 0b10010000) {
		int n = buf[1] >> 3 & 0b1;
		out << "ST      QR" << (n * 8) << ", " << dsr_prefix << "[EA]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b01010111 && (buf[1] & 0b11110111) == 0b10010000) {
		int n = buf[1] >> 3 & 0b1;
		out << "ST      QR" << (n * 8) << ", " << dsr_prefix << "[EA+]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00000000) == 0b00000000 && (buf[1] & 0b11111111) == 0b11100001) {
		int i = buf[0] >> 0 & 0b11111111;
		out << "ADD     SP, " << (signedtohex(i, 8));
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00001111 && (buf[1] & 0b11111111) == 0b10100000) {
		int m = buf[0] >> 4 & 0b1111;
		out << "MOV     ECSR, R" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00001101 && (buf[1] & 0b11110001) == 0b10100000) {
		int m = buf[1] >> 1 & 0b111;
		out << "MOV     ELR, ER" << (m * 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00001100 && (buf[1] & 0b11111111) == 0b10100000) {
		int m = buf[0] >> 4 & 0b1111;
		out << "MOV     EPSW, R" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00000101 && (buf[1] & 0b11110001) == 0b10100000) {
		int n = buf[1] >> 1 & 0b111;
		out << "MOV     ER" << (n * 2) << ", ELR";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00011010 && (buf[1] & 0b11110001) == 0b10100000) {
		int n = buf[1] >> 1 & 0b111;
		out << "MOV     ER" << (n * 2) << ", SP";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00001011 && (buf[1] & 0b11111111) == 0b10100000) {
		int m = buf[0] >> 4 & 0b1111;
		out << "MOV     PSW, R" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00000000) == 0b00000000 && (buf[1] & 0b11111111) == 0b11101001) {
		int i = buf[0] >> 0 & 0b11111111;
		out << "MOV     PSW, " << (i);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00000111 && (buf[1] & 0b11110000) == 0b10100000) {
		int n = buf[1] >> 0 & 0b1111;
		out << "MOV     R" << (n) << ", ECSR";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00000100 && (buf[1] & 0b11110000) == 0b10100000) {
		int n = buf[1] >> 0 & 0b1111;
		out << "MOV     R" << (n) << ", EPSW";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00000011 && (buf[1] & 0b11110000) == 0b10100000) {
		int n = buf[1] >> 0 & 0b1111;
		out << "MOV     R" << (n) << ", PSW";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00011111) == 0b00001010 && (buf[1] & 0b11111111) == 0b10100001) {
		int m = buf[0] >> 5 & 0b111;
		out << "MOV     SP, ER" << (m * 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b01011110 && (buf[1] & 0b11110001) == 0b11110000) {
		int n = buf[1] >> 1 & 0b111;
		out << "PUSH    ER" << (n * 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b01111110 && (buf[1] & 0b11110111) == 0b11110000) {
		int n = buf[1] >> 3 & 0b1;
		out << "PUSH    QR" << (n * 8);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b01001110 && (buf[1] & 0b11110000) == 0b11110000) {
		int n = buf[1] >> 0 & 0b1111;
		out << "PUSH    R" << (n);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b01101110 && (buf[1] & 0b11110011) == 0b11110000) {
		int n = buf[1] >> 2 & 0b11;
		out << "PUSH    XR" << (n * 4);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b11001110 && (buf[1] & 0b11110000) == 0b11110000) {
		int l = buf[1] >> 3 & 0b1, e = buf[1] >> 2 & 0b1, p = buf[1] >> 1 & 0b1, a = buf[1] & 0b1;
		out << "PUSH    "
			<< (p ? "ELR " : "")
			<< (e ? "EPSW " : "")
			<< (l ? "LR " : "")
			<< (a ? "EA " : "");
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00011110 && (buf[1] & 0b11110001) == 0b11110000) {
		int n = buf[1] >> 1 & 0b111;
		out << "POP     ER" << (n * 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00111110 && (buf[1] & 0b11110111) == 0b11110000) {
		int n = buf[1] >> 3 & 0b1;
		out << "POP     QR" << (n * 8);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00001110 && (buf[1] & 0b11110000) == 0b11110000) {
		int n = buf[1] >> 0 & 0b1111;
		out << "POP     R" << (n);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00101110 && (buf[1] & 0b11110011) == 0b11110000) {
		int n = buf[1] >> 2 & 0b11;
		out << "POP     XR" << (n * 4);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b10001110 && (buf[1] & 0b11110000) == 0b11110000) {
		int l = buf[1] >> 3 & 0b1, e = buf[1] >> 2 & 0b1, p = buf[1] >> 1 & 0b1, a = buf[1] & 0b1;
		out << "POP     "
			<< (a ? "EA " : "")
			<< (l ? "LR " : "")
			<< (e ? "PSW " : "")
			<< (p ? "PC " : "");
		if (p) {
			// in this case, we dont hope the controlflow gets extended, lets just define a funciton at next instruction
			LABEL_FUNCTION(pc + 2);
		}
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00001110 && (buf[1] & 0b11110000) == 0b10100000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 0 & 0b1111;
		out << "MOV     CR" << (n) << ", R" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00101101 && (buf[1] & 0b11110001) == 0b11110000) {
		int n = buf[1] >> 1 & 0b111;
		out << "MOV     CER" << (n * 2) << ", " << dsr_prefix << "[EA]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00111101 && (buf[1] & 0b11110001) == 0b11110000) {
		int n = buf[1] >> 1 & 0b111;
		out << "MOV     CER" << (n * 2) << ", " << dsr_prefix << "[EA+]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00001101 && (buf[1] & 0b11110000) == 0b11110000) {
		int n = buf[1] >> 0 & 0b1111;
		out << "MOV     CR" << (n) << ", " << dsr_prefix << "[EA]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00011101 && (buf[1] & 0b11110000) == 0b11110000) {
		int n = buf[1] >> 0 & 0b1111;
		out << "MOV     CR" << (n) << ", " << dsr_prefix << "[EA+]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b01001101 && (buf[1] & 0b11110011) == 0b11110000) {
		int n = buf[1] >> 2 & 0b11;
		out << "MOV     CXR" << (n * 4) << ", " << dsr_prefix << "[EA]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b01011101 && (buf[1] & 0b11110011) == 0b11110000) {
		int n = buf[1] >> 2 & 0b11;
		out << "MOV     CXR" << (n * 4) << ", " << dsr_prefix << "[EA+]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b01101101 && (buf[1] & 0b11110111) == 0b11110000) {
		int n = buf[1] >> 3 & 0b1;
		out << "MOV     CQR" << (n * 8) << ", " << dsr_prefix << "[EA]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b01111101 && (buf[1] & 0b11110111) == 0b11110000) {
		int n = buf[1] >> 3 & 0b1;
		out << "MOV     CQR" << (n * 8) << ", " << dsr_prefix << "[EA+]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00000110 && (buf[1] & 0b11110000) == 0b10100000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 0 & 0b1111;
		out << "MOV     R" << (n) << ", CR" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b10101101 && (buf[1] & 0b11110001) == 0b11110000) {
		int m = buf[1] >> 1 & 0b111;
		out << "MOV     " << dsr_prefix << "[EA], CER" << (m * 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b10111101 && (buf[1] & 0b11110001) == 0b11110000) {
		int m = buf[1] >> 1 & 0b111;
		out << "MOV     " << dsr_prefix << "[EA+], CER" << (m * 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b10001101 && (buf[1] & 0b11110000) == 0b11110000) {
		int m = buf[1] >> 0 & 0b1111;
		out << "MOV     " << dsr_prefix << "[EA], CR" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b10011101 && (buf[1] & 0b11110000) == 0b11110000) {
		int m = buf[1] >> 0 & 0b1111;
		out << "MOV     " << dsr_prefix << "[EA+], CR" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b11001101 && (buf[1] & 0b11110011) == 0b11110000) {
		int m = buf[1] >> 2 & 0b11;
		out << "MOV     " << dsr_prefix << "[EA], CXR" << (m * 4);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b11011101 && (buf[1] & 0b11110011) == 0b11110000) {
		int m = buf[1] >> 2 & 0b11;
		out << "MOV     " << dsr_prefix << "[EA+], CXR" << (m * 4);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b11101101 && (buf[1] & 0b11110111) == 0b11110000) {
		int m = buf[1] >> 3 & 0b1;
		out << "MOV     " << dsr_prefix << "[EA], CQR" << (m * 8);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b11111101 && (buf[1] & 0b11110111) == 0b11110000) {
		int m = buf[1] >> 3 & 0b1;
		out << "MOV     " << dsr_prefix << "[EA+], CQR" << (m * 8);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00011111) == 0b00001010 && (buf[1] & 0b11111111) == 0b11110000) {
		int m = buf[0] >> 5 & 0b111;
		out << "LEA     [ER" << (m * 2) << "]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00011111 && (buf[1] & 0b11110000) == 0b10000000) {
		int n = buf[1] >> 0 & 0b1111;
		out << "DAA     R" << (n);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00111111 && (buf[1] & 0b11110000) == 0b10000000) {
		int n = buf[1] >> 0 & 0b1111;
		out << "DAS     R" << (n);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b01011111 && (buf[1] & 0b11110000) == 0b10000000) {
		int n = buf[1] >> 0 & 0b1111;
		out << "NEG     R" << (n);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b10001111) == 0b00000000 && (buf[1] & 0b11110000) == 0b10100000) {
		int b = buf[0] >> 4 & 0b111, n = buf[1] >> 0 & 0b1111;
		out << "SB      R" << (n) << "." << (b);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b10001111) == 0b00000010 && (buf[1] & 0b11110000) == 0b10100000) {
		int b = buf[0] >> 4 & 0b111, n = buf[1] >> 0 & 0b1111;
		out << "RB      R" << (n) << "." << (b);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b10001111) == 0b00000001 && (buf[1] & 0b11110000) == 0b10100000) {
		int b = buf[0] >> 4 & 0b111, n = buf[1] >> 0 & 0b1111;
		out << "TB      R" << (n) << "." << (b);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00001000 && (buf[1] & 0b11111111) == 0b11101101) {
		out << "EI";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b11110111 && (buf[1] & 0b11111111) == 0b11101011) {
		out << "DI";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b10000000 && (buf[1] & 0b11111111) == 0b11101101) {
		out << "SC";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b01111111 && (buf[1] & 0b11111111) == 0b11101011) {
		out << "RC";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b11001111 && (buf[1] & 0b11111111) == 0b11111110) {
		out << "CPLC";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00000000) == 0b00000000 && (buf[1] & 0b11110000) == 0b11000000) {
		int c = buf[1] >> 0 & 0b1111, r = buf[0] >> 0 & 0b11111111;
		auto addr = (pc & 0x0f0000) | (uint16_t)(pc + 2 + ((int)(signed char)r << 1));
		if (c == 15) {
			out << "NOP";
		}
		else {
			LABEL_LABEL(addr);
			if (c == 14) { // BAL
				// in this case, we dont hope the controlflow gets extended, lets just define a funciton at next instruction
				LABEL_FUNCTION(pc + 2);
			}
			out << "B" << (cond[c]) << "    $" << (tohex(addr, 5));
		}
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00011111) == 0b00001111 && (buf[1] & 0b11110001) == 0b10000001) {
		int m = buf[1] >> 1 & 0b111, n = buf[0] >> 5 & 0b111;
		out << "EXTBW   ER" << (n * 2); // Idk if this format is legal xd
		//  << " ; ERn" << (n * 2) << " = R" << (m * 2)
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11000000) == 0b00000000 && (buf[1] & 0b11111111) == 0b11100101) {
		int i = buf[0] >> 0 & 0b111111;
		out << "SWI      " << (i);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b11111111 && (buf[1] & 0b11111111) == 0b11111111) {
		out << "BRK";
		LABEL_FUNCTION(pc + 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00011111) == 0b00000010 && (buf[1] & 0b11111111) == 0b11110000) {
		int n = buf[0] >> 5 & 0b111;
		out << "B       ER" << (n * 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00011111) == 0b00000011 && (buf[1] & 0b11111111) == 0b11110000) {
		int n = buf[0] >> 5 & 0b111;
		out << "BL      ER" << (n * 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00000100 && (buf[1] & 0b11110001) == 0b11110000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 1 & 0b111;
		out << "MUL     ER" << (n * 2) << ", R" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00001111) == 0b00001001 && (buf[1] & 0b11110001) == 0b11110000) {
		int m = buf[0] >> 4 & 0b1111, n = buf[1] >> 1 & 0b111;
		out << "DIV     ER" << (n * 2) << ", R" << (m);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00101111 && (buf[1] & 0b11111111) == 0b11111110) {
		out << "INC     [EA]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00111111 && (buf[1] & 0b11111111) == 0b11111110) {
		out << "DEC     [EA]";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00011111 && (buf[1] & 0b11111111) == 0b11111110) {
		out << "RT";
		LABEL_FUNCTION(pc + 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00001111 && (buf[1] & 0b11111111) == 0b11111110) {
		out << "RTI";
		LABEL_FUNCTION(pc + 2);
		buf += 2;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b10001111 && (buf[1] & 0b11111111) == 0b11111110) {
		out << "NOP";
		buf += 2;
		return;
	}
	if ((buf[0] & 0b00011111) == 0b00001000 && (buf[1] & 0b11110001) == 0b10100000 && (buf[2] & 0b00000000) == 0b00000000 && (buf[3] & 0b00000000) == 0b00000000) {
		int D = buf[2] >> 0 & 0b11111111, E = buf[3] >> 0 & 0b11111111, m = buf[0] >> 5 & 0b111, n = buf[1] >> 1 & 0b111;
		out << "L       ER" << (n * 2) << ", " << dsr_prefix << "ER" << (m * 2) << "[" << (signedtohex(E * 256 + D, 16)) << "]";
		buf += 4;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00010010 && (buf[1] & 0b11110001) == 0b10010000 && (buf[2] & 0b00000000) == 0b00000000 && (buf[3] & 0b00000000) == 0b00000000) {
		int D = buf[2] >> 0 & 0b11111111, E = buf[3] >> 0 & 0b11111111, n = buf[1] >> 1 & 0b111;
		out << "L       ER" << (n * 2) << ", " << dsr_prefix << (tohex(E * 256 + D, 4));
		buf += 4;
		return;
	}
	if ((buf[0] & 0b00011111) == 0b00001000 && (buf[1] & 0b11110000) == 0b10010000 && (buf[2] & 0b00000000) == 0b00000000 && (buf[3] & 0b00000000) == 0b00000000) {
		int D = buf[2] >> 0 & 0b11111111, E = buf[3] >> 0 & 0b11111111, m = buf[0] >> 5 & 0b111, n = buf[1] >> 0 & 0b1111;
		out << "L       R" << (n) << ", " << dsr_prefix << "ER" << (m * 2) << "[" << (signedtohex(E * 256 + D, 16)) << "]";
		buf += 4;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00010000 && (buf[1] & 0b11110000) == 0b10010000 && (buf[2] & 0b00000000) == 0b00000000 && (buf[3] & 0b00000000) == 0b00000000) {
		int D = buf[2] >> 0 & 0b11111111, E = buf[3] >> 0 & 0b11111111, n = buf[1] >> 0 & 0b1111;
		out << "L       R" << (n) << ", " << dsr_prefix << (tohex(E * 256 + D, 4));
		buf += 4;
		return;
	}
	if ((buf[0] & 0b00011111) == 0b00001001 && (buf[1] & 0b11110001) == 0b10100000 && (buf[2] & 0b00000000) == 0b00000000 && (buf[3] & 0b00000000) == 0b00000000) {
		int D = buf[2] >> 0 & 0b11111111, E = buf[3] >> 0 & 0b11111111, m = buf[0] >> 5 & 0b111, n = buf[1] >> 1 & 0b111;
		out << "ST      ER" << (n * 2) << ", " << dsr_prefix << "ER" << (m * 2) << "[" << (signedtohex(E * 256 + D, 16)) << "]";
		buf += 4;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00010011 && (buf[1] & 0b11110001) == 0b10010000 && (buf[2] & 0b00000000) == 0b00000000 && (buf[3] & 0b00000000) == 0b00000000) {
		int D = buf[2] >> 0 & 0b11111111, E = buf[3] >> 0 & 0b11111111, n = buf[1] >> 1 & 0b111;
		out << "ST      ER" << (n * 2) << ", " << dsr_prefix << (tohex(E * 256 + D, 4));
		buf += 4;
		return;
	}
	if ((buf[0] & 0b00011111) == 0b00001001 && (buf[1] & 0b11110000) == 0b10010000 && (buf[2] & 0b00000000) == 0b00000000 && (buf[3] & 0b00000000) == 0b00000000) {
		int D = buf[2] >> 0 & 0b11111111, E = buf[3] >> 0 & 0b11111111, m = buf[0] >> 5 & 0b111, n = buf[1] >> 0 & 0b1111;
		out << "ST      R" << (n) << ", " << dsr_prefix << "ER" << (m * 2) << "[" << (signedtohex(E * 256 + D, 16)) << "]";
		buf += 4;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00010001 && (buf[1] & 0b11110000) == 0b10010000 && (buf[2] & 0b00000000) == 0b00000000 && (buf[3] & 0b00000000) == 0b00000000) {
		int D = buf[2] >> 0 & 0b11111111, E = buf[3] >> 0 & 0b11111111, n = buf[1] >> 0 & 0b1111;
		out << "ST      R" << (n) << ", " << dsr_prefix << (tohex(E * 256 + D, 4));
		buf += 4;
		return;
	}
	if ((buf[0] & 0b00011111) == 0b00001011 && (buf[1] & 0b11111111) == 0b11110000 && (buf[2] & 0b00000000) == 0b00000000 && (buf[3] & 0b00000000) == 0b00000000) {
		int D = buf[2] >> 0 & 0b11111111, E = buf[3] >> 0 & 0b11111111, m = buf[0] >> 5 & 0b111;
		out << "LEA     ER" << (m * 2) << "[" << (signedtohex(E * 256 + D, 16)) << "]";
		buf += 4;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00001100 && (buf[1] & 0b11111111) == 0b11110000 && (buf[2] & 0b00000000) == 0b00000000 && (buf[3] & 0b00000000) == 0b00000000) {
		int D = buf[2] >> 0 & 0b11111111, E = buf[3] >> 0 & 0b11111111;
		out << "LEA     " << (tohex(E * 256 + D, 4));
		buf += 4;
		return;
	}
	if ((buf[0] & 0b10001111) == 0b10000000 && (buf[1] & 0b11111111) == 0b10100000 && (buf[2] & 0b00000000) == 0b00000000 && (buf[3] & 0b00000000) == 0b00000000) {
		int D = buf[2] >> 0 & 0b11111111, E = buf[3] >> 0 & 0b11111111, b = buf[0] >> 4 & 0b111;
		out << "SB      " << (tohex(E * 256 + D, 4)) << "." << (b);
		buf += 4;
		return;
	}
	if ((buf[0] & 0b10001111) == 0b10000010 && (buf[1] & 0b11111111) == 0b10100000 && (buf[2] & 0b00000000) == 0b00000000 && (buf[3] & 0b00000000) == 0b00000000) {
		int D = buf[2] >> 0 & 0b11111111, E = buf[3] >> 0 & 0b11111111, b = buf[0] >> 4 & 0b111;
		out << "RB      " << (tohex(E * 256 + D, 4)) << "." << (b);
		buf += 4;
		return;
	}
	if ((buf[0] & 0b10001111) == 0b10000001 && (buf[1] & 0b11111111) == 0b10100000 && (buf[2] & 0b00000000) == 0b00000000 && (buf[3] & 0b00000000) == 0b00000000) {
		int D = buf[2] >> 0 & 0b11111111, E = buf[3] >> 0 & 0b11111111, b = buf[0] >> 4 & 0b111;
		out << "TB      " << (tohex(E * 256 + D, 4)) << "." << (b);
		buf += 4;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00000000 && (buf[1] & 0b11110000) == 0b11110000 && (buf[2] & 0b00000000) == 0b00000000 && (buf[3] & 0b00000000) == 0b00000000) {
		int C = buf[2] >> 0 & 0b11111111, D = buf[3] >> 0 & 0b11111111, g = buf[1] >> 0 & 0b1111;
		auto addr = (g << 16) | (D << 8) | (C);
		LABEL_FUNCTION(addr);
		out << "B       $" << (tohex(addr, 5));
		// in this case, we dont hope the controlflow gets extended, lets just define a funciton at next instruction
		LABEL_FUNCTION(pc + 4);
		buf += 4;
		return;
	}
	if ((buf[0] & 0b11111111) == 0b00000001 && (buf[1] & 0b11110000) == 0b11110000 && (buf[2] & 0b00000000) == 0b00000000 && (buf[3] & 0b00000000) == 0b00000000) {
		int C = buf[2] >> 0 & 0b11111111, D = buf[3] >> 0 & 0b11111111, g = buf[1] >> 0 & 0b1111;
		auto addr = (g << 16) | (D << 8) | (C);
		LABEL_FUNCTION(addr);
		out << "BL      $" << (tohex(addr, 5));
		buf += 4;
		return;
	}
	out << "dw      " << tohex(buf[0] | (buf[1] << 8), 4) << "\n";
	buf += 2;
}
