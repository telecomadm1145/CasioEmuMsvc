#pragma once

#include <SDL.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace casioemu {
class Emulator;
struct SpriteInfo;
struct ColourInfo;
struct ScreenOutputFrame {
	// All pointed-to frame data is borrowed for one synchronous Render call and
	// is never retained. Render and destruction must run on the renderer thread.
	SDL_Renderer* renderer = nullptr;
	SDL_Texture* interface_texture = nullptr;
	SDL_Surface* interface_surface = nullptr;
	const std::vector<SpriteInfo>* sprite_info = nullptr;
	const std::vector<uint8_t>* sprite_available = nullptr;
	const ColourInfo* ink_colour = nullptr;
	const float* screen_ink_alpha = nullptr;
	int logical_width = 0;
	int logical_height = 0;
	SDL_Rect lcd_dest{};
	bool render_pixel_layer = true;
};
class ScreenOutput {
public:
	explicit ScreenOutput(Emulator& emulator);
	~ScreenOutput();
	ScreenOutput(const ScreenOutput&) = delete;
	ScreenOutput& operator=(const ScreenOutput&) = delete;
	void Render(const ScreenOutputFrame& frame);
	void Stop();
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
} // namespace casioemu
