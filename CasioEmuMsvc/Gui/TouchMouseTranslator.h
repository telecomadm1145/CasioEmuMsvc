#pragma once

#include <SDL.h>
#include <array>
#include <cstddef>
#include <functional>

enum class TouchTarget {
	Emulator,
	ImGui
};

class TouchMouseTranslator {
public:
	using EventSink = std::function<void(const SDL_Event&, TouchTarget)>;
	using GuiHitTest = std::function<bool(float x, float y)>;

	TouchMouseTranslator(Uint32 windowId, EventSink sink, GuiHitTest guiHitTest);

	void SetWindowId(Uint32 windowId);

	/**
	 * 返回 true 表示该 touch 事件已消费。
	 */
	bool HandleEvent(const SDL_Event& event, int windowW, int windowH);

	void RenderDebug(SDL_Renderer* renderer) const;

private:
	struct TouchState {
		bool active = false;
		bool dragging = false;
		bool suppressTap = false;

		SDL_FingerID fingerId = 0;

		float startX = 0.0f;
		float startY = 0.0f;
		float currentX = 0.0f;
		float currentY = 0.0f;

		Uint32 startTime = 0;

		TouchTarget target = TouchTarget::Emulator;
	};

	struct TouchSample {
		float x = 0.0f;
		float y = 0.0f;
		Uint32 time = 0;
	};

	static constexpr std::size_t TrailBufferSize = 512;

	struct TouchTrail {
		std::array<TouchSample, TrailBufferSize> samples{};
		std::size_t currentIndex = 0;
		std::size_t count = 0;
	};

private:
	bool HandleFingerDown(const SDL_TouchFingerEvent& finger, int windowW, int windowH);
	bool HandleFingerUp(const SDL_TouchFingerEvent& finger, int windowW, int windowH);
	bool HandleFingerMotion(const SDL_TouchFingerEvent& finger, int windowW, int windowH);

	void StartFinger(TouchState& state, SDL_FingerID fingerId, float x, float y, TouchTarget target);
	void ResetFinger(TouchState& state);

	void HandleSingleFingerMove(TouchState& state, float x, float y);
	void HandleTwoFingerMove(TouchState& state, float x, float y, float anchorX, float anchorY);

	void PromoteSecondFingerToPrimary();

	void AddTrail(TouchTrail& trail, float x, float y);
	void DrawTrail(SDL_Renderer* renderer, const TouchTrail& trail) const;
	void DrawCross(SDL_Renderer* renderer, const TouchState& state, Uint8 r, Uint8 g, Uint8 b) const;

	void EmitMouseMotion(TouchTarget target, float x, float y);
	void EmitMouseButton(TouchTarget target, Uint8 button, Uint8 state, float x, float y);
	void EmitMouseClick(TouchTarget target, Uint8 button, float x, float y);
	void EmitMouseWheel(TouchTarget target, float deltaPixels, float mouseX, float mouseY);

private:
	Uint32 windowId_ = 0;
	EventSink sink_;
	GuiHitTest guiHitTest_;

	TouchState primary_;
	TouchState secondary_;

	TouchTrail primaryTrail_;
	TouchTrail secondaryTrail_;

	bool leftButtonDown_ = false;

	const float dragThresholdPixels_ = 10.0f;
	const Uint32 longPressDelayMs_ = 500;
	const Uint32 trailDurationMs_ = 500;
	const float scrollPixelsPerWheel_ = 20.0f;
};