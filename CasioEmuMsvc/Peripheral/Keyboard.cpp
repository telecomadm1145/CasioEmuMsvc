#include "Keyboard.hpp"

#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Emulator.hpp"
#include "Logger.hpp"
#include "ModelInfo.h"

#include <ML620Ports.h>
#include <SDL.h>
#include <chrono>
#include <fstream>
#include <thread>
#include <unordered_map>
#include "vibration.h"

namespace casioemu {
	class Keyboard : public Peripheral {
		MMURegion region_ko_mask, region_ko, region_ki, region_input_mode, region_input_filter;
		uint16_t keyboard_out, keyboard_out_mask;
		uint8_t keyboard_in, input_mode, input_filter, keyboard_ghost[8], ki_ghost[8];
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
		void PressButton(Button& button, bool stick);
		void PressAt(int x, int y, bool stick);
		void PressButtonByCode(uint8_t code);
		void StartInject();
		void StoreKeyLog();
		void ReleaseAll();
		void RecalculateKI();
		void RecalculateGhost();
	};
	void Keyboard::Initialise() {
		renderer = emulator.GetRenderer();

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

		region_ki.Setup(0xF040, 1, "Keyboard/KI", &keyboard_in, MMURegion::DefaultRead<uint8_t>, MMURegion::IgnoreWrite, emulator);

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
			auto button_name = btn.keyname.c_str();

			SDL_Keycode button_key;
			button_key = SDL_GetKeyFromName(button_name);
			if (button_key == SDLK_UNKNOWN)
				printf("[Keyboard][Warn] Key %x is being bind to a invalid or empty key '%s'\n", btn.kiko, button_name);

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
		if (factory_test) {
			keyboard_in = (uint8_t)~0b00011000; // KI 3 KI 4 enabled xD
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
            case SDL_FINGERDOWN:
            case SDL_FINGERUP:
                if (event.type == SDL_FINGERDOWN) {
                    SDL_Log("Pressed at: %f %f",event.tfinger.x , event.tfinger.y);
                    PressAt(event.tfinger.x , event.tfinger.y, false);
                } else {
                    ReleaseAll();
                }
                break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			switch (event.button.button) {
			case SDL_BUTTON_LEFT:
				if (event.button.state == SDL_PRESSED)
					PressAt(event.button.x, event.button.y, false);
				else
					ReleaseAll();
				break;

			case SDL_BUTTON_RIGHT:
				if (event.button.state == SDL_PRESSED)
					PressAt(event.button.x, event.button.y, true);
				break;
			}
			break;

		case SDL_KEYDOWN:
		case SDL_KEYUP:
			SDL_Keycode keycode = event.key.keysym.sym;
			auto iterator = keyboard_map.find(keycode);
			printf("[Keyboard][Info] SDL_Keycode: %x(%s)\n", keycode, SDL_GetKeyName(keycode));
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
				PressButton(buttons[iterator->second], false);
			else
				ReleaseAll();
			break;
		}
	}

	void Keyboard::Uninitialise() {
	}

	void Keyboard::PressButton(Button& button, bool stick) {
		bool old_pressed = button.pressed;
		if (stick) {
			button.stuck = !button.stuck;
			button.pressed = button.stuck;
		}
		else
			button.pressed = true;

		if (button.type == Button::BT_POWER && button.pressed && !old_pressed) {
			if (!(emulator.hardware_id == HW_CLASSWIZ && (emulator.chipset.data_FCON & 0x03) == 0x03))
				emulator.chipset.Reset();
			else {
				printf("[Keyboard][Info] RESETB is BLOCKED.Press Ctrl+F11 to reset.\n");
			}
		}
		if (button.type == Button::BT_BUTTON) {
			if (emulator.hardware_id == HW_TI) {
				// emulator.chipset.MaskableInterrupts[EXI0INT].TryRaise();
				printf("[Keyboard][Info] Keycode: 0x%x\n", button.code);
				// if (!emulator.chipset.GetRunningState() && button.code == 0x29) {
				//	emulator.chipset.Reset();
				// }
				emulator.chipset.tiKey = button.code;
			}
			printf("[Keyboard][Info] KI: %d, KO: %d\n", (int)(log(button.ki_bit) / log(2)), (int)(log(button.ko_bit) / log(2)));
		}

		if (button.type == Button::BT_BUTTON) {
			Vibration::vibrate(100);
			if (real_hardware) {
				RecalculateGhost();
			}
			else {
				// The emulator provided by Casio does not support multiple keys being pressed at once.
				if (button.pressed) {
					// Report an arbitrary pressed key.
					keyboard_in_emu = button.ki_bit;
					keyboard_out_emu = button.ko_bit;
					emu_ki_readcount = emu_ko_readcount = 0;
					emulator.chipset.MaskableInterrupts[EXI0INT].TryRaise();
				}
				else {
					// This key is released. There might still be other keys being held.
					keyboard_in_emu = keyboard_out_emu = 0;
				}
			}
		}
	}

	void Keyboard::PressAt(int x, int y, bool stick) {
		for (auto& button : buttons) {
			if (button.rect.x <= x && button.rect.y <= y && button.rect.x + button.rect.w > x && button.rect.y + button.rect.h > y) {
				PressButton(button, stick);
				break;
			}
		}
	}

	void Keyboard::PressButtonByCode(uint8_t code) {
		if (code == 0xFF) {
			PressButton(buttons[63], false);
		}
		else {
			int button_index = ((code >> 1) & 0x38) | (code & 0x07);
			if (button_index < 63) {
				PressButton(buttons[button_index], false);
			}
			else {
				printf("[Keyboard][Info] Invalid button code 0x%02X!\n", code);
			}
		}
	}

	void Keyboard::StartInject() {
	}

	void Keyboard::StoreKeyLog() {
	}

	void Keyboard::RecalculateGhost() {
		struct KOColumn {
			uint8_t connections;
			uint8_t KIRows;
			bool seen;
		} columns[8];

		has_input = 0;
		if (emulator.hardware_id == HW_FX_5800P) {
			for (auto& button : buttons)
				if (button.type == Button::BT_BUTTON && button.pressed && button.ki_bit)
					has_input |= button.ki_bit;
		}
		else {
			for (auto& button : buttons)
				if (button.type == Button::BT_BUTTON && button.pressed && button.ki_bit & input_filter)
					has_input |= button.ki_bit;
		}

		for (size_t cx = 0; cx != 8; ++cx) {
			columns[cx].seen = false;
			columns[cx].connections = 0;
			columns[cx].KIRows = 0;
			for (size_t rx = 0; rx != 8; ++rx) {
				Button& button = buttons[cx * 8 + rx];
				if (button.type == Button::BT_BUTTON && button.pressed) {
					columns[cx].KIRows |= 1 << rx;
					for (size_t ax = 0; ax != 8; ++ax) {
						Button& sibling = buttons[ax * 8 + rx];
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

		RecalculateKI();
	}

	void Keyboard::RecalculateKI() {
		if (emulator.hardware_id == HW_TI) {
			auto pp = emulator.chipset.QueryInterface<IPortProvider>();
			if (!pp) // No port provider :(
				return;
			auto is_on_pressed = false;
			keyboard_in = 0;
			for (auto& button : buttons) {
				if (button.code == 0x29) { // right, [ON] is a gpio button xd
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
		if (emulator.hardware_id == HW_FX_5800P || emulator.ModelDefinition.legacy_ko) { // TODO: label this as legacy ko?
			keyboard_in = 0xFF;
			for (auto& button : buttons)
				if (button.type == Button::BT_BUTTON && button.pressed && button.ko_bit & keyboard_out)
					keyboard_in &= ~button.ki_bit;
			return;
		}
		uint8_t keyboard_out_ghosted = 0;
		uint8_t ki_pulled_up = 0;
		for (size_t ix = 0; ix != 7; ++ix)
			if (keyboard_out & ~keyboard_out_mask & (1 << ix))
				keyboard_out_ghosted |= keyboard_ghost[ix];

		keyboard_in = ~input_mode;
		for (auto& button : buttons)
			if (button.type == Button::BT_BUTTON && button.pressed && button.ko_bit & keyboard_out_ghosted)
				keyboard_in &= ~button.ki_bit;

		for (size_t ix = 0; ix != 8; ++ix)
			if (keyboard_in & (1 << ix))
				ki_pulled_up |= ki_ghost[ix];

		for (auto& button : buttons)
			if (button.type == Button::BT_BUTTON && button.pressed && button.ki_bit & input_mode & ki_pulled_up)
				keyboard_in |= button.ki_bit;
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
        SDL_Log("Release All called!");
		for (auto& button : buttons) {
			if (!button.stuck && button.pressed) {
				button.pressed = false;
				if (button.type == Button::BT_BUTTON)
					had_effect = true;
			}
		}

		if (had_effect) {
			if (real_hardware)
				RecalculateGhost();
			else
				has_input = keyboard_in_emu = keyboard_out_emu = 0;
		}
	}
	Peripheral* CreateKeyboard(Emulator& emu) {
		return new Keyboard(emu);
	}
} // namespace casioemu
