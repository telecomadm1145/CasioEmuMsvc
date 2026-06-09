#include "Audio.h"
#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Emulator.hpp"
namespace casioemu {
	class AudioDriver : public Peripheral {
		uint8_t control{}, tempo{};
		uint16_t length{};

		MMURegion MD0CON{}, MD0TMP{}, MD0TL{};

		size_t count = 0;
		static void audio_callback(void* userdata, Uint8* stream, int len) {
			((AudioDriver*)userdata)->real_callback((uint16_t*)stream, len >> 1);
		}
		bool T8HZ() {
			return (SDL_GetTicks64() % 256) < 128;
		}
		bool T1HZ() {
			return (SDL_GetTicks64() % 2000) < 1000;
		}
		void real_callback(uint16_t* stream, int len) {
			if (control) {
				if (tempo == 0) {
					if (T8HZ()) {
						memset(stream, 0, len << 1);
						return;
					}
				}
				else if (tempo == 1) {
					if (T8HZ() || T1HZ()) {
						memset(stream, 0, len << 1);
						return;
					}
				}
				else if (tempo == 2) {
					if (count > 441000 / 32) {
						control = 0;
						tempo = 0;
						memset(stream, 0, len << 1);
						return;
					}
				}
				auto freq = length & 0xf;
				auto clk = 0;
				auto on = 0;
				switch (freq) {
				case 0:
					clk = 108;
					break;
				case 1:
					clk = 215;
					break;
				case 2:
					clk = 323;
					break;
				case 3:
					clk = 431;
					break;
				case 4:
					clk = 538;
					break;
				case 5:
					clk = 646;
					break;
				case 6:
					clk = 754;
					break;
				case 7:
					clk = 861;
					break;
				default:
					break;
				}
				auto duty = length >> 8;
				if (duty == 0)
					duty = 1;
				on = clk * duty / 16;
				for (int i = 0; i < len; i++) {
					stream[i] = ((count % clk) < on) ? 16384 : 0;
					count++;
				}
			}
			else {
				memset(stream, 0, len << 1);
				count = 0;
			}
		}
	public:
		AudioDriver(Emulator& emu) : Peripheral(emu) {
			SDL_Init(SDL_INIT_AUDIO);

			SDL_AudioSpec desired_spec{};
			desired_spec.freq = 44100;
			desired_spec.format = AUDIO_S16SYS;
			desired_spec.channels = 1;
#ifdef __EMSCRIPTEN__
			desired_spec.samples = 512;
#else
			desired_spec.samples = 64;
#endif
			desired_spec.userdata = this;
			desired_spec.callback = audio_callback;

			audio_device = SDL_OpenAudioDevice(
				NULL, 0, &desired_spec, NULL, 0);
			block_bit = 4;
			MD0CON.Setup(0xF2C0, 1, "Buzzer/MD0CON", &control, MMURegion::DefaultRead<uint8_t, 0x1>, MMURegion::DefaultWrite<uint8_t, 0x1>, emulator);
			MD0TMP.Setup(0xF2C1, 1, "Buzzer/MD0TMP", &tempo, MMURegion::DefaultRead<uint8_t, 0x3>, MMURegion::DefaultWrite<uint8_t, 0x3>, emulator);
			MD0TL.Setup(0xF2C2, 2, "Buzzer/MD0TL", &length, MMURegion::DefaultRead<uint16_t, 0xF07>, MMURegion::DefaultWrite<uint16_t, 0xF07>, emulator);
		}
		SDL_AudioDeviceID audio_device{};
		void Initialise() override {
			SDL_PauseAudioDevice(audio_device, 0);
		}
		void Tick() override {
		}
		void Uninitialise() override {
			SDL_PauseAudioDevice(audio_device, 1);
		}
		~AudioDriver() {
			SDL_CloseAudioDevice(audio_device);
		}
	};
	Peripheral* CreateBuzzerDriver(Emulator& emu) {
		return new AudioDriver(emu);
	}
} // namespace casioemu
