import re

content = open('CasioEmuMsvc/Peripheral/Keyboard.cpp').read()

# 1. Fix Memory Leak on interrupted timers. We need to store the `param` pointer in the Button struct so we can delete it.
# Instead of storing `param` in `Button`, since `param` just holds the `Keyboard*` and `button_index`, which are static,
# we can just use the Button index casted as `void*` or a static array of params. Wait, better yet,
# just delete the param when cancelling. But `SDL_RemoveTimer` doesn't give us back the `param`.
# To fix this simply without changing the struct, we can store `param` in `Button` struct.
# Wait, let's just make the timer callback receive the button_index cast to `void*` since `this` (Keyboard*) is not available.
# Actually, the callback needs `Keyboard*`. We can use a small array of structs inside Keyboard, or just store the `DelayedReleaseParam*` in `Button`.
# Let's add `void* releaseParam;` to `Button`? We can't modify the struct definition easily via regex without risking issues.
# Alternatively, since there are only 64 buttons, we can just declare `DelayedReleaseParam releaseParams[64];` in the `Keyboard` class.
# Let's just change `Button` struct: it's at the top.

# Read the file line by line to patch safely
lines = content.splitlines(True)
for i, line in enumerate(lines):
    if "SDL_TimerID releaseTimer;" in line:
        lines.insert(i+1, "\t\t\tstruct DelayedReleaseParam* releaseParam = nullptr;\n")
        break
content = "".join(lines)

# Now in PressButton, when cancelling timer:
content = content.replace(
'''		if (button.releaseTimer != 0) {
			SDL_RemoveTimer(button.releaseTimer);
			button.releaseTimer = 0;
		}''',
'''		if (button.releaseTimer != 0) {
			SDL_RemoveTimer(button.releaseTimer);
			button.releaseTimer = 0;
			if (button.releaseParam) {
				delete button.releaseParam;
				button.releaseParam = nullptr;
			}
		}'''
)

# In TryReleaseButton:
content = content.replace(
'''			auto* param = new DelayedReleaseParam{this, button_index};
			button.releaseTimer = SDL_AddTimer(25 - elapsed, DelayedReleaseCallback, param);''',
'''			auto* param = new DelayedReleaseParam{this, button_index};
			button.releaseParam = param;
			button.releaseTimer = SDL_AddTimer(25 - elapsed, DelayedReleaseCallback, param);'''
)

# In ExecuteDelayedRelease:
content = content.replace(
'''	void Keyboard::ExecuteDelayedRelease(size_t button_index) {
		if (button_index >= 64) return;
		auto& button = buttons[button_index];
		button.releaseTimer = 0; // timer fired''',
'''	void Keyboard::ExecuteDelayedRelease(size_t button_index) {
		if (button_index >= 64) return;
		auto& button = buttons[button_index];
		button.releaseTimer = 0; // timer fired
		button.releaseParam = nullptr;'''
)

# 2. Fix Shutdown Race Condition: Add destructor logic to Uninitialise
content = content.replace(
'''	void Keyboard::Uninitialise() {
	}''',
'''	void Keyboard::Uninitialise() {
		for (auto& button : buttons) {
			if (button.releaseTimer != 0) {
				SDL_RemoveTimer(button.releaseTimer);
				button.releaseTimer = 0;
				if (button.releaseParam) {
					delete button.releaseParam;
					button.releaseParam = nullptr;
				}
			}
		}
	}'''
)

# 3. In ExecuteDelayedRelease, remove the `if (button.type == Button::BT_BUTTON)` guard for recalculation
content = content.replace(
'''			bool state_changed = false;
			if (button.type == Button::BT_BUTTON) {
				state_changed = true;
			}
			if (state_changed) {
				if (real_hardware) {
					RecalculateGhost();
				}
				else {
					RecalculateEmuInput();
				}
			}''',
'''			if (real_hardware) {
				RecalculateGhost();
			}
			else {
				RecalculateEmuInput();
			}'''
)

open('CasioEmuMsvc/Peripheral/Keyboard.cpp', 'w').write(content)
