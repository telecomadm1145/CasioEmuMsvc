#include "TouchMouseTranslator.h"

#include <algorithm>
#include <cmath>

TouchMouseTranslator::TouchMouseTranslator(Uint32 windowId, EventSink sink)
	: windowId_(windowId), sink_(std::move(sink)) {
}

void TouchMouseTranslator::SetWindowId(Uint32 windowId) {
	windowId_ = windowId;
}

bool TouchMouseTranslator::HandleEvent(const SDL_Event& event, int windowW, int windowH) {
	switch (event.type) {
	case SDL_FINGERDOWN:
		return HandleFingerDown(event.tfinger, windowW, windowH);

	case SDL_FINGERUP:
		return HandleFingerUp(event.tfinger, windowW, windowH);

	case SDL_FINGERMOTION:
		return HandleFingerMotion(event.tfinger, windowW, windowH);

	default:
		return false;
	}
}

bool TouchMouseTranslator::HandleFingerDown(const SDL_TouchFingerEvent& finger, int windowW, int windowH) {
	const float x = finger.x * static_cast<float>(windowW);
	const float y = finger.y * static_cast<float>(windowH);

	if (!primary_.active) {
		StartFinger(primary_, finger.fingerId, x, y);
		return true;
	}

	if (!secondary_.active && finger.fingerId != primary_.fingerId) {
		if (primary_.dragging || leftButtonDown_) {
			EmitMouseButton(SDL_BUTTON_LEFT, SDL_RELEASED, primary_.currentX, primary_.currentY);
			primary_.dragging = false;
		}

		primary_.suppressTap = true;

		StartFinger(secondary_, finger.fingerId, x, y);
		secondary_.suppressTap = true;

		return true;
	}

	return true;
}

bool TouchMouseTranslator::HandleFingerUp(const SDL_TouchFingerEvent& finger, int windowW, int windowH) {
	const float x = finger.x * static_cast<float>(windowW);
	const float y = finger.y * static_cast<float>(windowH);
	const Uint32 now = SDL_GetTicks();

	if (primary_.active && primary_.fingerId == finger.fingerId) {
		primary_.currentX = x;
		primary_.currentY = y;

		if (primary_.dragging || leftButtonDown_) {
			EmitMouseButton(SDL_BUTTON_LEFT, SDL_RELEASED, x, y);
			primary_.dragging = false;
		}
		else {
			const float dx = x - primary_.startX;
			const float dy = y - primary_.startY;
			const float distSq = dx * dx + dy * dy;
			const float thresholdSq = dragThresholdPixels_ * dragThresholdPixels_;

			if (!primary_.suppressTap && distSq <= thresholdSq) {
				if (now - primary_.startTime < longPressDelayMs_) {
					EmitMouseClick(SDL_BUTTON_LEFT, x, y);
				}
				else {
					EmitMouseClick(SDL_BUTTON_RIGHT, x, y);
				}
			}
		}

		ResetFinger(primary_);

		if (secondary_.active) {
			PromoteSecondFingerToPrimary();
		}

		return true;
	}

	if (secondary_.active && secondary_.fingerId == finger.fingerId) {
		secondary_.currentX = x;
		secondary_.currentY = y;

		if (secondary_.dragging || leftButtonDown_) {
			EmitMouseButton(SDL_BUTTON_LEFT, SDL_RELEASED, x, y);
			secondary_.dragging = false;
		}

		ResetFinger(secondary_);
		primary_.suppressTap = true;

		return true;
	}

	return true;
}

bool TouchMouseTranslator::HandleFingerMotion(const SDL_TouchFingerEvent& finger, int windowW, int windowH) {
	const float x = finger.x * static_cast<float>(windowW);
	const float y = finger.y * static_cast<float>(windowH);

	if (primary_.active && primary_.fingerId == finger.fingerId) {
		if (!secondary_.active) {
			HandleSingleFingerMove(primary_, x, y);
		}
		else {
			primary_.suppressTap = true;
			secondary_.suppressTap = true;

			HandleTwoFingerMove(primary_, x, y, primary_.currentX, primary_.currentY);
		}

		primary_.currentX = x;
		primary_.currentY = y;

		AddTrail(primaryTrail_, x, y);
		return true;
	}

	if (secondary_.active && secondary_.fingerId == finger.fingerId) {
		if (primary_.active) {
			primary_.suppressTap = true;
			secondary_.suppressTap = true;

			HandleTwoFingerMove(secondary_, x, y, primary_.currentX, primary_.currentY);
		}

		secondary_.currentX = x;
		secondary_.currentY = y;

		AddTrail(secondaryTrail_, x, y);
		return true;
	}

	return true;
}

void TouchMouseTranslator::StartFinger(TouchState& state, SDL_FingerID fingerId, float x, float y) {
	state.active = true;
	state.dragging = false;
	state.suppressTap = false;

	state.fingerId = fingerId;

	state.startX = x;
	state.startY = y;
	state.currentX = x;
	state.currentY = y;

	state.startTime = SDL_GetTicks();
}

void TouchMouseTranslator::ResetFinger(TouchState& state) {
	state = TouchState{};
}

void TouchMouseTranslator::HandleSingleFingerMove(TouchState& state, float x, float y) {
	const float dx = x - state.startX;
	const float dy = y - state.startY;

	const float distSq = dx * dx + dy * dy;
	const float thresholdSq = dragThresholdPixels_ * dragThresholdPixels_;

	if (!state.dragging && distSq > thresholdSq) {
		EmitMouseMotion(x, y);
		EmitMouseButton(SDL_BUTTON_LEFT, SDL_PRESSED, x, y);
		state.dragging = true;
		state.suppressTap = true;
	}

	if (state.dragging) {
		EmitMouseMotion(x, y);
	}
}

void TouchMouseTranslator::HandleTwoFingerMove(TouchState& state, float x, float y, float anchorX, float anchorY) {
	const float moveY = y - state.currentY;

	if (std::abs(moveY) <= 1.0f) {
		return;
	}

	EmitMouseWheel(-moveY, anchorX, anchorY);
}

void TouchMouseTranslator::PromoteSecondFingerToPrimary() {
	primary_ = secondary_;

	primary_.startX = primary_.currentX;
	primary_.startY = primary_.currentY;
	primary_.startTime = SDL_GetTicks();

	primary_.dragging = false;
	primary_.suppressTap = true;

	ResetFinger(secondary_);
}

void TouchMouseTranslator::AddTrail(TouchTrail& trail, float x, float y) {
	trail.samples[trail.currentIndex] = TouchSample{x, y, SDL_GetTicks()};
	trail.currentIndex = (trail.currentIndex + 1) % TrailBufferSize;
	trail.count = std::min<std::size_t>(trail.count + 1, TrailBufferSize);
}

void TouchMouseTranslator::RenderDebug(SDL_Renderer* renderer) const {
	if (!renderer) {
		return;
	}

	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	DrawTrail(renderer, primaryTrail_);
	DrawTrail(renderer, secondaryTrail_);

	DrawCross(renderer, primary_, 255, 0, 0);
	DrawCross(renderer, secondary_, 0, 255, 0);
}

void TouchMouseTranslator::DrawTrail(SDL_Renderer* renderer, const TouchTrail& trail) const {
	const Uint32 now = SDL_GetTicks();

	for (std::size_t i = 0; i < trail.count; ++i) {
		const std::size_t idx =
			(trail.currentIndex + TrailBufferSize - 1 - i) % TrailBufferSize;

		const TouchSample& sample = trail.samples[idx];
		const Uint32 age = now - sample.time;

		if (age > trailDurationMs_) {
			continue;
		}

		const float t = static_cast<float>(age) / static_cast<float>(trailDurationMs_);
		const float radius = 5.0f + 50.0f * t;
		const Uint8 alpha = static_cast<Uint8>(120.0f * (1.0f - t));

		SDL_SetRenderDrawColor(renderer, 255, 255, 255, alpha);

		SDL_Rect rect{
			static_cast<int>(sample.x - radius * 0.5f),
			static_cast<int>(sample.y - radius * 0.5f),
			static_cast<int>(radius),
			static_cast<int>(radius)};

		SDL_RenderFillRect(renderer, &rect);
	}
}

void TouchMouseTranslator::DrawCross(
	SDL_Renderer* renderer,
	const TouchState& state,
	Uint8 r,
	Uint8 g,
	Uint8 b) const {
	if (!state.active) {
		return;
	}

	SDL_SetRenderDrawColor(renderer, r, g, b, 255);

	SDL_RenderDrawLine(
		renderer,
		static_cast<int>(state.currentX - 10),
		static_cast<int>(state.currentY),
		static_cast<int>(state.currentX + 10),
		static_cast<int>(state.currentY));

	SDL_RenderDrawLine(
		renderer,
		static_cast<int>(state.currentX),
		static_cast<int>(state.currentY - 10),
		static_cast<int>(state.currentX),
		static_cast<int>(state.currentY + 10));
}

void TouchMouseTranslator::EmitMouseMotion(float x, float y) {
	if (!sink_) {
		return;
	}

	SDL_Event event{};
	event.type = SDL_MOUSEMOTION;
	event.motion.timestamp = SDL_GetTicks();
	event.motion.windowID = windowId_;
	event.motion.which = SDL_TOUCH_MOUSEID;
	event.motion.state = leftButtonDown_ ? SDL_BUTTON_LMASK : 0;
	event.motion.x = static_cast<Sint32>(std::lround(x));
	event.motion.y = static_cast<Sint32>(std::lround(y));
	event.motion.xrel = 0;
	event.motion.yrel = 0;

	sink_(event);
}

void TouchMouseTranslator::EmitMouseButton(Uint8 button, Uint8 state, float x, float y) {
	if (!sink_) {
		return;
	}

	SDL_Event event{};
	event.type = state == SDL_PRESSED ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
	event.button.timestamp = SDL_GetTicks();
	event.button.windowID = windowId_;
	event.button.which = SDL_TOUCH_MOUSEID;
	event.button.button = button;
	event.button.state = state;
	event.button.clicks = 1;
	event.button.x = static_cast<Sint32>(std::lround(x));
	event.button.y = static_cast<Sint32>(std::lround(y));

	sink_(event);

	if (button == SDL_BUTTON_LEFT) {
		leftButtonDown_ = state == SDL_PRESSED;
	}
}

void TouchMouseTranslator::EmitMouseClick(Uint8 button, float x, float y) {
	EmitMouseMotion(x, y);
	EmitMouseButton(button, SDL_PRESSED, x, y);
	EmitMouseButton(button, SDL_RELEASED, x, y);
}

void TouchMouseTranslator::EmitMouseWheel(float deltaPixels, float mouseX, float mouseY) {
	if (!sink_) {
		return;
	}

	const float preciseY = deltaPixels / scrollPixelsPerWheel_;

	SDL_Event event{};
	event.type = SDL_MOUSEWHEEL;
	event.wheel.timestamp = SDL_GetTicks();
	event.wheel.windowID = windowId_;
	event.wheel.which = SDL_TOUCH_MOUSEID;
	event.wheel.x = 0;
	event.wheel.y = static_cast<Sint32>(std::lround(preciseY));
	event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;

#if SDL_VERSION_ATLEAST(2, 0, 18)
	event.wheel.preciseX = 0.0f;
	event.wheel.preciseY = preciseY;
#endif

#if SDL_VERSION_ATLEAST(2, 26, 0)
	event.wheel.mouseX = static_cast<Sint32>(std::lround(mouseX));
	event.wheel.mouseY = static_cast<Sint32>(std::lround(mouseY));
#endif

	sink_(event);
}