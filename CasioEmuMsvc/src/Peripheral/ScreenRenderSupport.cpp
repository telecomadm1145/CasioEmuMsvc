#include "ScreenRenderSupport.hpp"

#include <SDL_image.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <string_view>

namespace casioemu {
SDL_Texture* CreateSvgSpriteTexture(SDL_Renderer* renderer, const SpriteInfo& sprite, int width, int height, SDL_Rect* content_bounds) {
	if (!renderer || sprite.svg_shape.empty() || width <= 0 || height <= 0)
		return nullptr;
	if (content_bounds)
		*content_bounds = {0, 0, width, height};
	SDL_RWops* rw = SDL_RWFromConstMem(sprite.svg_shape.data(), static_cast<int>(sprite.svg_shape.size()));
	if (!rw) {
		SDL_Log("[Screen][Warn] SDL_RWFromConstMem failed for SVG sprite: %s", SDL_GetError());
		return nullptr;
	}
	SDL_Surface* surface = IMG_LoadSizedSVG_RW(rw, width, height);
	SDL_RWclose(rw);
	if (!surface) {
		SDL_Log("[Screen][Warn] IMG_LoadSizedSVG_RW failed for SVG sprite: %s", IMG_GetError());
		return nullptr;
	}
	SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
	SDL_FreeSurface(surface);
	if (!converted) {
		SDL_Log("[Screen][Warn] SDL_ConvertSurfaceFormat failed for SVG sprite: %s", SDL_GetError());
		return nullptr;
	}
	if (content_bounds) {
		int min_x = converted->w;
		int min_y = converted->h;
		int max_x = -1;
		int max_y = -1;
		if (!SDL_MUSTLOCK(converted) || SDL_LockSurface(converted) == 0) {
			for (int y = 0; y < converted->h; ++y) {
				const auto* row = reinterpret_cast<const Uint32*>(
					static_cast<const Uint8*>(converted->pixels) + y * converted->pitch);
				for (int x = 0; x < converted->w; ++x) {
					Uint8 red = 0, green = 0, blue = 0, alpha = 0;
					SDL_GetRGBA(row[x], converted->format, &red, &green, &blue, &alpha);
					if (alpha == 0)
						continue;
					min_x = std::min(min_x, x);
					min_y = std::min(min_y, y);
					max_x = std::max(max_x, x);
					max_y = std::max(max_y, y);
				}
			}
			if (SDL_MUSTLOCK(converted))
				SDL_UnlockSurface(converted);
		}
		if (max_x >= min_x && max_y >= min_y)
			*content_bounds = {min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
	}
	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, converted);
	SDL_FreeSurface(converted);
	if (!texture) {
		SDL_Log("[Screen][Warn] SDL_CreateTextureFromSurface failed for SVG sprite: %s", SDL_GetError());
		return nullptr;
	}
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	return texture;
}

SvgSpriteTextureCache::SvgSpriteTextureCache(SvgSpriteTextureCache&& other) noexcept {
	MoveFrom(other);
}
SvgSpriteTextureCache& SvgSpriteTextureCache::operator=(SvgSpriteTextureCache&& other) noexcept {
	if (this != &other) {
		Reset();
		MoveFrom(other);
	}
	return *this;
}
SvgSpriteTextureCache::~SvgSpriteTextureCache() {
	Reset();
}
void SvgSpriteTextureCache::Reset() {
	if (texture) {
		SDL_DestroyTexture(texture);
		texture = nullptr;
	}
	width = 0;
	height = 0;
	shape_size = 0;
	shape_hash = 0;
	content_bounds = {};
	software_renderer = false;
}
SDL_Texture* SvgSpriteTextureCache::Get(SDL_Renderer* renderer, const SpriteInfo& sprite, int requested_width, int requested_height) {
	if (sprite.svg_shape.empty())
		return nullptr;
	const int target_width = std::max(1, requested_width);
	const int target_height = std::max(1, requested_height);
	const auto target_shape_size = sprite.svg_shape.size();
	const auto target_shape_hash = std::hash<std::string>{}(sprite.svg_shape);
	if (!texture || width != target_width || height != target_height ||
		shape_size != target_shape_size || shape_hash != target_shape_hash) {
		Reset();
		SDL_RendererInfo renderer_info{};
		software_renderer = SDL_GetRendererInfo(renderer, &renderer_info) == 0 &&
			renderer_info.name && std::string_view(renderer_info.name) == "software";
		texture = CreateSvgSpriteTexture(renderer, sprite, target_width, target_height,
			software_renderer ? &content_bounds : nullptr);
		software_renderer = texture && software_renderer;
		width = texture ? target_width : 0;
		height = texture ? target_height : 0;
		shape_size = texture ? target_shape_size : 0;
		shape_hash = texture ? target_shape_hash : 0;
	}
	return texture;
}
bool SvgSpriteTextureCache::ContentClip(const SDL_Rect& dest, SDL_Rect& clip) const {
	if (!software_renderer || width <= 0 || height <= 0 || content_bounds.w <= 0 || content_bounds.h <= 0)
		return false;
	const int left = static_cast<int>(std::floor(static_cast<double>(content_bounds.x) * dest.w / width));
	const int top = static_cast<int>(std::floor(static_cast<double>(content_bounds.y) * dest.h / height));
	const int right = static_cast<int>(std::ceil(static_cast<double>(content_bounds.x + content_bounds.w) * dest.w / width));
	const int bottom = static_cast<int>(std::ceil(static_cast<double>(content_bounds.y + content_bounds.h) * dest.h / height));
	clip = {dest.x + left - 1, dest.y + top - 1, std::max(1, right - left + 2), std::max(1, bottom - top + 2)};
	return true;
}
void SvgSpriteTextureCache::MoveFrom(SvgSpriteTextureCache& other) noexcept {
	texture = std::exchange(other.texture, nullptr);
	width = std::exchange(other.width, 0);
	height = std::exchange(other.height, 0);
	shape_size = std::exchange(other.shape_size, 0);
	shape_hash = std::exchange(other.shape_hash, 0);
	content_bounds = std::exchange(other.content_bounds, SDL_Rect{});
	software_renderer = std::exchange(other.software_renderer, false);
}

std::pair<int, int> CurrentRenderTargetSpriteSize(SDL_Renderer* renderer, const SDL_Rect& dest) {
	float scale_x = 1.0f;
	float scale_y = 1.0f;
	if (renderer)
		SDL_RenderGetScale(renderer, &scale_x, &scale_y);
	return {
		std::max(1, static_cast<int>(std::lround(std::abs(static_cast<double>(dest.w) * scale_x)))),
		std::max(1, static_cast<int>(std::lround(std::abs(static_cast<double>(dest.h) * scale_y))))};
}
void RenderModelSprite(SDL_Renderer* renderer, SDL_Texture* interface_texture, SvgSpriteTextureCache* svg_texture, const SpriteInfo& sprite, const ColourInfo& ink_colour, uint8_t alpha) {
	SDL_Rect dest = sprite.dest;
	SDL_Texture* texture = nullptr;
	if (svg_texture) {
		auto [target_width, target_height] = CurrentRenderTargetSpriteSize(renderer, dest);
		texture = svg_texture->Get(renderer, sprite, target_width, target_height);
	}
	if (texture) {
		SDL_Rect old_clip{};
		const SDL_bool old_clip_enabled = SDL_RenderIsClipEnabled(renderer);
		if (old_clip_enabled)
			SDL_RenderGetClipRect(renderer, &old_clip);
		SDL_Rect content_clip{};
		bool content_clip_enabled = svg_texture && svg_texture->ContentClip(dest, content_clip);
		if (content_clip_enabled && old_clip_enabled) {
			SDL_Rect intersection{};
			if (SDL_IntersectRect(&content_clip, &old_clip, &intersection))
				content_clip = intersection;
			else
				content_clip = {0, 0, 0, 0};
		}
		if (content_clip_enabled)
			SDL_RenderSetClipRect(renderer, &content_clip);
		SDL_SetTextureColorMod(texture, ink_colour.r, ink_colour.g, ink_colour.b);
		SDL_SetTextureAlphaMod(texture, alpha);
		SDL_RenderCopy(renderer, texture, nullptr, &dest);
		SDL_SetTextureAlphaMod(texture, 255);
		SDL_SetTextureColorMod(texture, 255, 255, 255);
		if (content_clip_enabled)
			SDL_RenderSetClipRect(renderer, old_clip_enabled ? &old_clip : nullptr);
		return;
	}
	if (!interface_texture)
		return;
	SDL_SetTextureAlphaMod(interface_texture, alpha);
	SDL_Rect src = sprite.src;
	SDL_RenderCopy(renderer, interface_texture, &src, &dest);
}
#ifndef CASIOEMU_CORE_WEB
SDL_Color ScreenPixelColour(const ColourInfo& ink_colour, float alpha_value) {
	SDL_Color colour{
		static_cast<Uint8>(ink_colour.r),
		static_cast<Uint8>(ink_colour.g),
		static_cast<Uint8>(ink_colour.b),
		Uint8(std::clamp(static_cast<int>(alpha_value), 0, 255))};
	if (alpha_value <= 0.0f) {
		colour.a = 0;
	} else if (alpha_value > 255.0f) {
		const int extra = static_cast<int>(alpha_value - 255.0f);
		colour.r = static_cast<uint8_t>(std::max(0, ink_colour.r - extra));
		colour.g = static_cast<uint8_t>(std::max(0, ink_colour.g - static_cast<int>(extra * 0.8f)));
		colour.b = static_cast<uint8_t>(std::max(0, ink_colour.b - static_cast<int>(extra * 0.1f)));
		colour.a = 255;
	}
	return colour;
}
#endif
} // namespace casioemu

