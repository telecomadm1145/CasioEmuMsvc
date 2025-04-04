// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include <iostream>
#include <iomanip>
#include <sdl.h>
#include <imgui_impl_sdl2.h>
#include<sstream>
#include "Memory.h"

// 最近邻缩放函数，scale 必须 >= 1
SDL_Surface* ScaleSurfaceNearest(SDL_Surface* src, int scale)
{
	if (!src || scale < 1)
		return nullptr;

	int new_w = src->w * scale;
	int new_h = src->h * scale;

	// 根据源图像的像素格式创建目标 surface
	SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(0, new_w, new_h,
		src->format->BitsPerPixel,
		src->format->format);
	if (!scaled) {
		SDL_Log("无法创建缩放后的 surface: %s", SDL_GetError());
		return nullptr;
	}

	// 如果需要则锁定 surface
	if (SDL_MUSTLOCK(src))
		SDL_LockSurface(src);
	if (SDL_MUSTLOCK(scaled))
		SDL_LockSurface(scaled);

	// 每个像素占用的字节数
	int bpp = src->format->BytesPerPixel;
	for (int y = 0; y < new_h; y++) {
		for (int x = 0; x < new_w; x++) {
			// 计算源图中对应的坐标（最近邻算法）
			int src_x = x / scale;
			int src_y = y / scale;

			// 计算源像素的地址
			Uint8* src_pixel = static_cast<Uint8*>(src->pixels) + src_y * src->pitch + src_x * bpp;
			// 计算目标像素的地址
			Uint8* dst_pixel = static_cast<Uint8*>(scaled->pixels) + y * scaled->pitch + x * bpp;
			// 复制 bpp 个字节（像素数据）
			std::memcpy(dst_pixel, src_pixel, bpp);
		}
	}

	if (SDL_MUSTLOCK(src))
		SDL_UnlockSurface(src);
	if (SDL_MUSTLOCK(scaled))
		SDL_UnlockSurface(scaled);

	return scaled;
}

PluginApi* g_api;
using byte = uint8_t;
using nint = intptr_t;
class Cw2toolsWindow :public UIWindow {
public:
	uint8_t* rom = 0;
	int is_cwii = 0;
	nint ne_00;
	nint ne_fx;
	nint va_f1;
	nint va_f2;
	int index;
	bool va_fix;
	int font_type;
	Cw2toolsWindow() : UIWindow("Cw2tools") {
		rom = (uint8_t*)g_api->QueryInterface<IChipset>()->GetRom();
		if (g_api->QueryInterface<IEmulator>()->ModelDefinition()->hardware_id == casioemu::HardwareId::HW_CLASSWIZ_II) {
			is_cwii = 1;
		}
	}
	auto BytesToBitSet(byte* bytes, nint length)
	{
		auto bitSet = std::vector<bool>(length * 8);
		if (bytes + length >= rom + 0x80000)
		{
			return bitSet;
		}
		for (int i = 0; i < length; i++)
		{
			for (int j = 0; j < 8; j++)
			{
				// 位掩码用于检查每个位
				bitSet[i * 8 + (7 - j)] = (bytes[i] & (1 << j)) != 0;
			}
		}
		return bitSet;
	}
	auto BytesToBitSetLE(byte* bytes, nint length)
	{
		auto bitSet = std::vector<bool>(length * 8);
		if (bytes + length >= rom + 0x80000)
		{
			return bitSet;
		}
		for (int i = 0; i < length; i += 2)
		{
			for (int j = 0; j < 8; j++)
			{
				bitSet[(i + 1) * 8 + j] = (bytes[i] & (0x80 >> j)) != 0;
			}
			for (int j = 0; j < 8; j++)
			{
				bitSet[i * 8 + j] = (bytes[i + 1] & (0x80 >> j)) != 0;
			}
		}
		return bitSet;
	}
	struct BitmapContext {
		SDL_Surface* surface;
		uint8_t* pixels;
		int pitch;
	};

	SDL_Surface* Get(nint bs, int index, int width, int height) {
		return (font_type == 0) ? Clip(bs, index, width, height) : Clip2(bs, index, width, height);
	}

	SDL_Surface* Clip(nint bs, int index, int width, int height) {
		SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
		if (!surface) return nullptr;

		SDL_LockSurface(surface);
		uint8_t* buf = static_cast<uint8_t*>(surface->pixels);

		int wi = width * index;
		int index2 = (wi >> 4);
		int n = wi & 15;

		auto array1 = BytesToBitSetLE((byte*)rom + bs + 2 * index2 * height, 32 * height);
		int d = 0;

		for (int j = 0; j < height; ++j) {
			int e = n + d;
			for (int i = 0; i < (16 - n); ++i) {
				uint8_t value = array1[e++] ? 0x00 : 0xFF;
				uint32_t* pixel = reinterpret_cast<uint32_t*>(&buf[j * surface->pitch + i * 4]);
				*pixel = SDL_MapRGBA(surface->format, 255, 255, 255, value);
			}
			d += 16;
		}

		for (int j = 0; j < height; ++j) {
			int e = d;
			for (int i = (16 - n); i < width; ++i) {
				uint8_t value = array1[e++] ? 0x00 : 0xFF;
				uint32_t* pixel = reinterpret_cast<uint32_t*>(&buf[j * surface->pitch + i * 4]);
				*pixel = SDL_MapRGBA(surface->format,255, 255, 255, value);
			}
			d += 16;
		}

		SDL_UnlockSurface(surface);
		return surface;
	}

	SDL_Surface* Clip2(nint bs, int index, int width, int height) {
		SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
		if (!surface) return nullptr;

		SDL_LockSurface(surface);
		uint8_t* buf = static_cast<uint8_t*>(surface->pixels);

		int wi = width * index;
		int index2 = (wi / 8);
		int n = wi % 8;

		auto array1 = BytesToBitSet((byte*)rom +  bs + index2 * height, 32 * height);
		int d = 0;

		for (int j = 0; j < height; ++j) {
			int e = n + d;
			for (int i = 0; i < (8 - n); ++i) {
				uint8_t value = array1[e++] ? 0x00 : 0xFF;
				uint32_t* pixel = reinterpret_cast<uint32_t*>(&buf[j * surface->pitch + i * 4]);
				*pixel = SDL_MapRGBA(surface->format, 255, 255, 255, value);
			}
			d += 8;
		}

		for (int j = 0; j < height; ++j) {
			int e = d;
			for (int i = (8 - n); i < width; ++i) {
				uint8_t value = array1[e++] ? 0x00 : 0xFF;
				uint32_t* pixel = reinterpret_cast<uint32_t*>(&buf[j * surface->pitch + i * 4]);
				*pixel = SDL_MapRGBA(surface->format, 255, 255, 255, value);
			}
			d += 8;
		}

		SDL_UnlockSurface(surface);
		return surface;
	}

	void Set(nint bs, int index, int width, int height, SDL_Surface* src) {
		SDL_LockSurface(src);
		uint8_t* data = static_cast<uint8_t*>(src->pixels);
		uint16_t* buf = reinterpret_cast<uint16_t*>(bs);

		int wi = width * index;
		int index2 = (wi >> 4);
		int n = wi & 15;

		for (int j = 0; j < height; ++j) {
			int e = n;
			for (int i = 0; i < std::min(16 - n, width); ++i) {
				uint8_t* pixel = &data[j * src->pitch + i * 4];
				bool b = (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0);
				uint16_t mask = static_cast<uint16_t>(0x8000 >> e++);
				buf[index2 * height + j] = b ? (buf[index2 * height + j] & ~mask) : (buf[index2 * height + j] | mask);
			}
		}

		for (int j = 0; j < height; ++j) {
			int e = 0;
			for (int i = (16 - n); i < width; ++i) {
				uint8_t* pixel = &data[j * src->pitch + i * 4];
				bool b = (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0);
				uint16_t mask = static_cast<uint16_t>(0x8000 >> e++);
				buf[index2 * height + j + height] = b ? (buf[index2 * height + j + height] & ~mask)
					: (buf[index2 * height + j + height] | mask);
			}
		}

		SDL_UnlockSurface(src);
	}

	void Set2(nint bs, int index, int width, int height, SDL_Surface* src) {
		SDL_LockSurface(src);
		uint8_t* data = static_cast<uint8_t*>(src->pixels);
		uint8_t* buf = (uint8_t*)bs;

		int wi = width * index;
		int index2 = (wi >> 3);
		int n = wi & 7;

		for (int j = 0; j < height; ++j) {
			int e = n;
			for (int i = 0; i < std::min(8 - n, width); ++i) {
				uint8_t* pixel = &data[j * src->pitch + i * 4];
				bool b = (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0);
				uint8_t mask = static_cast<uint8_t>(0x80 >> e++);
				buf[index2 * height + j] = b ? (buf[index2 * height + j] & ~mask)
					: (buf[index2 * height + j] | mask);
			}
		}

		for (int j = 0; j < height; ++j) {
			int e = 0;
			for (int i = (8 - n); i < width; ++i) {
				uint8_t* pixel = &data[j * src->pitch + i * 4];
				bool b = (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0);
				uint8_t mask = static_cast<uint8_t>(0x80 >> e++);
				buf[index2 * height + j + height] = b ? (buf[index2 * height + j + height] & ~mask)
					: (buf[index2 * height + j + height] | mask);
			}
		}

		SDL_UnlockSurface(src);
	}

	// 将 LookupTable 方法转换为 C++ 版本
	int LookupTable(int ind) {
		if (font_type != 0) {
			if (font_type == 2) {
				if (is_cwii)
					return (reinterpret_cast<const int*>(rom + ne_fx)[ind]) & 0xffffff;
				else
					return reinterpret_cast<const uint16_t*>(rom + ne_fx)[ind];
			}
			return ne_00;
		}
		if (is_cwii) {
			return (reinterpret_cast<const int*>(rom + ne_fx)[ind]) & 0xffffff;
		}
		else {
			if (va_fix) {
				if (ind == 0)
					return va_f1;
				else if (ind == 1)
					return va_f2;
				else
					return ne_00;
			}
			return reinterpret_cast<const uint16_t*>(rom + ne_fx)[ind];
		}
	}

	// 将 LookupChar 方法转换为 C++ 版本
	SDL_Surface* LookupChar(int codepoint) {
		// 取高字节，并转换为 uint8_t
		uint8_t head = static_cast<uint8_t>((codepoint & 0xff00) >> 8);
		// 取低字节，并减去 0x10（注意：(byte)codepoint 在 C# 中取的是低8位）
		uint8_t ind = static_cast<uint8_t>((codepoint & 0xff) - 0x10);
		int w = 10;
		int h = 13;
		int w2 = 11;
		int h2 = 12;

		if (font_type != 0) {
			w = w2 = 5;
			if (font_type == 1)
				h = h2 = 7;
			else
				h = h2 = 9;
		}

		if (head >= 0xf0) {
			head -= 0xf0;
			int tbl = LookupTable(head);
			if (head == 1 || (head == 2 && !va_fix))
				return Get(tbl, ind, w2, h2);
			return Get(tbl, ind, w, h);
		}
		else if (head != 0) {
			throw std::runtime_error("Codepoint is WRONG!");
		}
		else {
			return Get(ne_00, ind, w, h);
		}
	}
 void LoadSettings()
	{
		if (font_type == 0)
		{
			ne_00 = std::strtol(NE_00_Input.c_str(), 0, 16);
			if (va_fix)
			{
				va_f1 = std::strtol(VA_F0_Input.c_str(), 0, 16);
				va_f2 = std::strtol(VA_F1_Input.c_str(), 0, 16);
			}
			else
				ne_fx = std::strtol(NE_Fx_Input.c_str(), 0, 16);
		}
		else if (font_type == 1)
		{
			ne_00 = std::strtol(L8_00_Input.c_str(), 0, 16);
			va_fix = false;
		}
		else
		{
			ne_00 = std::strtol(LA_00_Input.c_str(), 0, 16);
			ne_fx = std::strtol(LA_Fx_Input.c_str(), 0, 16);
			va_fix = false;
		}
	}

	// 将 SetChar 方法转换为 C++ 版本
	void SetChar(int codepoint, SDL_Surface* src) {
		uint8_t head = static_cast<uint8_t>((codepoint & 0xff00) >> 8);
		uint8_t ind = static_cast<uint8_t>((codepoint & 0xff) - 0x10);

		switch (font_type) {
		case 0:
		{
			if (head >= 0xf0) {
				head -= 0xf0;
				int tbl = LookupTable(head);
				if (head == 1 || (head == 2 && !va_fix))
					Set(tbl, ind, 11, 12, src);
				else
					Set(tbl, ind, 10, 13, src);
			}
			else if (head != 0) {
				throw std::runtime_error("Codepoint is WRONG!");
			}
			else {
				Set(ne_00, ind, 10, 13, src);
			}
			break;
		}
		case 1:
		{
			Set2(ne_00, ind, 5, 7, src);
			break;
		}
		case 2:
		{
			if (head >= 0xf0) {
				head -= 0xf0;
				int tbl = LookupTable(head);
				Set2(tbl, ind, 5, 9, src);
			}
			else if (head != 0) {
				throw std::runtime_error("Codepoint is WRONG!");
			}
			else {
				Set2(ne_00, ind, 5, 9, src);
			}
			break;
		}
		// 如果有其他 font_type，可在此添加默认处理……
		}
	}


	// 用于显示结果的变量，模拟原来界面上各文本框的 Text 属性
	std::string NE_00_Input, NE_Fx_Input, NE_F1_Input, NE_F2_Input, L8_00_Input, VA_F0_Input, VA_F1_Input, LA_00_Input, LA_Fx_Input;

	// 一个 stub 函数，原代码中调用 SetVAFix(true)
	void SetVAFix(bool flag)
	{
		// 根据需要实现修正逻辑，此处仅作示意
		printf("SetVAFix(%s)\n", flag ? "true" : "false");
	}

	void Button_Click_11()
	{
		// 先将所有文本内容置为 "????"
		NE_00_Input = NE_Fx_Input = L8_00_Input = VA_F0_Input = VA_F1_Input = LA_00_Input = LA_Fx_Input = "????";
		va_fix = false;
		if (is_cwii)
		{
			switch (font_type)
			{
			case 0:
			{
				// 查找第一个签名
				unsigned char* r = (unsigned char*)FindSignature(rom, 0x60000,
					"00 ?? 01 ?? 02 f2 b0 f4 d2 7c 78 02 c9");
				if (r)
					printf("%04X\n", (unsigned int)(r - rom));
				else
					printf("not found\n");
				if (r != nullptr)
				{
					r -= 1;
					// 组合三个字节：r[0]、r[2]、r[4]
					uint32_t code = r[0] | (r[2] << 8) | (r[4] << 16);
					std::stringstream ss;
					ss << std::uppercase << std::hex << std::setw(4)
						<< std::setfill('0') << code;
					NE_00_Input = ss.str();
				}
				// 查找第二个签名
				r = (unsigned char*)FindSignature(rom, 0x60000,
					"00 01 0f 20 00 21 2b 91 2a 90 08 a2 ?? ?? 08 90 ?? ?? f2 b2 f4 d0");
				if (r)
					printf("%04X\n", (unsigned int)(r - rom));
				else
					printf("not found\n");
				if (r == nullptr)
					return;
				r -= 2;
				// 检查条件：*(ushort*)(r + 0xE) == *(ushort*)(r + 0x12) - 2
				if (*reinterpret_cast<uint16_t*>(r + 0xE) != (*reinterpret_cast<uint16_t*>(r + 0x12)) - 2)
					return;
				{
					uint16_t val = *reinterpret_cast<uint16_t*>(r + 0xE);
					std::stringstream ss;
					ss << std::uppercase << std::hex << std::setw(4)
						<< std::setfill('0') << val;
					NE_Fx_Input = ss.str();
				}
				break;
			}
			case 1:
			{
				unsigned char* r = (unsigned char*)FindSignature(rom, 0x60000,
					"05 fc f4 d2 16 ce 09 00 ff d0 90 80");
				if (r)
					printf("%04X\n", (unsigned int)(r - rom));
				else
					printf("not found\n");
				if (r != nullptr)
				{
					r -= 1;
					uint32_t code = static_cast<uint32_t>(r[-5])
						| (static_cast<uint32_t>(r[-3]) << 8)
						| (static_cast<uint32_t>(r[-1]) << 16);
					std::stringstream ss;
					ss << std::uppercase << std::hex << std::setw(4)
						<< std::setfill('0') << code;
					L8_00_Input = ss.str();
				}
				break;
			}
			case 2:
			{
				unsigned char* r = (unsigned char*)FindSignature(rom, 0x60000,
					"05 fc f4 d2 00 74 14 c8 7e d0 7c 70");
				if (r)
					printf("%04X\n", (unsigned int)(r - rom));
				else
					printf("not found\n");
				if (r != nullptr)
				{
					r -= 1;
					uint32_t code = static_cast<uint32_t>(r[-5])
						| (static_cast<uint32_t>(r[-3]) << 8)
						| (static_cast<uint32_t>(r[-1]) << 16);
					std::stringstream ss;
					ss << std::uppercase << std::hex << std::setw(4)
						<< std::setfill('0') << code;
					LA_00_Input = ss.str();
				}
				r = (unsigned char*)FindSignature(rom, 0x60000,
					"00 01 0f 20 00 21 2b 91 2a 90 08 a2");
				if (r != nullptr)
				{
					if (*reinterpret_cast<uint16_t*>(r + 0xC) != (*reinterpret_cast<uint16_t*>(r + 0x10)) - 2)
						return;
					uint16_t val = *reinterpret_cast<uint16_t*>(r + 0xC);
					std::stringstream ss;
					ss << std::uppercase << std::hex << std::setw(4)
						<< std::setfill('0') << val;
					LA_Fx_Input = ss.str();
				}
				break;
			}
			}
		}
		else  // 非 cwii 版
		{
			switch (font_type)
			{
			case 0:
			{
				unsigned char* r = (unsigned char*)FindSignature(rom, 0x40000,
					"07 81 92 c3 ea a1 3e f8 2e f4 8e f2");
				if (r)
					printf("%04X\n", (unsigned int)(r - rom));
				else
					printf("not found\n");
				if (r != nullptr)
				{
					uint16_t code = static_cast<uint16_t>(r[-4])
						| (static_cast<uint16_t>(r[-2]) << 8);
					std::stringstream ss;
					ss << std::uppercase << std::hex << std::setw(4)
						<< std::setfill('0') << code;
					NE_00_Input = ss.str();
				}
				r = (unsigned char*)FindSignature(rom, 0x40000,
					"21 80 00 61 02 90 0a f0 2e f0");
				if (r)
					printf("%04X\n", (unsigned int)(r - rom));
				else
					printf("not found\n");
				if (r != nullptr)
				{
					uint16_t code = static_cast<uint16_t>(r[-4])
						| (static_cast<uint16_t>(r[-2]) << 8);
					std::stringstream ss;
					ss << std::uppercase << std::hex << std::setw(4)
						<< std::setfill('0') << code;
					NE_Fx_Input = ss.str();
				}
				else
				{
					r = (unsigned char*)FindSignature(rom, 0x40000,
						"0c 08 5e f2 5e f4 00 7c 0a c8 7c 70");
					if (r)
						printf("%04X\n", (unsigned int)(r - rom));
					else
						printf("not found\n");
					if (r != nullptr)
					{
						uint16_t val0 = *reinterpret_cast<uint16_t*>(r - 10);
						uint16_t val1 = *reinterpret_cast<uint16_t*>(r - 2);
						std::stringstream ss0, ss1;
						ss0 << std::uppercase << std::hex << std::setw(4)
							<< std::setfill('0') << val0;
						ss1 << std::uppercase << std::hex << std::setw(4)
							<< std::setfill('0') << val1;
						VA_F0_Input = ss0.str();
						VA_F1_Input = ss1.str();
						va_fix = true;
						return;
					}
				}
				break;
			}
			case 1:
			{
				unsigned char* r = (unsigned char*)FindSignature(rom, 0x40000,
					"07 08 00 ce 5e f2 5e f4 00 79 09 c8 7c");
				if (r)
					printf("%04X\n", (unsigned int)(r - rom));
				else
					printf("not found\n");
				if (r != nullptr)
				{
					uint16_t val = *reinterpret_cast<uint16_t*>(&r[-2]);
					std::stringstream ss;
					ss << std::uppercase << std::hex << std::setw(4)
						<< std::setfill('0') << val;
					L8_00_Input = ss.str();
				}
				break;
			}
			case 2:
			{
				unsigned char* r = (unsigned char*)FindSignature(rom, 0x40000,
					"00 79 0b c9 6e f0 90 82 0f 22 1a 92");
				if (r)
					printf("%04X\n", (unsigned int)(r - rom));
				else
					printf("not found\n");
				if (r != nullptr)
				{
					uint16_t val = *reinterpret_cast<uint16_t*>(&r[-2]);
					{
						std::stringstream ss;
						ss << std::uppercase << std::hex << std::setw(4)
							<< std::setfill('0') << val;
						LA_00_Input = ss.str();
					}
					uint16_t code = r[0xC] | (r[0xE] << 8);
					std::stringstream ss2;
					ss2 << std::uppercase << std::hex << std::setw(4)
						<< std::setfill('0') << code;
					LA_Fx_Input = ss2.str();
				}
				break;
			}
			}
		}

		// 输出各项结果（仅为调试用途）
		std::cout << "NE_00_Input: " << NE_00_Input << std::endl;
		std::cout << "NE_Fx_Input: " << NE_Fx_Input << std::endl;
		std::cout << "L8_00_Input: " << L8_00_Input << std::endl;
		std::cout << "VA_F0_Input: " << VA_F0_Input << std::endl;
		std::cout << "VA_F1_Input: " << VA_F1_Input << std::endl;
		std::cout << "LA_00_Input: " << LA_00_Input << std::endl;
		std::cout << "LA_Fx_Input: " << LA_Fx_Input << std::endl;
	}

	int Codepoint = 0x10;
	char cp[8];
	SDL_Texture* preview = nullptr;
	int w = 0, h = 0;
	void RenderCore() override {
		if (ImGui::BeginTabBar("Cw2tools")) {
			if (ImGui::BeginTabItem("Font")) {
				if (ImGui::BeginTabBar("Font types")) {
					if (ImGui::BeginTabItem("Normal 14")) {
						font_type = 0;
						ImGui::Checkbox("Version A fix", &va_fix);
						NE_00_Input.reserve(6);
						ImGui::InputText("00", NE_00_Input.data(), 6);
						if (!va_fix) {
							NE_Fx_Input.reserve(6);
							ImGui::InputText("Fx", NE_Fx_Input.data(), 6);
						}
						else {
							NE_F1_Input.reserve(6);
							ImGui::InputText("F1", NE_F1_Input.data(), 6);
							NE_F2_Input.reserve(6);
							ImGui::InputText("F2", NE_F2_Input.data(), 6);
						}
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("Small 10")) {
						font_type = 2;
						LA_00_Input.reserve(6);
						ImGui::InputText("00", LA_00_Input.data(), 6);
						LA_Fx_Input.reserve(6);
						ImGui::InputText("Fx", LA_Fx_Input.data(), 6);
						ImGui::EndTabItem();

					}
					if (ImGui::BeginTabItem("Tiny 8")) {
						font_type = 1;
						L8_00_Input.reserve(6);
						ImGui::InputText("00", L8_00_Input.data(), 6);
						ImGui::EndTabItem();

					}
					ImGui::EndTabBar();
				}
				if (ImGui::Button("AutoSearch"))
					Button_Click_11();
				if (preview != 0)
					ImGui::Image((ImTextureID)preview, { (float)w,(float)h });
				ImGui::InputText("Codepoint", cp, 8);
				Codepoint = std::strtol(cp, 0, 16);
				if (ImGui::Button("Lookup"))
				{
					LoadSettings();
					// 获取原始字符图像
					SDL_Surface* surface = LookupChar(Codepoint);
					if (!surface)
					{
						SDL_Log("LookupChar 返回空 surface");
						return;
					}

					int scale = 4; // 这里设置放大倍数，例如4倍
					// 手动最近邻缩放
					SDL_Surface* scaled_surface = ScaleSurfaceNearest(surface, scale);
					if (!scaled_surface)
					{
						SDL_Log("缩放失败");
						SDL_FreeSurface(surface);
						return;
					}

					// 更新显示宽高（已手动放大）
					w = scaled_surface->w;
					h = scaled_surface->h;

					// 从 ImGui IO 中取得 renderer 指针
					auto renderer = *(SDL_Renderer**)ImGui::GetIO().BackendRendererUserData;
					if (preview)
						SDL_DestroyTexture(preview);
					preview = SDL_CreateTextureFromSurface(renderer, scaled_surface);

					// 释放临时生成的 surface（原始 surface 也要释放）
					SDL_FreeSurface(scaled_surface);
					SDL_FreeSurface(surface);
				}
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
};
extern "C" _declspec(dllexport) void fPluginLoad(PluginApi* api) {
	if (api == 0)
		return;
	g_api = api;
	PLUGINASSERTSTL(api);
	if (api->GetVersion() < 1)
	{
		std::cout << "This plugin requires at least PluginApi v1 to execute.\n";
		return;
	}
	if (!api->RegisterPlugin("example", "ExamplePlugin", 0))
		return;
	api->AddWindow(new Cw2toolsWindow());
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

