#pragma once

#include <SDL.h>
#include <cstddef>
#include <utility>
#include "ModelInfo.h"

namespace casioemu {
SDL_Texture* CreateSvgSpriteTexture(SDL_Renderer* renderer, const SpriteInfo& sprite, int width, int height, SDL_Rect* content_bounds);

class SvgSpriteTextureCache {
public:
	SvgSpriteTextureCache() = default;
	SvgSpriteTextureCache(const SvgSpriteTextureCache&) = delete;
	SvgSpriteTextureCache& operator=(const SvgSpriteTextureCache&) = delete;
	SvgSpriteTextureCache(SvgSpriteTextureCache&& other) noexcept;
	SvgSpriteTextureCache& operator=(SvgSpriteTextureCache&& other) noexcept;
	~SvgSpriteTextureCache();
	void Reset();
	SDL_Texture* Get(SDL_Renderer* renderer, const SpriteInfo& sprite, int requested_width, int requested_height);
	bool ContentClip(const SDL_Rect& dest, SDL_Rect& clip) const;
private:
	void MoveFrom(SvgSpriteTextureCache& other) noexcept;
	SDL_Texture* texture = nullptr;
	int width = 0;
	int height = 0;
	std::size_t shape_size = 0;
	std::size_t shape_hash = 0;
	SDL_Rect content_bounds{};
	bool software_renderer = false;
};

std::pair<int, int> CurrentRenderTargetSpriteSize(SDL_Renderer* renderer, const SDL_Rect& dest);
void RenderModelSprite(SDL_Renderer* renderer, SDL_Texture* interface_texture, SvgSpriteTextureCache* svg_texture, const SpriteInfo& sprite, const ColourInfo& ink_colour, uint8_t alpha);
#ifndef CASIOEMU_CORE_WEB
SDL_Color ScreenPixelColour(const ColourInfo& ink_colour, float alpha_value);
#endif
} // namespace casioemu
