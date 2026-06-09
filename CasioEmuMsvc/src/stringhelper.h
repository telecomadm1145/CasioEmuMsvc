#pragma once
#include <string>
#include <algorithm>
#include <cctype>
inline void ltrim(std::string& s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
		return !std::isspace(ch);
	}));
}

// trim from end (in place)
inline void rtrim(std::string& s) {
	s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
		return !std::isspace(ch);
	}).base(),
		s.end());
}
inline std::string trim(const std::string& str) {
	std::string fin = str;
	ltrim(fin);
	rtrim(fin);
	return fin;
}