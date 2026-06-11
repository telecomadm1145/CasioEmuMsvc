#include "Keyboard.hpp"
#include <SDL.h>

#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Emulator.hpp"
#include "Logger.hpp"
#include "ModelInfo.h"
#include "ePSCpu.h"
#include "vibration.h"
#include <ML620Ports.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <unordered_map>

namespace casioemu {
	class Keyboard : public Peripheral, public IKeyboardAutomation {
		MMURegion region_ko_mask, region_ko, region_ki, region_input_mode, region_input_filter;
		uint16_t keyboard_out, keyboard_out_mask;
		uint8_t keyboard_in, input_mode, input_filter, keyboard_ghost[8], ki_ghost[8];
		uint8_t keyboard_in_override{};
		uint8_t keyboard_in_last, input_filter_last;

		bool real_hardware;
		MMURegion region_ready_emu, region_ko_emu, region_ki_emu, region_pd_emu;
		uint8_t keyboard_ready_emu, keyboard_out_emu, keyboard_in_emu, keyboard_pd_emu;

		int emu_ki_readcount, emu_ko_readcount;

		uint8_t has_input;
		size_t EXI0INT = 0;

		SDL_Renderer* renderer;

		struct Button {
			enum ButtonType {
				BT_NONE,
				BT_BUTTON,
				BT_POWER
			} type;
			SDL_Rect rect;
			uint8_t ko_bit, ki_bit;
			uint8_t code;
			bool pressed, stuck;
			uint32_t pressTime;
			SDL_TimerID releaseTimer;
			struct DelayedReleaseParam* releaseParam = nullptr;
			SDL_FingerID pressingFingerId; // ID of the finger currently pressing this button
		} buttons[64];

		// Maps from keycode to an index to (buttons).
		std::unordered_map<SDL_Keycode, size_t> keyboard_map;

		bool p0, p1, p146;

	public:
		using Peripheral::Peripheral;

		bool factory_test = false;

		void Initialise();
		void Reset();
		void Tick();
		void Frame();
		void UIEvent(SDL_Event& event);
		void Uninitialise();
		void PressButton(Button& button, bool stick, SDL_FingerID fingerId);
		void PressAt(int x, int y, bool stick, SDL_FingerID fingerId);
		void ReleaseAt(int x, int y, SDL_FingerID fingerId);
		void PressButtonByCode(uint8_t code);
		bool TryReleaseButton(Button& button);
		void ExecuteDelayedRelease(size_t button_index);
		void StartInject();
		void StoreKeyLog();
		void ReleaseAll();
		void RecalculateKI();
		void RecalculateGhost();
		void RecalculateEmuInput(); // Declaration for new function
		bool AnyFingerPressing();	// Declaration for new function
		void* QueryInterface(const char* name) override {
			if (strcmp(name, typeid(IKeyboardAutomation).name()) == 0) {
				return (IKeyboardAutomation*)this;
			}
			return 0;
		}
		void SaveState(std::ostream& os) override {
			os.write(reinterpret_cast<const char*>(&keyboard_out), sizeof(keyboard_out));
			os.write(reinterpret_cast<const char*>(&keyboard_out_mask), sizeof(keyboard_out_mask));
			os.write(reinterpret_cast<const char*>(&keyboard_in), sizeof(keyboard_in));
			os.write(reinterpret_cast<const char*>(&input_mode), sizeof(input_mode));
			os.write(reinterpret_cast<const char*>(&input_filter), sizeof(input_filter));
			os.write(reinterpret_cast<const char*>(&keyboard_in_emu), sizeof(keyboard_in_emu));
			os.write(reinterpret_cast<const char*>(&keyboard_out_emu), sizeof(keyboard_out_emu));
			os.write(reinterpret_cast<const char*>(&keyboard_ready_emu), sizeof(keyboard_ready_emu));
		}
		void LoadState(std::istream& is) override {
			is.read(reinterpret_cast<char*>(&keyboard_out), sizeof(keyboard_out));
			is.read(reinterpret_cast<char*>(&keyboard_out_mask), sizeof(keyboard_out_mask));
			is.read(reinterpret_cast<char*>(&keyboard_in), sizeof(keyboard_in));
			is.read(reinterpret_cast<char*>(&input_mode), sizeof(input_mode));
			is.read(reinterpret_cast<char*>(&input_filter), sizeof(input_filter));
			is.read(reinterpret_cast<char*>(&keyboard_in_emu), sizeof(keyboard_in_emu));
			is.read(reinterpret_cast<char*>(&keyboard_out_emu), sizeof(keyboard_out_emu));
			is.read(reinterpret_cast<char*>(&keyboard_ready_emu), sizeof(keyboard_ready_emu));
		}

		// 通过 IKeyboardAutomation 继承
		void Key(int ki, int ko, bool pressed) override {
			uint8_t ki_bit = (uint8_t)(1 << ki);
			uint8_t ko_bit = (uint8_t)(1 << ko);
			for (auto& button : buttons) {
				if (button.type == Button::BT_BUTTON &&
					button.ki_bit == ki_bit && button.ko_bit == ko_bit) {
					if (pressed) {
						PressButton(button, false, -1);
					} else {
						if (TryReleaseButton(button)) {
							if (real_hardware) RecalculateGhost();
							else RecalculateEmuInput();
						}
					}
					return;
				}
			}
		}
		void PressCode(uint8_t code, bool pressed) override {
			if (pressed) {
				PressButtonByCode(code);
			} else {
				int button_index;
				if (code == 0xFF) button_index = 63;
				else button_index = ((code >> 1) & 0x38) | (code & 0x07);
				if (button_index < 64) {
					auto& button = buttons[button_index];
					if (TryReleaseButton(button)) {
						if (real_hardware) RecalculateGhost();
						else RecalculateEmuInput();
					}
				}
			}
		}
	};
	struct DelayedReleaseParam {
		Keyboard* keyboard;
		size_t button_index;
	};

	Uint32 DelayedReleaseCallback(Uint32 interval, void* param) {
		auto* p = static_cast<DelayedReleaseParam*>(param);
		p->keyboard->ExecuteDelayedRelease(p->button_index);
		delete p;
		return 0; // Don't run again
	}

	void Keyboard::ExecuteDelayedRelease(size_t button_index) {
		if (button_index >= 64) return;
		auto& button = buttons[button_index];
		button.releaseTimer = 0; // timer fired
		button.releaseParam = nullptr;
		if (button.pressed && !button.stuck) {
			button.pressed = false;
			button.pressingFingerId = -1;
			if (real_hardware) {
				RecalculateGhost();
			}
			else {
				RecalculateEmuInput();
			}
		}
	}

	bool Keyboard::TryReleaseButton(Button& button) {
		if (!button.pressed || button.stuck) return false;

		uint32_t now = SDL_GetTicks();
		uint32_t elapsed = now - button.pressTime;
		if (elapsed < 25) {
			if (button.releaseTimer != 0) {
				return false; // Timer already running
			}
			size_t button_index = &button - buttons;
			auto* param = new DelayedReleaseParam{this, button_index};
			button.releaseParam = param;
			button.releaseTimer = SDL_AddTimer(25 - elapsed, DelayedReleaseCallback, param);
			return false; // Not released yet
		}

		// Immediately release
		button.pressed = false;
		button.pressingFingerId = -1;
		return true; // Indicates it was immediately released
	}

	void Keyboard::Initialise() {
		renderer = emulator.GetRenderer();

		for (auto& button : buttons) {
			button.pressingFingerId = -1; // Initialize finger ID
		}

		/*
		 * When real_hardware is false, the program should emulate the behavior of the
		 * calculator emulator provided by Casio, which has different keyboard input
		 * interface.
		 */
		real_hardware = emulator.ModelDefinition.real_hardware;

		clock_type = CLOCK_UNDEFINED;
		if (emulator.hardware_id == HW_TI) {
			auto pp = emulator.chipset.QueryInterface<IPortProvider>();
			if (!pp)
				return;
			pp->SetPortOutputCallback(3, [&](uint8_t new_output) {
				keyboard_out = new_output;
				RecalculateKI();
			});
			pp->SetPortInput(0, 0, 0x20);
			pp->SetPortInput(4, 0, 0xff);
			goto init_kbd;
		}
		if (emulator.hardware_id == HW_EPS6800) {
			// TODO!
			emulator.chipset.epscpu->portacalc = [this]() {
				this->RecalculateKI();
			};
			goto init_kbd;
		}

		region_ki.Setup(0xF040, 1, "Keyboard/KI", this,
			[](MMURegion* region, size_t) {
				return ((Keyboard*)region->userdata)->keyboard_in;
			},
			[](MMURegion* region, size_t, uint8_t data) {
#ifdef CASIOEMU_ENABLE_KEYBOARD_KI_WRITE
				Keyboard* keyboard = ((Keyboard*)region->userdata);
				keyboard->keyboard_in_override = data;
				keyboard->RecalculateKI();
#endif
			},
			emulator);

		region_input_mode.Setup(
			0xF041, 1, "Keyboard/InputMode", this, [](MMURegion* region, size_t) {
			Keyboard *keyboard = ((Keyboard *)region->userdata);
			return (uint8_t)keyboard->input_mode; }, [](MMURegion* region, size_t, uint8_t data) {
			Keyboard *keyboard = ((Keyboard *)region->userdata);
			keyboard->input_mode = data;
			keyboard->RecalculateKI(); }, emulator);

		region_input_filter.Setup(0xF042, 1, "Keyboard/InputFilter", &input_filter, MMURegion::DefaultRead<uint8_t>, MMURegion::DefaultWrite<uint8_t>, emulator);
		if (emulator.hardware_id == HW_FX_5800P || emulator.ModelDefinition.legacy_ko) {
			region_ko.Setup(
				0xF044, 1, "Keyboard/KO", this,
				[](MMURegion* region, size_t offset) {
					offset -= region->base;
					Keyboard* keyboard = ((Keyboard*)region->userdata);
					return (uint8_t)(keyboard->keyboard_out ^ 0xFF);
				},
				[](MMURegion* region, size_t offset, uint8_t data) {
					Keyboard* keyboard = ((Keyboard*)region->userdata);
					keyboard->keyboard_out = 0xFF ^ data;
					keyboard->RecalculateKI();
				},
				emulator);
		}
		else {
			region_ko_mask.Setup(
				0xF044, 2, "Keyboard/KOMask", this,
				[](MMURegion* region, size_t offset) {
					offset -= region->base;
					Keyboard* keyboard = ((Keyboard*)region->userdata);
					return (uint8_t)((keyboard->keyboard_out_mask & 0x03FF) >> (offset * 8));
				},
				[](MMURegion* region, size_t offset, uint8_t data) {
					offset -= region->base;
					Keyboard* keyboard = ((Keyboard*)region->userdata);
					keyboard->keyboard_out_mask &= ~(((uint16_t)0xFF) << (offset * 8));
					keyboard->keyboard_out_mask |= ((uint16_t)data) << (offset * 8);
					keyboard->keyboard_out_mask &= 0x03FF;
					if (!offset)
						keyboard->RecalculateKI();
				},
				emulator);

			region_ko.Setup(
				0xF046, 2, "Keyboard/KO", this,
				[](MMURegion* region, size_t offset) {
					offset -= region->base;
					Keyboard* keyboard = ((Keyboard*)region->userdata);
					return (uint8_t)((keyboard->keyboard_out & 0x83FF) >> (offset * 8));
				},
				[](MMURegion* region, size_t offset, uint8_t data) {
					offset -= region->base;
					Keyboard* keyboard = ((Keyboard*)region->userdata);
					keyboard->keyboard_out &= ~(((uint16_t)0xFF) << (offset * 8));
					keyboard->keyboard_out |= ((uint16_t)data) << (offset * 8);
					keyboard->keyboard_out &= 0x83FF;
					if (!offset)
						keyboard->RecalculateKI();
				},
				emulator);
		}
		if (!real_hardware) {
			keyboard_pd_emu = emulator.ModelDefinition.pd_value;
			keyboard_ready_emu = 1;
			emu_ki_readcount = 0;
			emu_ko_readcount = 0;
			int offset = emulator.hardware_id == HW_ES_PLUS ? 0 : emulator.hardware_id == HW_CLASSWIZ ? 0x40000
																									  : 0x80000;
			size_t rse = 0x8E00;
			size_t ki = 0x8E01;
			size_t ko = 0x8E02;
			if (emulator.ModelDefinition.is_sample_rom) {
				rse += 7;
				ki += 4;
				ko += 6;
			}
			region_ready_emu.Setup(
				offset + rse, 1, "Keyboard/ReadyStatusEmulator", this,
				[](MMURegion* region, size_t offset) {
					Keyboard* keyboard = (Keyboard*)region->userdata;
					if (keyboard->keyboard_ready_emu == 8 || keyboard->keyboard_ready_emu == 2) {
						uint8_t emu_ki = keyboard->keyboard_in_emu;
						uint8_t emu_ko = keyboard->keyboard_out_emu;
						// if (++keyboard->emu_ki_readcount > 1)
						//	keyboard->keyboard_in_emu = 0;
						// if (++keyboard->emu_ko_readcount > 1)
						//	keyboard->keyboard_out_emu = 0;
						if (emu_ki == 4 && emu_ko == 16)
							return (uint8_t)1;
						else
							return (uint8_t)0;
					}
					return keyboard->keyboard_ready_emu;
				},
				[](MMURegion* region, size_t offset, uint8_t data) {
					((Keyboard*)region->userdata)->keyboard_ready_emu = data;
				},
				emulator);
			region_ki_emu.Setup(
				offset + ki, 1, "Keyboard/KIEmulator", this,
				[](MMURegion* region, size_t offset) {
					Keyboard* keyboard = ((Keyboard*)region->userdata);
					keyboard->emu_ki_readcount++;
					uint8_t value = keyboard->keyboard_in_emu;
					if (keyboard->emu_ki_readcount > 1)
						keyboard->keyboard_in_emu = 0;
					return value;
				},
				MMURegion::IgnoreWrite, emulator);
			region_ko_emu.Setup(
				offset + ko, 1, "Keyboard/KOEmulator", this,
				[](MMURegion* region, size_t offset) {
					Keyboard* keyboard = ((Keyboard*)region->userdata);
					keyboard->emu_ko_readcount++;
					uint8_t value = keyboard->keyboard_out_emu;
					if (keyboard->emu_ko_readcount > 1)
						keyboard->keyboard_out_emu = 0;
					return value;
				},
				MMURegion::IgnoreWrite, emulator);
		}
		if (emulator.hardware_id == HW_CLASSWIZ_II) {
			region_pd_emu.Setup(0xF058, 1, "Keyboard/PdValue", &emulator.ModelDefinition.pd_value, MMURegion::DefaultRead<uint8_t>, MMURegion::IgnoreWrite, emulator);
		}
		else if (emulator.hardware_id == HW_ES_PLUS || emulator.hardware_id == HW_CLASSWIZ) {
			region_pd_emu.Setup(0xF050, 1, "Keyboard/PdValue", &emulator.ModelDefinition.pd_value, MMURegion::DefaultRead<uint8_t>, MMURegion::IgnoreWrite, emulator);
		}

	init_kbd: {
		for (auto& button : buttons)
			button = {};

		for (auto& btn : emulator.ModelDefinition.buttons) {
			uint8_t code = btn.kiko;
			size_t button_ix;
			if (code == 0xFF) {
				button_ix = 63;
			}
			else {
				if (emulator.hardware_id == HW_TI) {
					button_ix = btn.kiko;
				}
				else {
					button_ix = ((code >> 1) & 0x38) | (code & 0x07);
				}
				if (button_ix >= 64)
					PANIC("button index doesn't fit 6 bits\n");
			}
#ifndef CASIOEMU_CORE_WEB
			auto button_name = btn.keyname.c_str();

			SDL_Keycode button_key;
			button_key = SDL_GetKeyFromName(button_name);
			if (button_key == SDLK_UNKNOWN)
				printf("[Keyboard][Warn] Key %x is being bind to a invalid or empty key '%s'\n", btn.kiko, button_name);

			if (button_key != SDLK_UNKNOWN) {
				bool insert_success = keyboard_map.emplace(button_key, button_ix).second;
				if (!insert_success)
					printf("[Keyboard][Warn] Key '%s' is used twice for key %x\n", button_name, btn.kiko);
			}
			std::string bn2;
			if (btn.keyname.starts_with("Keypad ")) {
				if (btn.keyname == "Keypad Enter") {
					bn2 = "Return";
				}
				else {
					bn2 = btn.keyname.substr(7);
				}
			}
			else {
				if (btn.keyname == "Return") {
					bn2 = "Keypad Enter";
				}
				else if (btn.keyname == "enter") {
					bn2 = "Return";
				}
				else if (btn.keyname == "Backspace") {
					bn2 = "Delete";
				}
				else if (btn.keyname == "Delete") {
					bn2 = "Backspace";
				}
				else {
					bn2 = "Keypad " + btn.keyname;
				}
			}
			auto button_key_2 = SDL_GetKeyFromName(bn2.c_str());
			if (button_key_2 != SDLK_UNKNOWN) {
				bool insert_success = keyboard_map.emplace(button_key_2, button_ix).second;
			}
#endif
			Button& button = buttons[button_ix];
			button = {};

			if (code == 0xFF)
				button.type = Button::BT_POWER;
			else
				button.type = Button::BT_BUTTON;
			button.rect = btn.rect;
			if (emulator.hardware_id == HW_TI) {
				int kimap[] = {7, 0, 1, 2, 3, 4, 5, 6};
				auto ki = kimap[btn.kiko & 7];
				auto ko = (btn.kiko >> 3);
				if (ki == 7 && ko > 0) {
					ko -= 1;
				}
				button.ki_bit = 1 << ki;
				button.ko_bit = 1 << ko;
				button.code = btn.kiko;
			}
			else {
				button.ko_bit = 1 << ((code >> 4) & 0xF);
				button.ki_bit = 1 << (code & 0xF);
			}
		}
	}
	}

	void Keyboard::Reset() {
		p0 = false;
		p1 = false;
		p146 = false;
		input_mode = 0;
		keyboard_out = 0;
		keyboard_out_mask = 0;
		keyboard_in_last = 0xFF;
		input_filter_last = 0;

		if (!real_hardware) {
			keyboard_in_emu = 0;
			emu_ki_readcount = 0;
			keyboard_out_emu = 0;
			emu_ko_readcount = 0;
		}

		RecalculateGhost();
	}

	void Keyboard::Tick() {
		if (emulator.ModelDefinition.hardware_id == HW_TI) {
			return;
		}
		if (emulator.ModelDefinition.hardware_id == HW_EPS6800) {
			return;
		}
		if (factory_test) {
			keyboard_in = (uint8_t)~0b00011000; // KI 3 KI 4 enabled xD
			return;
		}
		if (keyboard_in_override) {
			keyboard_in = keyboard_in_override;
			return;
		}
		if (!real_hardware) {
			if (keyboard_ready_emu > 1)
				emulator.chipset.MaskableInterrupts[EXI0INT].TryRaise();
			return;
		}
		switch (emulator.chipset.data_EXICON & 0x03) {
		case 0:
			input_filter_last &= input_filter;
			if (keyboard_in_last & input_filter_last & ~(keyboard_in & input_filter_last))
				emulator.chipset.MaskableInterrupts[EXI0INT].TryRaise();
			break;
		case 1:
			input_filter_last &= input_filter;
			if (keyboard_in & input_filter_last & ~(keyboard_in_last & input_filter_last))
				emulator.chipset.MaskableInterrupts[EXI0INT].TryRaise();
			break;
		case 2:
			if (input_filter & keyboard_in)
				emulator.chipset.MaskableInterrupts[EXI0INT].TryRaise();
			break;
		case 3:
			if (input_filter & ~keyboard_in)
				emulator.chipset.MaskableInterrupts[EXI0INT].TryRaise();
			break;
		default:
			break;
		}
		input_filter_last = input_filter;
		keyboard_in_last = keyboard_in;
	}

	void Keyboard::Frame() {
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		for (auto& button : buttons) {
			if (button.type != Button::BT_NONE && button.pressed) {
				if (button.stuck)
					SDL_SetRenderDrawColor(renderer, 127, 0, 0, 127);
				else
					SDL_SetRenderDrawColor(renderer, 0, 0, 0, 127);
				SDL_RenderFillRect(renderer, &button.rect);
			}
			else {
				SDL_SetRenderDrawColor(renderer, 0, 0, 0, 40);
				SDL_RenderFillRect(renderer, &button.rect);
			}
		}
	}

	void Keyboard::UIEvent(SDL_Event& event) {
		switch (event.type) {
		case SDL_FINGERDOWN: {
			// SDL_Log("Finger down: ID %lld at %f, %f", event.tfinger.fingerId, event.tfinger.x, event.tfinger.y);
			int window_w, window_h;
			SDL_GetWindowSize(emulator.window, &window_w, &window_h);
			PressAt(event.tfinger.x * window_w, event.tfinger.y * window_h, false, event.tfinger.fingerId);
		} break;
		case SDL_FINGERUP: {
			// SDL_Log("Finger up: ID %lld at %f, %f", event.tfinger.fingerId, event.tfinger.x, event.tfinger.y);
			int window_w, window_h;
			SDL_GetWindowSize(emulator.window, &window_w, &window_h);
			ReleaseAt(event.tfinger.x * window_w, event.tfinger.y * window_h, event.tfinger.fingerId);
		} break;
		case SDL_MOUSEBUTTONDOWN:
			// Mouse clicks do not carry finger ID, treat as new press or stick
			// The PressAt/PressButton will handle if a button is already pressed by a finger
			if (event.button.button == SDL_BUTTON_LEFT) {
				PressAt(event.button.x, event.button.y, false, -1);
			}
			else if (event.button.button == SDL_BUTTON_RIGHT) {
				PressAt(event.button.x, event.button.y, true, -1);
			}
			break;
		case SDL_MOUSEBUTTONUP:
			// Mouse up for left button might still call ReleaseAll if that's desired for mouse interaction
			// However, this could conflict if a finger is still holding a button.
			// For now, let mouse up release only if NOT pressed by a finger.
			// This needs careful thought based on desired interaction for mixed mouse/touch.
			// A simpler approach for now: MOUSEUP releases a button only if no finger is on it.
			// Or, maintain current ReleaseAll for mouse for simplicity, and focus on finger distinctness.
			if (event.button.button == SDL_BUTTON_LEFT) {
				// Option 1: Try to release at cursor, but only if not finger-pressed
				// (This is complex as ReleaseAt expects a fingerId or needs modification)
				// Option 2: Keep ReleaseAll for mouse, assuming mouse is a global override.
				// For now, let's stick to ReleaseAll for mouse left up, and ensure finger releases are specific.
				// This means a mouse click could still clear finger presses.
				// This is a point of potential conflict to revisit if mixed input is common.
				bool button_released_by_mouse = false;
				for (auto& button : buttons) {
					if (button.rect.x <= event.button.x && button.rect.y <= event.button.y &&
						button.rect.x + button.rect.w > event.button.x && button.rect.y + button.rect.h > event.button.y) {
						if (button.pressed && button.pressingFingerId == -1 && !button.stuck) { // Only if pressed by mouse (no fingerId)
							if (TryReleaseButton(button)) {
								button_released_by_mouse = true;
							}
							// Recalculate after this loop if changes were made
						}
						break;
					}
				}
				if (button_released_by_mouse) {
					if (real_hardware)
						RecalculateGhost();
					else
						RecalculateEmuInput(); // New function to update emu input state
				}
				else if (!AnyFingerPressing()) {
					// If no fingers are pressing anything, and a mouse up occurs,
					// it might be a general release action.
					// This is debatable. For now, to minimize impact, only ReleaseAll if no fingers are active.
					// ReleaseAll(); // Original behavior - potentially problematic with multi-touch
				}
			}
			// Right mouse up does nothing to button.pressed state typically
			break;

		case SDL_KEYDOWN:
		case SDL_KEYUP:
			SDL_Keycode keycode = event.key.keysym.sym;
			auto iterator = keyboard_map.find(keycode);
			// printf("[Keyboard][Info] SDL_Keycode: %x(%s)\n", keycode, SDL_GetKeyName(keycode));
			if (event.key.keysym.sym == SDLK_F12 && event.key.state == SDL_PRESSED) {
				if (event.key.keysym.mod & KMOD_CTRL) {
					return;
				}
				// Trigger screenshot when F12 is pressed
				emulator.screenshot_requested.store(true);
				printf("[Keyboard][Info] Screenshot requested via F12\n");
				return;
			}
			if (event.key.keysym.sym == SDLK_F11 && event.key.state) {
				if (event.key.keysym.mod & KMOD_LCTRL) {
					emulator.chipset.Reset();
					return;
				}
				factory_test = !factory_test;
				emulator.chipset.tiDiagMode = factory_test;
				emulator.chipset.tiKey = 0xfe;
				printf("Factory test/Ti Diag status: %d\n", factory_test);
				return;
			}
			if (iterator == keyboard_map.end())
				break;
			if (event.key.state == SDL_PRESSED)
				PressButton(buttons[iterator->second], false, iterator->second);
			else
				ReleaseAll();
			break;
		}
	}

	void Keyboard::Uninitialise() {
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
	}

	bool Keyboard::AnyFingerPressing() {
		for (const auto& button : buttons) {
			if (button.pressed && button.pressingFingerId != -1) {
				return true;
			}
		}
		return false;
	}

	void Keyboard::RecalculateEmuInput() {
		if (real_hardware)
			return; // This function is only for emulated input path

		uint8_t current_keyboard_in_emu = 0;
		uint8_t current_keyboard_out_emu = 0;
		bool first_pressed_button_found = false;

		for (const auto& button : buttons) {
			if (button.type == Button::BT_BUTTON && button.pressed) {
				current_keyboard_in_emu |= button.ki_bit; // OR all pressed KI bits together

				if (!first_pressed_button_found) {
					// For KO, the original emulator behavior was "Report an arbitrary pressed key."
					// This implies only one KO line might be considered active for the emulated CPU.
					// If multiple KO lines can truly be active and sensed by the CPU, this should be ORed.
					// Sticking to one KO line active based on the first found pressed button:
					current_keyboard_out_emu = button.ko_bit;
					first_pressed_button_found = true;
				}
			}
		}

		if (first_pressed_button_found) {
			keyboard_in_emu = current_keyboard_in_emu;
			keyboard_out_emu = current_keyboard_out_emu;
			emu_ki_readcount = emu_ko_readcount = 0;
			if (EXI0INT < emulator.chipset.EffectiveMICount) {
				emulator.chipset.MaskableInterrupts[EXI0INT].TryRaise();
			}
		}
		else {
			keyboard_in_emu = 0;
			keyboard_out_emu = 0;
		}
		// Update has_input based on whether any button is pressed (for any input type)
		has_input = (keyboard_in_emu != 0);
	}

	void Keyboard::PressButton(Button& button, bool stick, SDL_FingerID fingerId) {
		button.pressTime = SDL_GetTicks();
		if (button.releaseTimer != 0) {
			SDL_RemoveTimer(button.releaseTimer);
			button.releaseTimer = 0;
			if (button.releaseParam) {
				delete button.releaseParam;
				button.releaseParam = nullptr;
			}
		}
		// SDL_Log("PressButton: code %02X, stick %d, fingerId %lld, current button fingerId %lld", button.code, stick, fingerId, button.pressingFingerId);

		// Prevent a button from being simultaneously claimed by two different fingers.
		if (fingerId != -1 && button.pressed && button.pressingFingerId != -1 && button.pressingFingerId != fingerId) {
			// SDL_Log("Button %02X already pressed by finger %lld, ignoring press from finger %lld", button.code, button.pressingFingerId, fingerId);
			return;
		}
		// If same finger presses again, and it's not a stick operation, it's likely a redundant event.
		if (fingerId != -1 && button.pressed && button.pressingFingerId == fingerId && !stick && !button.stuck) {
			// SDL_Log("Button %02X already pressed by same finger %lld, not sticking.",button.code, fingerId);
			return;
		}

		bool old_pressed_state = button.pressed;
		SDL_FingerID old_finger_id = button.pressingFingerId; // Could be -1 if not finger-pressed

		button.pressed = true; // Assume it will be pressed by this action

		if (stick) {
			// If this finger is already pressing this button and it's stuck, then this stick press unsticks it.
			if (button.stuck && button.pressingFingerId == fingerId && fingerId != -1) {
				button.stuck = false;
				button.pressed = false; // Unsticking also unpresses it from this finger's action
				button.pressingFingerId = -1;
			}
			// If trying to stick with mouse/key (fingerId == -1) and it's stuck (possibly by a finger), unstick it.
			else if (button.stuck && fingerId == -1) {
				button.stuck = false;
				button.pressed = false;
				button.pressingFingerId = -1;
			}
			// Otherwise, if it's not stuck, or stuck by a *different* finger (which this press shouldn't override for stuck state),
			// this action makes it (or keeps it) stuck with the current fingerId (or generic if fingerId is -1).
			else if (!button.stuck) {
				button.stuck = true;
				button.pressed = true;				// Sticking implies pressing
				button.pressingFingerId = fingerId; // Can be -1 for mouse/key stick
			}
			else {
				// It's already stuck, but by a different finger. This new press (stick or not)
				// will now also be associated with this fingerId if it's a finger.
				// The button remains stuck.
				if (fingerId != -1)
					button.pressingFingerId = fingerId;
				// if fingerId is -1 (mouse/key), and it's already stuck by a finger, the mouse/key press is also on it.
				// No change to stuck state if already stuck. pressingFingerId might change to this new finger.
			}
		}
		else {									// Not a stick press
			button.stuck = false;				// A normal press always unsticks the button.
			button.pressingFingerId = fingerId; // Assign current finger, or -1 for mouse/key
		}

		if (button.type == Button::BT_POWER && button.pressed && !old_pressed_state) {
			if (!(emulator.hardware_id == HW_CLASSWIZ && (emulator.chipset.data_FCON & 0x03) == 0x03)) {
				emulator.chipset.Reset();
			}
			else {
				// printf("[Keyboard][Info] RESETB is BLOCKED.Press Ctrl+F11 to reset.\n");
			}
		}
		if (button.type == Button::BT_BUTTON) {
			if (emulator.hardware_id == HW_TI) {
				emulator.chipset.tiKey = button.code;
			}
			// printf("[Keyboard][Info] KI: %d, KO: %d for button %02X\n", (int)(log(button.ki_bit) / log(2)), (int)(log(button.ko_bit) / log(2)), button.code);
		}

		bool state_effectively_changed = (old_pressed_state != button.pressed) || (button.pressed && old_finger_id != button.pressingFingerId);

		if (button.type == Button::BT_BUTTON && state_effectively_changed) {
			if (button.pressed) { // Vibrate only if it results in a pressed state
				Vibration::vibrate(100);
			}
			if (real_hardware) {
				RecalculateGhost(); // This internally calls RecalculateKI
			}
			else {
				RecalculateEmuInput();
			}
		}
	}

	void Keyboard::PressAt(int x, int y, bool stick, SDL_FingerID fingerId) {
		// SDL_Log("PressAt: x %d, y %d, stick %d, fingerId %lld", x, y, stick, fingerId);
		for (auto& button : buttons) {
			if (button.rect.x <= x && button.rect.y <= y && button.rect.x + button.rect.w > x && button.rect.y + button.rect.h > y) {
				PressButton(button, stick, fingerId);
				return; // Process only the first button found at coordinates
			}
		}
	}

	void Keyboard::ReleaseAt(int x, int y, SDL_FingerID fingerId) {
		// SDL_Log("ReleaseAt: x %d, y %d, fingerId %lld", x, y, fingerId);
		bool button_effectively_released = false;
		for (auto& button : buttons) {
			if (button.rect.x <= x && button.rect.y <= y && button.rect.x + button.rect.w > x && button.rect.y + button.rect.h > y) {
				// To release, the button must be currently pressed by THIS finger and NOT be stuck.
				// If it's stuck, a finger_up event for the finger that stuck it does NOT release it.
				// It would require a subsequent "stick" press on it to toggle the stuck state.
				if (button.pressed && button.pressingFingerId == fingerId && !button.stuck) {
					if (TryReleaseButton(button)) {
						button_effectively_released = true;
					}
					// SDL_Log("Button code %02X released by finger %lld", button.code, fingerId);
				}
				else if (button.pressed && button.pressingFingerId == fingerId && button.stuck) {
					// Finger lifted from a button it was actively holding while stuck.
					// The button remains pressed because it's stuck.
					// We can clear the pressingFingerId to indicate no specific finger is actively holding it down anymore.
					// button.pressingFingerId = -1; // Or a generic "stuck" marker not tied to an active finger.
					// SDL_Log("Finger %lld lifted from STUCK button %02X. Button remains pressed due to stuck state.", fingerId, button.code);
				}
				// If another finger is pressing it, or if it's stuck by another finger/mouse, this finger_up does nothing to its pressed state.
				return; // Process only the first button found at coordinates
			}
		}

		if (button_effectively_released) {
			if (real_hardware) {
				RecalculateGhost();
			}
			else {
				RecalculateEmuInput();
			}
		}
	}

	void Keyboard::PressButtonByCode(uint8_t code) {
		if (code == 0xFF) {
			// Assuming POWER button is at index 63, not passing fingerId (treat as system event)
			PressButton(buttons[63], false, 0);
		}
		else {
			int button_index = ((code >> 1) & 0x38) | (code & 0x07);
			if (button_index < 63) {						  // Ensure index is valid
				PressButton(buttons[button_index], false, 0); // Not passing fingerId
			}
			else {
				// printf("[Keyboard][Info] Invalid button code 0x%02X for PressButtonByCode!\n", code);
			}
		}
	}

	void Keyboard::StartInject() {
	}

	void Keyboard::StoreKeyLog() {
	}

	void Keyboard::RecalculateGhost() { // This is for real_hardware=true path
		struct KOColumn {
			uint8_t connections;
			uint8_t KIRows;
			bool seen;
		} columns[8];

		has_input = 0; // Recalculate has_input based on currently pressed buttons
		if (emulator.hardware_id == HW_FX_5800P) {
			for (const auto& button : buttons) // Iterate const
				if (button.type == Button::BT_BUTTON && button.pressed && button.ki_bit)
					has_input |= button.ki_bit;
		}
		else {
			for (const auto& button : buttons) // Iterate const
				if (button.type == Button::BT_BUTTON && button.pressed && button.ki_bit & input_filter)
					has_input |= button.ki_bit;
		}

		for (size_t cx = 0; cx != 8; ++cx) {
			columns[cx].seen = false;
			columns[cx].connections = 0;
			columns[cx].KIRows = 0;
			for (size_t rx = 0; rx != 8; ++rx) {
				const Button& button = buttons[cx * 8 + rx]; // Iterate const
				if (button.type == Button::BT_BUTTON && button.pressed) {
					columns[cx].KIRows |= 1 << rx;
					for (size_t ax = 0; ax != 8; ++ax) {
						const Button& sibling = buttons[ax * 8 + rx]; // Iterate const
						if (sibling.type == Button::BT_BUTTON && sibling.pressed)
							columns[cx].connections |= 1 << ax;
					}
				}
			}
		}

		for (size_t gx = 0; gx != 8; ++gx) {
			ki_ghost[gx] = 0;
		}

		for (size_t cx = 0; cx != 8; ++cx) {
			if (!columns[cx].seen) {
				uint8_t to_visit = 1 << cx;
				uint8_t ghost_mask = 1 << cx;
				columns[cx].seen = true;

				while (to_visit) {
					uint8_t new_to_visit = 0;
					for (size_t vx = 0; vx != 8; ++vx) {
						if (to_visit & (1 << vx)) {
							for (size_t sx = 0; sx != 8; ++sx) {
								if (columns[vx].connections & (1 << sx) && !columns[sx].seen) {
									new_to_visit |= 1 << sx;
									ghost_mask |= 1 << sx;
									columns[cx].KIRows |= columns[sx].KIRows;
									columns[sx].seen = true;
								}
							}
						}
					}
					to_visit = new_to_visit;
				}

				for (size_t gx = 0; gx != 8; ++gx) {
					if (ghost_mask & (1 << gx))
						keyboard_ghost[gx] = ghost_mask;
					if (columns[cx].KIRows & (1 << gx))
						ki_ghost[gx] = columns[cx].KIRows;
				}
			}
		}
		RecalculateKI(); // Recalculate physical KI lines based on all current presses
	}

	void Keyboard::RecalculateKI() { // This is for real_hardware=true path
		if (emulator.hardware_id == HW_TI) {
			auto pp = emulator.chipset.QueryInterface<IPortProvider>();
			if (!pp)
				return;
			auto is_on_pressed = false;
			keyboard_in = 0;
			for (const auto& button : buttons) { // Iterate const
				if (button.code == 0x29) {
					if (button.pressed)
						is_on_pressed = true;
					continue;
				}
				if (button.type == Button::BT_BUTTON && button.pressed && button.ko_bit & keyboard_out)
					keyboard_in |= button.ki_bit;
			}
			pp->SetPortInput(0, is_on_pressed ? 0x20 : 0, 0x20);
			pp->SetPortInput(4, keyboard_in, 0xff);
			return;
		}
		if (keyboard_in_override) {
			keyboard_in = keyboard_in_override;
			return;
		}
		if (emulator.hardware_id == HW_FX_5800P || emulator.ModelDefinition.legacy_ko) {
			keyboard_in = 0xFF;
			for (const auto& button : buttons) { // Iterate const
				if (button.type == Button::BT_BUTTON && button.pressed && button.ko_bit & keyboard_out)
					keyboard_in &= ~button.ki_bit;
			}
			return;
		}
		uint8_t keyboard_out_ghosted = 0;
		uint8_t ki_pulled_up = 0;
		for (size_t ix = 0; ix != 7; ++ix)
			if (keyboard_out & ~keyboard_out_mask & (1 << ix))
				keyboard_out_ghosted |= keyboard_ghost[ix];

		keyboard_in = ~input_mode;
		for (const auto& button : buttons) { // Iterate const
			if (button.type == Button::BT_BUTTON && button.pressed && button.ko_bit & keyboard_out_ghosted)
				keyboard_in &= ~button.ki_bit;
		}
		for (size_t ix = 0; ix != 8; ++ix)
			if (keyboard_in & (1 << ix))
				ki_pulled_up |= ki_ghost[ix];

		for (const auto& button : buttons) { // Iterate const
			if (button.type == Button::BT_BUTTON && button.pressed && button.ki_bit & input_mode & ki_pulled_up)
				keyboard_in |= button.ki_bit;
		}
		if (emulator.hardware_id != HW_TI) {
			if (keyboard_out & ~keyboard_out_mask & (1 << 7) && p0)
				keyboard_in &= 0x7F;
			if (keyboard_out & ~keyboard_out_mask & (1 << 8) && p1)
				keyboard_in &= 0x7F;
			if (keyboard_out & ~keyboard_out_mask & (1 << 9) && p146)
				keyboard_in &= 0x7F;
		}
	}

	void Keyboard::ReleaseAll() {
		bool had_effect = false;
		// SDL_Log("Release All called!");
		for (auto& button : buttons) {
			// Release a button if it's pressed AND NOT stuck.
			// If it's stuck, ReleaseAll() does not unstick it. Stuck buttons need a specific stick-press to toggle.
			if (button.pressed && !button.stuck) {
				if (TryReleaseButton(button)) {
					if (button.type == Button::BT_BUTTON) {
						had_effect = true;
					}
				}
			}
			else if (button.stuck && button.pressingFingerId != -1) {
				// If it's stuck AND a finger was last associated with it (e.g. finger held it down while it got stuck),
				// clear the finger ID as that finger is no longer "actively" pressing it due to ReleaseAll.
				// The button remains pressed because it's stuck.
				// button.pressingFingerId = -1; // Or some other generic "stuck" marker
			}
		}

		if (had_effect) {
			if (real_hardware) {
				RecalculateGhost(); // This calls RecalculateKI
			}
			else {
				RecalculateEmuInput();
			}
		}
	}
	Peripheral* CreateKeyboard(Emulator& emu) {
		return new Keyboard(emu);
	}
} // namespace casioemu
