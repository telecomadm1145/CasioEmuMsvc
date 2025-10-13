#pragma once
#include <vector>
using word = unsigned short;
using byte = unsigned char;
struct RomInfo {
	char ver[9];
	byte cid[8];
	word desired_sum;
	word real_sum;
	enum {
		Unknown,
		ES,
		ESP,
		ESP2nd,
		CWX,
		CWII,
		Fx5800p,
	} type;
	bool ok;
};
inline char get_pd(byte pd) {
	if (pd == 0)
		return '-';
	auto lg = log(pd) / log(2);
	if (int(lg) != lg) {
		return '?';
	}
	return int(lg) + '0';
}

RomInfo rom_info(std::vector<byte>& rom, const std::vector<byte>& flash, bool checksum = true);