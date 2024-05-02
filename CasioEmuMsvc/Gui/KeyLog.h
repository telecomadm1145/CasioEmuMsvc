#pragma once
#include <vcruntime_string.h>
extern char m_keylog_buffer[4096];
extern int m_keylog_i;
extern bool m_keylog_enabled;
inline void Ucs4Char2Utf8(int codePoint, char* outbuf, int& outlen)
{
	outlen = 0;
	if (codePoint <= 0x7F)
	{
		// Single byte UTF-8 encoding for ASCII characters
		*outbuf = static_cast<char>(codePoint);
		outlen = 1;
	}
	else if (codePoint <= 0x7FF)
	{
		// Two byte UTF-8 encoding
		*outbuf = static_cast<char>(0xC0 | (codePoint >> 6));             // Byte 1
		outbuf++;
		*outbuf = static_cast<char>(0x80 | (codePoint & 0x3F));           // Byte 2
		outlen = 2;
	}
	else if (codePoint <= 0xFFFF)
	{
		// Three byte UTF-8 encoding
		*outbuf = static_cast<char>(0xE0 | (codePoint >> 12));            // Byte 1
		outbuf++;
		*outbuf = static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));    // Byte 2
		outbuf++;
		*outbuf = static_cast<char>(0x80 | (codePoint & 0x3F));           // Byte 3
		outlen = 3;
	}
	else if (codePoint <= 0x10FFFF)
	{
		// Four byte UTF-8 encoding
		*outbuf = static_cast<char>(0xF0 | (codePoint >> 18));            // Byte 1
		outbuf++;
		*outbuf = static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));   // Byte 2
		outbuf++;
		*outbuf = static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));    // Byte 3
		outbuf++;
		*outbuf = static_cast<char>(0x80 | (codePoint & 0x3F));           // Byte 4
		outlen = 4;
	}
}
inline  void AppendKey(int code) {
	if (!m_keylog_enabled)
		return;
	if (m_keylog_i > 4000)
	{
		memcpy(m_keylog_buffer + m_keylog_i, "…", 4);
		return;
	}
	int len = 0;
	Ucs4Char2Utf8(0xE000 + code, m_keylog_buffer + m_keylog_i, len);
	m_keylog_i += len;
}
inline int getHighestBitPosition(unsigned char byte) {
	int position = -1;
	for (int i = 0; i < 8; ++i) {
		if (byte & 1) {
			position = i;
		}
		byte >>= 1;
	}
	return position;
}

constexpr int cwii_keymap[8][8]{
	{0xff,0x51,0xff,0x24,0xff,0x04,0xff},
	{0x2b,0x3c,0x00,0x56,0x03,0x05,0xff},
	{0x4c,0x06,0x2f,0x30,0x32,0x28,0xff},
	{0x39,0x3d,0x4d,0x3b,0x40,0x44,0x0f},
	{0x4e,0x45,0x46,0x47,0x07,0x08,0x0d},
	{0x16,0x17,0x18,0x4a,0x22,0xff,0x2a},
	{0x13,0x14,0x15,0x2d,0x2e,0xff,0x49},
	{0x10,0x11,0x12,0x0a,0x4b,0xff,0x21}
};
class KeyLog
{
public:
	void Draw();
};