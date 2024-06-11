#pragma once
#include <string>
namespace cwii {
	inline std::string trim(const std::string& str) {
		size_t first = str.find_first_not_of('0');
		if (first == std::string::npos)
			return "0";
		size_t last = str.find_last_not_of('0');
		return str.substr(first, (last - first + 1));
	}
	inline std::string trimEnd(const std::string& str) {
		size_t last = str.find_last_not_of('0');
		if (last == std::string::npos)
			return "0";
		if (str[last] == '.')
			return str.substr(0, last);
		return str.substr(0, last + 1);
	}
	inline std::string trimStart(const std::string& str) {
		size_t first = str.find_first_not_of('0');
		if (first == std::string::npos)
			return "0";
		return str.substr(first);
	}

	inline std::string HexizeString(const char* p, size_t size) {
		std::string fin;
		const char stock[] = "0123456789ABCDEF";
		for (size_t i = 0; i < size; i++) {
			fin += stock[(p[i] >> 4) & 0xF];
			fin += stock[(p[i] & 0xF)];
		}
		return fin;
	}
	inline std::string HexExp(char p, int sign) {
		std::string fin;
		const char* ss = "????????????????0123456789ABCDEF";
		const char* stock = ss + 0x10;
		if (sign == 1) {
			fin += stock[(p >> 4) & 0xF];
			fin += stock[(p & 0xF)];
			if (fin[0] == '0' && fin[1] == '0')
				return "0";
		}
		else {
			fin += stock[0x9 - (p >> 4) & 0xF];
			fin += stock[0xA - (p & 0xF)];
			if (((p >> 4) & 0xF) == 0 && (p & 0xF) == 0)
				return "0";
		}
		return sign == 1 ? trimStart(fin) : "-" + trimStart(fin);
	}
	inline int ConvertSign(char sign, int& expsign, int& numbersign) {
		switch (sign) {
		case 0x00:
			expsign = -1;
			break;
		case 0x01:
			break;
		case 0x05:
			expsign = -1;
			numbersign = -1;
			break;
		case 0x06:
			numbersign = -1;
			break;
		default:
			return 0;
		}
		return 1;
	}
	inline std::string StringizeCwiiNumber(const char* p) {
		try {

			auto type = (p[0] >> 4) & 0xF;
			auto exp = p[12];
			auto sign = p[13]; // 0xE == 14
			auto numbersign = 1;
			auto expsign = 1;
			if (!ConvertSign(sign, expsign, numbersign))
				0;
			// return "";
			auto base = HexizeString(p, 12);
			switch (type) {
			case 0x0:
			case 0x4: {
				auto exps = HexExp(exp, expsign);
				base[0] = 0;
				auto ind = base.find_first_not_of('0'); // find non zero
				if (ind == 0)
					ind = 1;
				auto nb = base.substr(ind);
				auto expn = std::stoi(exps) - ((int)ind - 1);
				if (std::abs(expn) <= 9) {
					if (expn > 0) {
						nb.insert(nb.begin() + expn + 1, '.');
					}
					else {
						return trimEnd(nb.substr(0, 11));
					}
				}
				else {
					nb.insert(nb.begin() + 1, '.');
					if (numbersign == -1) {
						nb.insert(nb.begin(), '-');
					}
					if (exps != "0")
						return trimEnd(nb.substr(0, 11)) + "x10^" + exps;
					return trimEnd(nb.substr(0, 11));
				}
			}
			case 0x2: {
				auto ind = base.find_first_of('A');
				auto ind2 = base.find_last_of('A');
				if (ind != ind2) {
					if (ind < base.size())
						base[ind] = '+';
				}
				if (ind2 < base.size())
					base[ind2] = '/';
				base.erase(0, 1);
				base.resize(exp, '0');
				if (numbersign == -1) {
					base = "-" + base;
				}
				return base;
			}
			case 0x8: {
				auto numbersign2 = 1;
				auto expsign2 = 1;
				if (!ConvertSign(exp, expsign2, numbersign2))
					0;
				// return "";
				std::string fin;
				auto sqrt1 = trim(base.substr(1, 3));
				auto a1 = trim(base.substr(4, 2));
				auto b1 = trim(base.substr(6, 2));
				auto sqrt2 = trim(base.substr(1 + 8, 3));
				auto a2 = trim(base.substr(4 + 8, 2));
				auto b2 = trim(base.substr(6 + 8, 2));
				if (!(sqrt1.empty() || a1.empty())) {
					if (numbersign == -1) {
						fin += "-";
					}
					if (a1 == "1" && b1 == "1") {
						if (sqrt1 != "1") {
							fin += "sqrt(";
							fin += sqrt1;
							fin += ")";
						}
						else {
							fin += "1";
						}
					}
					else if (b1 == "1") {
						if (a1 != "1")
							fin += a1;
						if (sqrt1 != "1") {
							fin += "sqrt(";
							fin += sqrt1;
							fin += ")";
						}
					}
					else {
						if (a1 != "1")
							fin += a1;
						if (sqrt1 != "1") {
							fin += "sqrt(";
							fin += sqrt1;
							fin += ")";
						}
						fin += "/";
						fin += b1;
					}
				}
				if (!(sqrt2.empty() || a2.empty())) {
					if (numbersign2 == -1) {
						fin += "-";
					}
					else {
						if (!fin.empty())
							fin += "+";
					}
					if (a2 == "1" && b2 == "1") {
						if (sqrt2 != "1") {
							fin += "sqrt(";
							fin += sqrt2;
							fin += ")";
						}
						else {
							fin += "1";
						}
					}
					else if (b2 == "1") {
						if (a2 != "1")
							fin += a2;
						if (sqrt2 != "1") {
							fin += "sqrt(";
							fin += sqrt2;
							fin += ")";
						}
					}
					else {
						if (a2 != "1")
							fin += a2;
						if (sqrt2 != "1") {
							fin += "sqrt(";
							fin += sqrt2;
							fin += ")";
						}
						fin += "/";
						fin += b2;
					}
				}
				return fin;
			} break;
			// case 0x6:
			//	return "";
			case 0xF:
				return "Error";
			default:
				return "";
			}
		}
		catch (...) {
			return "";
		}
	}
}