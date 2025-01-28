#include "GameBuffer.h"
#include <concepts>
#include <array>
template <std::integral Int, typename CharT>
inline void itoa_2(Int num, CharT* out) {
	int j = 0;
	if (num == 0) {
		out[j++] = '0';
		out[j++] = 0;
		return;
	}
	Int num2 = num;
	while (num2 > 0) {
		num2 /= 10;
		j++;
	}
	out[j] = 0;
	j--;
	while (num) {
		out[j--] = "0123456789"[num % 10];
		num /= 10;
	}
}
inline auto Sqr(auto x) {
	return x * x;
}
void GameBuffer::EnsureCapacity() {
	Width = o_w;
	Height = o_h;
	auto target = Width * Height;
	if (PixelBuffer.size() < target) {
		PixelBuffer.resize(target);
	}
}
void GameBuffer::_ResizeBuffer() {
	EnsureCapacity();
}
void GameBuffer::CheckBuffer() {
	if (dirty_buffer) {
		_ResizeBuffer();
		dirty_buffer = false;
	}
}
void GameBuffer::ResizeBuffer(int width, int height) {
	o_w = width;
	o_h = height;
	dirty_buffer = true;
}
void GameBuffer::HideCursor() {
#ifndef __linux__
	constexpr char buf[] = "\u001b[?25l";
	write(buf, sizeof(buf));
#endif
}
void GameBuffer::InitConsole() {
	HideCursor();
}
void GameBuffer::Clear() {
	CheckBuffer();
	for (size_t i = 0; i < PixelBuffer.size(); i++) {
		PixelBuffer[i] = {};
	}
}
// Function to find the closest 256-color palette index
int ClosestConsoleColor(Color color) {
	// Define a basic set of console colors (simplified example)
	// You might want to expand this with a more comprehensive palette
	static const std::array<Color, 16> consoleColors = {
		Color{ 0, 0, 0 }, Color{ 128, 0, 0 }, Color{ 0, 128, 0 }, Color{ 128, 128, 0 },
		Color{ 0, 0, 128 }, Color{ 128, 0, 128 }, Color{ 0, 128, 128 }, Color{ 192, 192, 192 },
		Color{ 128, 128, 128 }, Color{ 255, 0, 0 }, Color{ 0, 255, 0 }, Color{ 255, 255, 0 },
		Color{ 0, 0, 255 }, Color{ 255, 0, 255 }, Color{ 0, 255, 255 }, Color{ 255, 255, 255 }
	};

	int bestIndex = 0;
	double bestDistance = std::numeric_limits<double>::max();

	for (size_t i = 0; i < consoleColors.size(); ++i) {
		double distance = std::pow((color.Red - consoleColors[i].Red), 2) +
						  std::pow((color.Green - consoleColors[i].Green), 2) +
						  std::pow((color.Blue - consoleColors[i].Blue), 2);
		if (distance < bestDistance) {
			bestDistance = distance;
			bestIndex = i;
		}
	}
	return bestIndex;
}

void GameBuffer::Output() {
	CheckBuffer();
	if (outbuf == 0) {
		outbuf = new char[buf_cap];
	}
	buf_i = 0;
	Color LastFg{ 255, 255, 255, 255 };
	Color LastBg{ 255, 0, 0, 0 };
	if (Height <= 0 && Width <= 0)
		return;
	WriteBufferString("\u001b[H\u001b[0m");

	for (size_t i = 0; i < Height; i++) {
		for (size_t j = 0; j < Width; j++) {
			PixelData dat = PixelBuffer[i * Width + j];

			if (dat.UcsChar == '\b')
				continue;

			if (dat.UcsChar == 0) {
				if (LastBg != Color{}) {
					WriteBufferString("\u001b[48;5;0m");
					LastBg = {};
				}
				WriteBufferChar(' ');
				continue;
			}

			int len = Measure(dat.UcsChar);
			if (len == 0)
				continue;
			if (len < 0 || dat.UcsChar < 32) {
				WriteBufferChar('?');
				continue;
			}

			dat.Background = Color::Blend(Color{ 255, 0, 0, 0 }, dat.Background);
			if (dat.Foreground != Color{})
				dat.Foreground = Color::Blend(dat.Background, dat.Foreground);
			else
				dat.Foreground = { 255, 255, 255, 255 };

			char commonbuf[4]{};

			if (LastFg != dat.Foreground) {
				int fgIndex = ClosestConsoleColor(dat.Foreground);
				WriteBufferString("\u001b[38;5;");
				itoa_2(fgIndex, commonbuf);
				WriteBufferString(commonbuf);
				WriteBufferChar('m');
				LastFg = dat.Foreground;
			}

			if (LastBg != dat.Background) {
				int bgIndex = ClosestConsoleColor(dat.Background);
				WriteBufferString("\u001b[48;5;");
				itoa_2(bgIndex, commonbuf);
				WriteBufferString(commonbuf);
				WriteBufferChar('m');
				LastBg = dat.Background;
			}

			char buf[4]{ 0 };
			int written = 0;
			Ucs4Char2Utf8(dat.UcsChar, buf, written);
			for (size_t i = 0; i < written; i++) {
				WriteBufferChar(buf[i]);
			}
		}
		WriteBufferChar('\n');
	}

	if (buf_i >= 1)
		buf_i--;

	write(outbuf, buf_i);
}

void GameBuffer::DrawString(const std::u32string& text, int startX, int startY, Color fg, Color bg,int overflow) {
	int x = startX;
	int y = startY;

	for (auto c : text) {
		if (c == '\n') {
			// Move to the next line
			x = startX;
			y++;
		}
		else {
			// Measure the width of the character
			if (c == '\t') {
				x++;
				continue;
			}
			int charWidth = Measure(c);
			if (x + charWidth <= b_right - 1) {
				// Set the pixel at the current position
				SetPixel(x, y, PixelData{ fg, bg, c });
				if (charWidth > 1)
					for (int i = 1; i < charWidth; i++) {
						SetPixel(x + i, y, PixelData{ {}, {}, '\b' });
					}
				x += charWidth;
			}
			else if (overflow == 1) {
				SetPixel(x - 1, y, PixelData{ {170,0,255,0}, bg, '>' });
			}
			else if (overflow == 0) {
				x = startX;
				y++;
				SetPixel(x, y, PixelData{ fg, bg, c });
				if (charWidth > 1)
					for (int i = 1; i < charWidth; i++) {
						SetPixel(x + i, y, PixelData{ {}, {}, '\b' });
					}
				x += charWidth;
			}
		}
	}
}

PixelData GameBuffer::GetPixel(int x, int y) {
	if (x < Width && y < Height && y > -1 && x > -1) {
		return PixelBuffer[y * Width + x];
	}
	throw std::out_of_range("X or Y out of range.");
}


void GameBuffer::SetPixel(int x, int y, PixelData pd) {
	if ((x < Width && y < Height && y > -1 && x > -1) &&
		(x <= b_right && y <= b_bottom && y >= b_top && x >= b_left)) {
		auto& ref = PixelBuffer[y * Width + x];
		if (pd.UcsChar != '\0') {
			if (ref.UcsChar == '\b') {
				for (int i = x; i >= 0; i--) {
					auto& ref2 = PixelBuffer[y * Width + i];
					ref2.UcsChar = ' ';
					if (ref2.UcsChar != '\b') {
						break;
					}
				}
			}
			if (pd.UcsChar != '\1')
				ref.UcsChar = pd.UcsChar;
		}
		ref.Background = Color::Blend(ref.Background, pd.Background);
		ref.Foreground = Color::Blend(ref.Foreground, pd.Foreground);
	}
}

void GameBuffer::SetPixelF(float x, float y, PixelData pd) {
	float x_i = 0.0;
	float x_f = std::modf(x, &x_i);
	float y_i = 0.0;
	float y_f = std::modf(y, &y_i);
	// Ratio

	// (1-x_f)*(1-y_f)	x_f*(1-y_f)
	// (1-x_f)*y_f		x_f*y_f
	SetPixel(x_i, y_i, pd * ((1 - x_f) * (1 - y_f)));
	SetPixel(x_i + 1, y_i, pd * (x_f * (1 - y_f)));
	SetPixel(x_i, y_i + 1, pd * ((1 - x_f) * y_f));
	SetPixel(x_i + 1, y_i + 1, pd * (x_f * y_f));
}

void GameBuffer::DrawLineH(float x, float y1, float y2, PixelData pd) {
	if (y1 > y2)
		std::swap(y1, y2);
	if (y1 == y2)
		SetPixel(x, y1, pd);
	y1 = std::max((float)0.0, y1);
	y2 = std::min(y2, (float)Height);
	for (int i = y1; i < y2; i++) {
		SetPixel(x, i, pd);
	}
}

void GameBuffer::DrawLineV(float x1, float x2, float y, PixelData pd) {
	if (x1 > x2)
		std::swap(x1, x2);
	float x1_i = 0.0;
	float x1_f = std::modf(x1, &x1_i);
	float x2_i = 0.0;
	float x2_f = std::modf(x2, &x2_i);
	float y_i = 0.0;
	float y_f = std::modf(y, &y_i);

	{
		float line0_weight = 1 - y_f;
		if (line0_weight > 1e-9) {
			PixelData pd2 = pd * line0_weight;
			if (x1_i == x2_i) {
				float weight = std::abs(x2_f - x1_f);
				SetPixel(x1_i, y_i, pd2 * weight);
			}
			else {
				float weight = 1 - x1_f;
				SetPixel(x1_i, y_i, pd2 * weight);
				float weight2 = x2_f;
				SetPixel(x2_i, y_i, pd2 * weight2);
				for (int i = x1_i + 1; i < x2_i; i++) {
					SetPixel(i, y_i, pd2);
				}
			}
		}
	}

	{
		float line1_weight = y_f;
		PixelData pd2 = pd * line1_weight;
		if (line1_weight > 1e-9) {
			if (x1_i == x2_i) {
				float weight = std::abs(x2_f - x1_f);
				SetPixel(x1_i, y_i + 1, pd2 * weight);
			}
			else {
				float weight = 1 - x1_f;
				SetPixel(x1_i, y_i + 1, pd2 * weight);
				float weight2 = x2_f;
				SetPixel(x2_i, y_i + 1, pd2 * weight2);
				for (int i = x1_i + 1; i < x2_i; i++) {
					SetPixel(i, y_i + 1, pd2);
				}
			}
		}
	}
}
void GameBuffer::DrawLine(int x1, int x2, int y1, int y2, PixelData pd) {
	int dx = abs(x2 - x1);
	int dy = abs(y2 - y1);
	//if (dx == 0) {
	//	DrawLineH(x1, y1, y2, pd);
	//	return;
	//}
	//if (dy == 0) {
	//	DrawLineV(x1, x2, y1, pd);
	//	return;
	//}
	int sx = (x1 < x2) ? 1 : -1;
	int sy = (y1 < y2) ? 1 : -1;
	int err = dx - dy;

	while (true) {
		// 到达终点
		if (x1 == x2 && y1 == y2) {
			break;
		}

		// 绘制当前像素
		SetPixel(x1, y1, pd);

		int err2 = 2 * err;

		// 调整 x 方向
		if (err2 > -dy) {
			err -= dy;
			x1 += sx;
		}

		// 调整 y 方向
		if (err2 < dx) {
			err += dx;
			y1 += sy;
		}
	}
}

void GameBuffer::FillRect(float left, float top, float right, float bottom, PixelData pd) {
	if (top > bottom)
		std::swap(top, bottom);
	left = std::clamp(left, 0.0f, (float)Width);
	right = std::clamp(right, 0.0f, (float)Width);
	top = std::clamp(top, 0.0f, (float)Height);
	bottom = std::clamp(bottom, 0.0f, (float)Height);
	float y1_i = 0.0;
	float y1_f = std::modf(top, &y1_i);
	float y2_i = 0.0;
	float y2_f = std::modf(bottom, &y2_i);
	if (y1_i == y2_i) {
		float weight = std::abs(y2_f - y1_f);
		DrawLineV(left, right, y1_i, pd * weight);
		return;
	}
	else {
		DrawLineV(left, right, y1_i, pd * (1 - y1_f));
		DrawLineV(left, right, y2_i, pd * (y1_f));
		for (int i = y1_i + 1; i < y2_i; i++) {
			DrawLineV(left, right, i, pd);
		}
	}
}

Color Color::Blend(Color a, Color b) {
	if (a == Color{})
		return b;
	if (b == Color{})
		return a;
	Color result;
	float alpha = static_cast<float>(b.Alpha) / 255.0f;
	float invAlpha = 1.0f - alpha;

	result.Red = static_cast<unsigned char>((a.Red * invAlpha) + (b.Red * alpha));
	result.Green = static_cast<unsigned char>((a.Green * invAlpha) + (b.Green * alpha));
	result.Blue = static_cast<unsigned char>((a.Blue * invAlpha) + (b.Blue * alpha));
	auto sum = a.Alpha + b.Alpha;
	if (sum > 255)
		sum = 255;
	result.Alpha = static_cast<unsigned char>(sum);

	return result;
}
