#include "Romu.h"
#include "Memory.h"
#include "ModelInfo.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>


inline word le_read(auto& p) {
	// this works for le machine
	return *(word*)&p;
}
inline void calc(word& sum, byte* bt, int len) {
	for (size_t i = 0; i < len; i += 2) {
		sum -= le_read(bt[i]);
	}
}
inline void calc3(word& sum, byte* bt, int len) {
	for (size_t i = 0; i < len; i++) {
		sum += bt[i];
	}
}

inline void calc2(word& sum, byte* bt, int len) {
	for (size_t i = 0; i < len; i++) {
		sum -= bt[i];
	}
}

RomInfo rom_info(std::vector<byte>& rom, const std::vector<byte>& flash, bool checksum) {
	auto dat = rom.data();
	auto dat2 = (byte*)flash.data(); // this is hack xD
	RomInfo ri{};
	auto spinit = *(word*)dat;
	enum {
		Unk,
		ESP1,
		ESP2,
		CWX,
		CWII,
	} sum_type{};
	if (spinit == 0xf000) { // cwx or casioemu
		if (rom.size() < 0x40000) {
			return ri;
		}
		if (rom.size() == 0x40000) { // must be cwx
		cwx_p:
			memcpy(ri.ver, &dat[0x3ffee], 8);
			memcpy(ri.cid, &dat[0x3fff8], 8);
			ri.desired_sum = le_read(dat[0x3fff6]);
			sum_type = CWX;
		}
		else {
			memcpy(ri.ver, &dat[0x3ffee], 8);
			if (ri.ver[0] == 'C' && ri.ver[1] == 'Y') {
				goto cwx_p;
			}
			if (rom.size() < 0x60000) {
				return ri;
			}
			if (dat[0x5ffee] != 'E') { // this means... it is stored at 0x71xxx
				if (rom.size() < 0x80000) {
					return ri;
				}
				memcpy(&dat[0x5e000], &dat[0x70000], 0x2000);
			}
			memcpy(ri.ver, &dat[0x5ffee], 8);
			memcpy(ri.cid, &dat[0x5fff8], 8);
			if (ri.ver[0] != 'E') {
				auto ver = (byte*)FindSignature(dat, 0x5e000, "?? 00 e9 90 ca ff ?? 00 e9 90 cb ff ?? 00 e9 90 cc ff ?? 00 e9 90 cd ff ?? 00 e9 90 ce ff ?? 00 e9 90 cf ff");
				auto ver2 = (byte*)FindSignature(dat, 0x5e000, "56 00 e9 90 d1 ff 2e 00 e9 90 d2 ff");
				if (ver && ver2) {
					auto ofst = ver2[14] | (ver2[15] << 8);
					for (size_t i = 0; i < 6; i++) {
						ri.ver[i] = ver[i * 6];
					}
					ri.ver[6] = dat[ofst];
					ri.ver[7] = dat[ofst + 1];
				}
			}
			ri.desired_sum = le_read(dat[0x5fff6]);
			sum_type = CWII;
		}
	}
	else {
		auto str = (byte*)FindSignature(dat, 0x8000, "49 4E 52 4f 4d 2D");
		if (str) {
			if (flash.size() < 0x80000) {
				return ri;
			}
			ri.type = RomInfo::Fx5800p;
			ri.desired_sum = le_read(dat2[0x7fffe]);
			memcpy(ri.ver, str + 2, 8);
			if (checksum) {
				calc3(ri.real_sum, &dat2[0x40000], 0x3fffe);
			}
			ri.ok = true;
			return ri;
		}
		str = (byte*)FindSignature(dat, 0x8000, "52 4f 4d 20 30");
		if (str) {
			ri.type = RomInfo::ES;
			memcpy(ri.ver, str, 8);
			ri.ok = true;
			return ri;
		}

		if (rom.size() < 0x20000) {
			return ri;
		}
		memcpy(ri.ver, &dat[0x1fff4], 8);
		ri.desired_sum = le_read(dat[0x1fffc]);
		sum_type = ESP1;
	}
	if (sum_type == ESP1) {
		if (ri.ver[0] == 'L' || ri.ver[0] == 'G') {
			sum_type = ESP1;
		}
		else if (ri.ver[0] == 'C') {
			sum_type = ESP2;
		}
		else {
			return ri;
		}
	}
	// std::cout << sum_type << "\n";
	switch (sum_type) {
	case ESP1:
		// calc(real_sum, dat, 0xfc00);
		if (checksum) {
			calc2(ri.real_sum, dat, 0x10000);
			calc2(ri.real_sum, &dat[0x10000], 0xfffc);
		}
		ri.type = RomInfo::ESP;
		break;
	case ESP2:
		// calc(real_sum, dat, 0xfc00);
		if (checksum) {
			calc2(ri.real_sum, dat, 0x10000);
			calc2(ri.real_sum, &dat[0x10000], 0xff40);
			calc2(ri.real_sum, &dat[0x1ffd0], 0x2c);
		}
		ri.type = RomInfo::ESP2nd;
		break;
	case CWX:
		if (checksum) {
			calc(ri.real_sum, dat, 0xfc00);
			calc(ri.real_sum, &dat[0x10000], 0x2fff6);
		}
		ri.type = RomInfo::CWX;
		break;
	case CWII:
		if (checksum) {
			calc(ri.real_sum, dat, 0xfc00);
			calc(ri.real_sum, &dat[0x10000], 0x4fff6);
		}
		ri.type = RomInfo::CWII;
		break;
	default:
		return ri;
	}
	ri.ok = true;
	return ri;
}