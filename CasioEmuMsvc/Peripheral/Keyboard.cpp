#include "Keyboard.hpp"

#include "../Data/HardwareId.hpp"
#include "../Logger.hpp"
#include "../Chipset/MMU.hpp"
#include "../Emulator.hpp"
#include "../Chipset/Chipset.hpp"
#include "../Gui/KeyLog.h"

#include <fstream>
#include <thread>
#include <chrono>
#include <lua.hpp>
#include <SDL.h>

namespace casioemu
{
	void Keyboard::Initialise()
	{
		renderer = emulator.GetRenderer();
		require_frame = true;

		/*
		 * When real_hardware is false, the program should emulate the behavior of the
		 * calculator emulator provided by Casio, which has different keyboard input
		 * interface.
		 */
		real_hardware = emulator.GetModelInfo("real_hardware");

		IntRaised = false;

		region_ki.Setup(0xF040, 1, "Keyboard/KI", &keyboard_in, MMURegion::DefaultRead<uint8_t>, MMURegion::IgnoreWrite, emulator);

		region_input_filter.Setup(0xF042, 1, "Keyboard/InputFilter", &input_filter, MMURegion::DefaultRead<uint8_t>, MMURegion::DefaultWrite<uint8_t>, emulator);

		region_ko_mask.Setup(0xF044, 2, "Keyboard/KOMask", this, [](MMURegion* region, size_t offset) {
			offset -= region->base;
			Keyboard* keyboard = ((Keyboard*)region->userdata);
			return (uint8_t)((keyboard->keyboard_out_mask & 0x03FF) >> (offset * 8));
			}, [](MMURegion* region, size_t offset, uint8_t data) {
				offset -= region->base;
				Keyboard* keyboard = ((Keyboard*)region->userdata);
				keyboard->keyboard_out_mask &= ~(((uint16_t)0xFF) << (offset * 8));
				keyboard->keyboard_out_mask |= ((uint16_t)data) << (offset * 8);
				keyboard->keyboard_out_mask &= 0x03FF;
				if (!offset)
					keyboard->RecalculateKI();
				}, emulator);

		region_ko.Setup(0xF046, 2, "Keyboard/KO", this, [](MMURegion* region, size_t offset) {
			offset -= region->base;
			Keyboard* keyboard = ((Keyboard*)region->userdata);
			return (uint8_t)((keyboard->keyboard_out & 0x03FF) >> (offset * 8));
			}, [](MMURegion* region, size_t offset, uint8_t data) {
				offset -= region->base;
				Keyboard* keyboard = ((Keyboard*)region->userdata);
				keyboard->keyboard_out &= ~(((uint16_t)0xFF) << (offset * 8));
				keyboard->keyboard_out |= ((uint16_t)data) << (offset * 8);
				keyboard->keyboard_out &= 0x03FF;
				if (!offset)
					keyboard->RecalculateKI();
				}, emulator);

		keybd_in = [&](int ki, int ko) {
			if (ki == 114514) {
				ReleaseAll();
				emulator.chipset.Reset();
				return;
			}
			Button* selected=0;
			for (auto& btn : buttons) {
				if (getHighestBitPosition(btn.ki_bit) == ki && getHighestBitPosition(btn.ko_bit) == ko) {
					selected = &btn;
					break;
				}
			}
			if (!selected)
				return;
			PressButton(*selected,false);
			emulator.chipset.MaskableInterrupts[IntIndex].TryRaise();
			std::this_thread::sleep_for(std::chrono::milliseconds(300));
			ReleaseAll();
		};

		if (!real_hardware)
		{
			keyboard_pd_emu = emulator.GetModelInfo("pd_value");
			keyboard_ready_emu = 1;
			emu_ki_readcount = 0;
			emu_ko_readcount = 0;
			int offset = emulator.hardware_id == HW_ES_PLUS ? 0 : emulator.hardware_id == HW_CLASSWIZ ? 0x40000 : 0x80000;
			region_ready_emu.Setup(offset + 0x8E00, 1, "Keyboard/ReadyStatusEmulator", this, [](MMURegion* region, size_t offset) {
				Keyboard* keyboard = ((Keyboard*)region->userdata);
				return keyboard->keyboard_ready_emu;
				}, [](MMURegion* region, size_t offset, uint8_t data) {
					Keyboard* keyboard = ((Keyboard*)region->userdata);
					if (data == 8) {
						keyboard->emulator.chipset.EmuTimerSkipped = true;
						if (keyboard->keyboard_in_emu == 4 && keyboard->keyboard_out_emu == 16) {
							keyboard->keyboard_ready_emu = 1;
						}
						else {
							keyboard->keyboard_ready_emu = 0;
						}
						return;
					}
					else if (data == 4) {
						keyboard->emulator.chipset.EmuTimerSkipped = true;
					}
					keyboard->keyboard_ready_emu = data;
					}, emulator);
			region_ki_emu.Setup(offset + 0x8E01, 1, "Keyboard/KIEmulator", this, [](MMURegion* region, size_t offset) {
				Keyboard* keyboard = ((Keyboard*)region->userdata);
				keyboard->emu_ki_readcount++;
				uint8_t value = keyboard->keyboard_in_emu;
				if (keyboard->emu_ki_readcount > 1)
					keyboard->keyboard_in_emu = 0;
				return value;
				}, MMURegion::IgnoreWrite, emulator);
			region_ko_emu.Setup(offset + 0x8E02, 1, "Keyboard/KOEmulator", this, [](MMURegion* region, size_t offset) {
				Keyboard* keyboard = ((Keyboard*)region->userdata);
				keyboard->emu_ko_readcount++;
				uint8_t value = keyboard->keyboard_out_emu;
				if (keyboard->emu_ko_readcount > 1)
					keyboard->keyboard_out_emu = 0;
				return value;
				}, MMURegion::IgnoreWrite, emulator);
			region_pd_emu.Setup(0xF050, 1, "Keyboard/PdValue", &keyboard_pd_emu, MMURegion::DefaultRead<uint8_t>, MMURegion::IgnoreWrite, emulator);
		}

		isInjectorTriggered = false;
		isKeyLogToggled = false;

		*(Keyboard**)lua_newuserdata(emulator.lua_state, sizeof(Keyboard*)) = this;
		lua_newtable(emulator.lua_state);
		lua_newtable(emulator.lua_state);
		lua_pushcfunction(emulator.lua_state, [](lua_State* lua_state) {
			Keyboard* keyboard = *(Keyboard**)lua_topointer(lua_state, 1);
			if (lua_gettop(lua_state) != 2) {
				logger::Info("Invalid argument nums!\n");
				return 0;
			}
			uint8_t code = lua_tointeger(lua_state, 2);
			keyboard->PressButtonByCode(code);
			return 0;
			});
		lua_setfield(emulator.lua_state, -2, "PressKey");
		lua_pushcfunction(emulator.lua_state, [](lua_State* lua_state) {
			Keyboard* keyboard = *(Keyboard**)lua_topointer(lua_state, 1);
			keyboard->ReleaseAll();
			return 0;
			});
		lua_setfield(emulator.lua_state, -2, "ReleaseAll");
		lua_pushcfunction(emulator.lua_state, [](lua_State* lua_state) {
			Keyboard* keyboard = *(Keyboard**)lua_topointer(lua_state, 1);
			if (keyboard->isInjectorTriggered) {
				logger::Info("Injector already triggered!\n");
				return 0;
			}
			switch (lua_gettop(lua_state)) {
			case 2:
				keyboard->PressTime = 100;
				keyboard->DelayTime = 150;
				break;
			case 3:
				keyboard->PressTime = lua_tointeger(lua_state, 3);
				keyboard->DelayTime = 150;
				break;
			case 4:
				keyboard->PressTime = lua_tointeger(lua_state, 3);
				keyboard->DelayTime = lua_tointeger(lua_state, 4);
				break;
			default:
				logger::Info("Invalid argument num!\n");
				return 0;
			}
			keyboard->keyseq_filename = lua_tostring(lua_state, 2);
			keyboard->StartInject();
			return 0;
			});
		lua_setfield(emulator.lua_state, -2, "KeyInject");
		lua_pushcfunction(emulator.lua_state, [](lua_State* lua_state) {
			Keyboard* keyboard = *(Keyboard**)lua_topointer(lua_state, 1);
			if (lua_gettop(lua_state) != 2) {
				logger::Info("Invalid argument nums!\n");
				return 0;
			}
			keyboard->keylog_filename = lua_tostring(lua_state, 2);
			keyboard->isKeyLogToggled = true;
			keyboard->KeyLogIndex = 0;
			keyboard->KeyLog = new uint8_t[100000];
			return 0;
			});
		lua_setfield(emulator.lua_state, -2, "StartKeyLog");
		lua_pushcfunction(emulator.lua_state, [](lua_State* lua_state) {
			Keyboard* keyboard = *(Keyboard**)lua_topointer(lua_state, 1);
			if (!keyboard->isKeyLogToggled) {
				logger::Info("KeyLog not started!\n");
				return 0;
			}
			keyboard->isKeyLogToggled = false;
			keyboard->StoreKeyLog();
			return 0;
			});
		lua_setfield(emulator.lua_state, -2, "StopKeyLog");
		lua_setfield(emulator.lua_state, -2, "__index");
		lua_pushcfunction(emulator.lua_state, [](lua_State*) {
			return 0;
			});
		lua_setfield(emulator.lua_state, -2, "__newindex");
		lua_setmetatable(emulator.lua_state, -2);
		lua_setglobal(emulator.lua_state, "Keyboard");

		{
			for (auto& button : buttons)
				button.type = Button::BT_NONE;

			const char* key = "button_map";
			lua_geti(emulator.lua_state, LUA_REGISTRYINDEX, emulator.lua_model_ref);
			if (lua_getfield(emulator.lua_state, -1, key) != LUA_TTABLE)
				PANIC("key '%s' is not a table\n", key);
			lua_len(emulator.lua_state, -1);
			size_t buttons_size = lua_tointeger(emulator.lua_state, -1);
			lua_pop(emulator.lua_state, 1);

			for (size_t ix = 0; ix != buttons_size; ++ix)
			{
				if (lua_geti(emulator.lua_state, -1, ix + 1) != LUA_TTABLE)
					PANIC("key '%s'[%zu] is not a table\n", key, ix + 1);

				if (lua_geti(emulator.lua_state, -1, 6) != LUA_TSTRING)
					PANIC("key '%s'[%zu][6] is not a string\n", key, ix + 1);

				size_t button_name_len;
				const char* button_name = lua_tolstring(emulator.lua_state, -1, &button_name_len);
				if (strlen(button_name) != button_name_len)
					PANIC("Key name '%.*s' contains null byte\n", (int)button_name_len, button_name);

				SDL_Keycode button_key;
				if (button_name_len == 0)
				{
					button_key = SDLK_UNKNOWN;
				}
				else
				{
					button_key = SDL_GetKeyFromName(button_name);
					if (button_key == SDLK_UNKNOWN)
						PANIC("Key name '%s' is invalid\n", button_name);
				}
				lua_pop(emulator.lua_state, 1); // Pop the key name

				for (int kx = 0; kx != 5; ++kx)
					if (lua_geti(emulator.lua_state, -1 - kx, kx + 1) != LUA_TNUMBER)
						PANIC("key '%s'[%zu][%i] is not a number\n", key, ix + 1, kx + 1);

				uint8_t code = lua_tointeger(emulator.lua_state, -1);
				size_t button_ix;
				if (code == 0xFF)
				{
					button_ix = 63;
				}
				else
				{
					button_ix = ((code >> 1) & 0x38) | (code & 0x07);
					if (button_ix >= 64)
						PANIC("button index doesn't fit 6 bits\n");
				}

				if (button_key != SDLK_UNKNOWN)
				{
					bool insert_success = keyboard_map.emplace(button_key, button_ix).second;
					if (!insert_success)
						PANIC("Key '%s' is used twice\n", button_name);
				}

				Button& button = buttons[button_ix];

				if (code == 0xFF)
					button.type = Button::BT_POWER;
				else
					button.type = Button::BT_BUTTON;
				button.rect.x = lua_tointeger(emulator.lua_state, -5);
				button.rect.y = lua_tointeger(emulator.lua_state, -4);
				button.rect.w = lua_tointeger(emulator.lua_state, -3);
				button.rect.h = lua_tointeger(emulator.lua_state, -2);
				button.ko_bit = 1 << ((code >> 4) & 0xF);
				button.ki_bit = 1 << (code & 0xF);

				lua_pop(emulator.lua_state, 6);

				button.pressed = false;
				button.stuck = false;
			}

			lua_pop(emulator.lua_state, 2);
		}
	}

	void Keyboard::Reset()
	{
		p0 = false;
		p1 = false;
		p146 = false;
		keyboard_out = 0;
		keyboard_out_mask = 0;
		IntRaised = false;

		if (!real_hardware)
		{
			keyboard_in_emu = 0;
			emu_ki_readcount = 0;
			keyboard_out_emu = 0;
			emu_ko_readcount = 0;
		}

		RecalculateGhost();
	}

	void Keyboard::Tick()
	{
		if (has_input && (!IntRaised)) {
			emulator.chipset.MaskableInterrupts[IntIndex].TryRaise();
			IntRaised = true;
		}
	}
	void Keyboard::Frame()
	{
		require_frame = false;

		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		for (auto& button : buttons)
		{
			if (button.type != Button::BT_NONE && button.pressed)
			{
				if (button.stuck)
					SDL_SetRenderDrawColor(renderer, 127, 0, 0, 127);
				else
					SDL_SetRenderDrawColor(renderer, 0, 0, 0, 127);
				SDL_RenderFillRect(renderer, &button.rect);
			}
		}
	}

	void Keyboard::UIEvent(SDL_Event& event)
	{
		switch (event.type)
		{
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			switch (event.button.button)
			{
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
		if (isKeyLogToggled) {
			StoreKeyLog();
			delete[] KeyLog;
			isKeyLogToggled = false;
		}
	}
	void Keyboard::PressButton(Button& button, bool stick)
	{
		bool old_pressed = button.pressed;
		if (stick)
		{
			button.stuck = !button.stuck;
			button.pressed = button.stuck;
		}
		else
			button.pressed = true;

		require_frame = true;

		if (button.type == Button::BT_POWER && button.pressed && !old_pressed)
		{
			AppendKey(0x35);
			emulator.chipset.Reset();
		}
		else if (button.pressed) {
			AppendKey(cwii_keymap[7 - getHighestBitPosition(button.ki_bit)][getHighestBitPosition(button.ko_bit)]);
		}
		if (button.type == Button::BT_BUTTON && button.pressed != old_pressed)
		{
			if (real_hardware)
				RecalculateGhost();
			else
			{
				// The emulator provided by Casio does not support multiple keys being pressed at once.
				if (button.pressed)
				{
					// Report an arbitrary pressed key.
					has_input = keyboard_in_emu = button.ki_bit;
					keyboard_out_emu = button.ko_bit;
					emu_ki_readcount = emu_ko_readcount = 0;
					emulator.chipset.EmuTimerSkipped = true;
				}
				else
				{
					// This key is released. There might still be other keys being held.
					has_input = keyboard_in_emu = keyboard_out_emu = 0;
				}
			}
		}

		if (isKeyLogToggled) {
			uint8_t keycode = 0;
			if (button.type == Button::BT_POWER) {
				keycode = 0xFF;
			}
			else if (button.type == Button::BT_BUTTON) {
				for (int bit = 1; bit <= 0x80; bit = bit << 1) {
					if (button.ko_bit == bit)
						break;
					keycode++;
				}
				keycode = keycode << 4;
				for (int bit = 1; bit <= 0x80; bit = bit << 1) {
					if (button.ki_bit == bit)
						break;
					keycode++;
				}
			}
			KeyLog[KeyLogIndex] = keycode;
			KeyLogIndex++;
		}
	}

	void Keyboard::PressAt(int x, int y, bool stick)
	{
		for (auto& button : buttons)
		{
			if (button.rect.x <= x && button.rect.y <= y && button.rect.x + button.rect.w > x && button.rect.y + button.rect.h > y)
			{
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
				logger::Info("Invalid button code 0x%02X!\n", code);
			}
		}
	}

	void Keyboard::StartInject() {
		std::thread inj_t([&]() {
			isInjectorTriggered = true;
			std::ifstream keyseq_handle(keyseq_filename, std::ifstream::binary);
			if (keyseq_handle.fail()) {
				logger::Info("Failed to load file %s\n", keyseq_filename);
				isInjectorTriggered = false;
				return;
			}
			std::vector<unsigned char> keyseq_raw = std::vector<unsigned char>((std::istreambuf_iterator<char>(keyseq_handle)), std::istreambuf_iterator<char>());
			for (int i = 0; i < (int)keyseq_raw.size(); i++) {
				PressButtonByCode(keyseq_raw[i]);
				std::this_thread::sleep_for(std::chrono::milliseconds(PressTime));
				ReleaseAll();
				std::this_thread::sleep_for(std::chrono::milliseconds(DelayTime));
			}
			isInjectorTriggered = false;
			});
		inj_t.detach();
	}

	void Keyboard::StoreKeyLog() {
		std::ofstream keylog_handle(keylog_filename, std::ofstream::binary);
		if (keylog_handle.fail()) {
			logger::Info("Failed to store KeyLog at %s\n", keylog_filename);
			return;
		}
		keylog_handle.write((char*)KeyLog, KeyLogIndex);
		keylog_handle.close();
	}

	void Keyboard::RecalculateGhost()
	{
		struct KOColumn
		{
			uint8_t connections;
			bool seen;
		} columns[8];

		has_input = 0;
		for (auto& button : buttons)
			if (button.type == Button::BT_BUTTON && button.pressed && button.ki_bit & input_filter)
				has_input |= button.ki_bit;

		for (size_t cx = 0; cx != 8; ++cx)
		{
			columns[cx].seen = false;
			columns[cx].connections = 0;
			for (size_t rx = 0; rx != 8; ++rx)
			{
				Button& button = buttons[cx * 8 + rx];
				if (button.type == Button::BT_BUTTON && button.pressed)
				{
					for (size_t ax = 0; ax != 8; ++ax)
					{
						Button& sibling = buttons[ax * 8 + rx];
						if (sibling.type == Button::BT_BUTTON && sibling.pressed)
							columns[cx].connections |= 1 << ax;
					}
				}
			}
		}

		for (size_t cx = 0; cx != 8; ++cx)
		{
			if (!columns[cx].seen)
			{
				uint8_t to_visit = 1 << cx;
				uint8_t ghost_mask = 1 << cx;
				columns[cx].seen = true;

				while (to_visit)
				{
					uint8_t new_to_visit = 0;
					for (size_t vx = 0; vx != 8; ++vx)
					{
						if (to_visit & (1 << vx))
						{
							for (size_t sx = 0; sx != 8; ++sx)
							{
								if (columns[vx].connections & (1 << sx) && !columns[sx].seen)
								{
									new_to_visit |= 1 << sx;
									ghost_mask |= 1 << sx;
									columns[sx].seen = true;
								}
							}
						}
					}
					to_visit = new_to_visit;
				}

				for (size_t gx = 0; gx != 8; ++gx)
					if (ghost_mask & (1 << gx))
						keyboard_ghost[gx] = ghost_mask;
			}
		}

		RecalculateKI();
	}

	void Keyboard::RecalculateKI()
	{
		uint8_t keyboard_out_ghosted = 0;
		for (size_t ix = 0; ix != 7; ++ix)
			if (keyboard_out & ~keyboard_out_mask & (1 << ix))
				keyboard_out_ghosted |= keyboard_ghost[ix];

		keyboard_in = 0xFF;
		for (auto& button : buttons)
			if (button.type == Button::BT_BUTTON && button.pressed && button.ko_bit & keyboard_out_ghosted)
				keyboard_in &= ~button.ki_bit;

		if (keyboard_out & ~keyboard_out_mask & (1 << 7) && p0)
			keyboard_in &= 0x7F;
		if (keyboard_out & ~keyboard_out_mask & (1 << 8) && p1)
			keyboard_in &= 0x7F;
		if (keyboard_out & ~keyboard_out_mask & (1 << 9) && p146)
			keyboard_in &= 0x7F;
	}

	void Keyboard::ReleaseAll()
	{
		bool had_effect = false;
		for (auto& button : buttons)
		{
			if (!button.stuck && button.pressed)
			{
				button.pressed = false;
				if (button.type == Button::BT_BUTTON)
					had_effect = true;
			}
		}
		if (had_effect)
		{
			require_frame = true;
			IntRaised = false;
			if (real_hardware)
				RecalculateGhost();
			else
				has_input = keyboard_in_emu = keyboard_out_emu = 0;
		}
	}
}

