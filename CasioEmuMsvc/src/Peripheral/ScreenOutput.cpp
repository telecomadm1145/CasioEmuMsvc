#include "ScreenOutput.hpp"
#include "ScreenRenderSupport.hpp"
#include "Emulator.hpp"
#include "Ext/Random.hpp"
#include "Gui/PopUpDisplay.h"
#include "ModelInfo.h"
#include <SDL_image.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <wingdi.h>
#undef min
#undef max
#endif
#ifdef __ANDROID__
#include <android/api-level.h>
#include <android/log.h>
#include <jni.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>
#include <unistd.h>
#include <fcntl.h>
static bool saveImageToMediaStore(
	const void* pixels,
	int width,
	int height,
	int pitch,
	const char* filename) {
	JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
	jobject activity = (jobject)SDL_AndroidGetActivity();
	if (!env || !activity)
		return false;

	jobject byteBuffer = env->NewDirectByteBuffer((void*)pixels, height * pitch);
	jclass activityClass = env->GetObjectClass(activity);
	jmethodID saveImageMethod = env->GetMethodID(
		activityClass,
		"saveImageToMediaStore",
		"(Ljava/nio/ByteBuffer;IIILjava/lang/String;)Z");
	if (saveImageMethod == NULL) {
		SDL_Log("Error: saveImageToMediaStore method not found. Please add it to your Java activity.");
		env->DeleteLocalRef(byteBuffer);
		env->DeleteLocalRef(activityClass);
		env->DeleteLocalRef(activity);
		return false;
	}

	jstring jfilename = env->NewStringUTF(filename);
	jboolean result = env->CallBooleanMethod(
		activity,
		saveImageMethod,
		byteBuffer,
		width,
		height,
		pitch,
		jfilename);
	env->DeleteLocalRef(jfilename);
	env->DeleteLocalRef(byteBuffer);
	env->DeleteLocalRef(activityClass);
	env->DeleteLocalRef(activity);
	return result;
}
#endif

namespace casioemu {
#if !defined(__EMSCRIPTEN__) && !defined(CASIOEMU_CORE_WEB)
	bool GetCaptureRect(const std::vector<SDL_Rect>& spriteRects, const std::vector<SDL_Rect>& pixelRects, SDL_Rect& captureRect) {
		if (spriteRects.empty() && pixelRects.empty()) {
			return false;
		}

		int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
		auto extendBounds = [&](const SDL_Rect& rect) {
			minX = std::min(minX, rect.x);
			minY = std::min(minY, rect.y);
			maxX = std::max(maxX, rect.x + rect.w);
			maxY = std::max(maxY, rect.y + rect.h);
		};

		for (const auto& rect : spriteRects) {
			extendBounds(rect);
		}
		for (const auto& rect : pixelRects) {
			extendBounds(rect);
		}

		if (maxX <= minX || maxY <= minY) {
			return false;
		}

		captureRect = { minX, minY, maxX - minX, maxY - minY };
		return true;
	}

	std::string MakeTimestampedName(const char* prefix, const char* suffix) {
		std::time_t t = std::time(nullptr);
		std::tm tm = *std::localtime(&t);
		std::ostringstream filename;
		filename << prefix
			<< std::put_time(&tm, "%Y-%m-%d-%H-%M-%S-")
			<< util::Random::uniform_uint32(0, 999)
			<< suffix;
		return filename.str();
	}

	std::filesystem::path GetRecordingOutputPath(const std::string& name) {
#ifdef __ANDROID__
		const char* externalPath = SDL_AndroidGetExternalStoragePath();
		if (externalPath && *externalPath) {
			return std::filesystem::path(externalPath) / "recordings" / name;
		}
#endif
		return std::filesystem::path(name);
	}

	void SaveScreenshotSurface(SDL_Surface* screenSurface, const std::string& filename) {
		if (!screenSurface)
			return;
#ifdef __ANDROID__
		bool success = saveImageToMediaStore(screenSurface->pixels, screenSurface->w, screenSurface->h, screenSurface->pitch, filename.c_str());
		if (!success) {
			SDL_Log("Error saving screenshot using MediaStore API");
		}
		else {
			SDL_Log("Screenshot saved successfully with MediaStore API");
		}

		JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
		jobject activity = (jobject)SDL_AndroidGetActivity();

		if (env && activity) {
			jobject byteBuffer = env->NewDirectByteBuffer(screenSurface->pixels,
				screenSurface->h * screenSurface->pitch);

			jclass activityClass = env->GetObjectClass(activity);
			jmethodID copyToClipboardMethod = env->GetMethodID(activityClass, "copyImageToClipboard",
				"(Ljava/nio/ByteBuffer;III)Z");

			if (copyToClipboardMethod != NULL) {
				jboolean result = env->CallBooleanMethod(activity, copyToClipboardMethod,
					byteBuffer, screenSurface->w,
					screenSurface->h, screenSurface->pitch);
				if (result) {
					SDL_Log("Screenshot copied to clipboard");
				}
				else {
					SDL_Log("Failed to copy screenshot to clipboard");
				}
			}
			else {
				SDL_Log("copyImageToClipboard method not found. Add it to your Java activity.");
			}

			env->DeleteLocalRef(byteBuffer);
			env->DeleteLocalRef(activityClass);
			env->DeleteLocalRef(activity);
		}
#else
		if (IMG_SavePNG(screenSurface, filename.c_str()) != 0) {
			SDL_Log("Error saving screenshot: %s", IMG_GetError());
		}
		else {
			SDL_Log("Screenshot saved to %s", filename.c_str());
		}

#ifdef _WIN32
		HDC hdcScreen = GetDC(NULL);
		HDC hdcMem = CreateCompatibleDC(hdcScreen);

		BITMAPINFOHEADER bi;
		ZeroMemory(&bi, sizeof(BITMAPINFOHEADER));
		bi.biSize = sizeof(BITMAPINFOHEADER);
		bi.biWidth = screenSurface->w;
		bi.biHeight = -screenSurface->h;
		bi.biPlanes = 1;
		bi.biBitCount = 32;
		bi.biCompression = BI_RGB;

		void* bits = NULL;
		HBITMAP hBitmap = CreateDIBSection(hdcMem, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, NULL, 0);

		if (hBitmap) {
			HGDIOBJ oldBitmap = SelectObject(hdcMem, hBitmap);

			uint8_t* dst = (uint8_t*)bits;

			for (int y = 0; y < screenSurface->h; y++) {
				uint8_t* src = (uint8_t*)screenSurface->pixels + y * screenSurface->pitch;
				for (int x = 0; x < screenSurface->w; x++) {
					dst[0] = src[2];
					dst[1] = src[1];
					dst[2] = src[0];
					dst[3] = src[3];

					src += 4;
					dst += 4;
				}
			}
			if (oldBitmap)
				SelectObject(hdcMem, oldBitmap);

			bool clipboardOwnsBitmap = false;
			if (OpenClipboard(NULL)) {
				EmptyClipboard();
				if (SetClipboardData(CF_BITMAP, hBitmap)) {
					clipboardOwnsBitmap = true;
				}
				else {
					SDL_Log("Failed to set clipboard bitmap");
				}
				CloseClipboard();
				if (clipboardOwnsBitmap)
					SDL_Log("Screenshot copied to clipboard");
			}
			else {
				SDL_Log("Failed to open clipboard");
			}
			if (!clipboardOwnsBitmap) {
				DeleteObject(hBitmap);
			}

			DeleteDC(hdcMem);
		}
		else {
			SDL_Log("Failed to create DIB section for clipboard");
		}

		ReleaseDC(NULL, hdcScreen);
#else
		SDL_Log("Clipboard copy not implemented for this platform");
#endif
#endif
	}

	bool EnsureParentDirectory(const std::filesystem::path& path) {
		const auto parent = path.parent_path();
		if (parent.empty()) {
			return true;
		}

		std::error_code ec;
		std::filesystem::create_directories(parent, ec);
		if (ec) {
			SDL_Log("Could not create recording directory %s: %s",
				parent.string().c_str(), ec.message().c_str());
			return false;
		}
		return true;
	}

#ifdef __ANDROID__
	inline uint8_t ClampByte(int value) {
		return static_cast<uint8_t>(std::clamp(value, 0, 255));
	}

	class AndroidVideoEncoder {
	public:
		~AndroidVideoEncoder() {
			Stop();
		}

		bool Start(const std::filesystem::path& path, int videoWidth, int videoHeight, int videoFps) {
			Stop();
			if (android_get_device_api_level() < 21) {
				SDL_Log("Android recording requires API level 21 or newer.");
				return false;
			}
			if (!EnsureParentDirectory(path)) {
				return false;
			}

			width = videoWidth;
			height = videoHeight;
			fps = std::max(1, videoFps);
			frameIndex = 0;

			const std::string pathString = path.string();
			fd = open(pathString.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0666);
			if (fd < 0) {
				SDL_Log("Could not open recording output %s", pathString.c_str());
				return false;
			}

			muxer = AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
			codec = AMediaCodec_createEncoderByType("video/avc");
			if (!muxer || !codec) {
				SDL_Log("Could not create Android media encoder.");
				Stop();
				return false;
			}

			AMediaFormat* format = AMediaFormat_new();
			AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
			AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, width);
			AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, height);
			AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, kColorFormatYuv420SemiPlanar);
			AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, std::max(256000, width * height * fps / 2));
			AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, fps);
			AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 2);

			media_status_t status = AMediaCodec_configure(codec, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
			AMediaFormat_delete(format);
			if (status != AMEDIA_OK) {
				SDL_Log("Could not configure Android media encoder: %d", status);
				Stop();
				return false;
			}

			status = AMediaCodec_start(codec);
			if (status != AMEDIA_OK) {
				SDL_Log("Could not start Android media encoder: %d", status);
				Stop();
				return false;
			}

			started = true;
			return true;
		}

		bool WriteRgbaFrame(const uint8_t* rgba, int pitch) {
			if (!started) {
				return false;
			}

			if (!Drain(false)) {
				return false;
			}

			ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(codec, 10000);
			if (inputIndex < 0) {
				SDL_Log("Android media encoder input buffer was not available.");
				return false;
			}

			size_t inputSize = 0;
			uint8_t* input = AMediaCodec_getInputBuffer(codec, inputIndex, &inputSize);
			const size_t needed = static_cast<size_t>(width) * height * 3 / 2;
			if (!input || inputSize < needed) {
				SDL_Log("Android media encoder input buffer is too small.");
				return false;
			}

			ConvertRgbaToNv12(rgba, pitch, input);
			const int64_t ptsUs = static_cast<int64_t>(frameIndex) * 1000000 / fps;
			media_status_t status = AMediaCodec_queueInputBuffer(codec, inputIndex, 0, needed, ptsUs, 0);
			if (status != AMEDIA_OK) {
				SDL_Log("Could not queue Android media encoder input: %d", status);
				return false;
			}

			++frameIndex;
			return Drain(false);
		}

		void Stop() {
			if (started && codec) {
				ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(codec, 10000);
				if (inputIndex >= 0) {
					const int64_t ptsUs = static_cast<int64_t>(frameIndex) * 1000000 / std::max(1, fps);
					const media_status_t status = AMediaCodec_queueInputBuffer(
						codec, inputIndex, 0, 0, ptsUs, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
					if (status == AMEDIA_OK)
						Drain(true);
					else
						SDL_Log("Could not queue Android media encoder EOS: %d", status);
				}
				AMediaCodec_stop(codec);
			}
			if (codec) {
				AMediaCodec_delete(codec);
				codec = nullptr;
			}
			if (muxer) {
				if (muxerStarted) {
					AMediaMuxer_stop(muxer);
				}
				AMediaMuxer_delete(muxer);
				muxer = nullptr;
			}
			if (fd >= 0) {
				close(fd);
				fd = -1;
			}

			started = false;
			muxerStarted = false;
			trackIndex = -1;
			frameIndex = 0;
		}

		bool IsOpen() const {
			return started;
		}

	private:
		void ConvertRgbaToNv12(const uint8_t* rgba, int pitch, uint8_t* yuv) const {
			uint8_t* yPlane = yuv;
			uint8_t* uvPlane = yuv + static_cast<size_t>(width) * height;

			for (int y = 0; y < height; ++y) {
				const uint8_t* row = rgba + static_cast<size_t>(y) * pitch;
				for (int x = 0; x < width; ++x) {
					const uint8_t* px = row + x * 4;
					const int r = px[0];
					const int g = px[1];
					const int b = px[2];
					yPlane[y * width + x] = ClampByte(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
				}
			}

			for (int y = 0; y < height; y += 2) {
				for (int x = 0; x < width; x += 2) {
					int uSum = 0;
					int vSum = 0;
					for (int yy = 0; yy < 2; ++yy) {
						const uint8_t* row = rgba + static_cast<size_t>(y + yy) * pitch;
						for (int xx = 0; xx < 2; ++xx) {
							const uint8_t* px = row + (x + xx) * 4;
							const int r = px[0];
							const int g = px[1];
							const int b = px[2];
							uSum += ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
							vSum += ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
						}
					}

					const size_t uvIndex = static_cast<size_t>(y / 2) * width + x;
					uvPlane[uvIndex] = ClampByte(uSum / 4);
					uvPlane[uvIndex + 1] = ClampByte(vSum / 4);
				}
			}
		}

		bool Drain(bool endOfStream) {
			// A per-dequeue timeout does not bound repeated retries or output
			// that never carries EOS. Keep shutdown bounded across the whole loop.
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
			while (true) {
				if (endOfStream && std::chrono::steady_clock::now() >= deadline) {
					SDL_Log("Timed out waiting for Android media encoder EOS.");
					return false;
				}
				AMediaCodecBufferInfo info{};
				ssize_t outputIndex = AMediaCodec_dequeueOutputBuffer(codec, &info, endOfStream ? 10000 : 0);
				if (outputIndex >= 0) {
					if ((info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0) {
						info.size = 0;
					}

					if (info.size > 0) {
						if (!muxerStarted) {
							SDL_Log("Android media encoder produced data before muxer was ready.");
							return false;
						}

						size_t outputSize = 0;
						uint8_t* output = AMediaCodec_getOutputBuffer(codec, outputIndex, &outputSize);
						if (!output || static_cast<size_t>(info.offset + info.size) > outputSize) {
							SDL_Log("Android media encoder output buffer is invalid.");
							AMediaCodec_releaseOutputBuffer(codec, outputIndex, false);
							return false;
						}
						AMediaCodecBufferInfo sampleInfo = info;
						sampleInfo.offset = 0;
						media_status_t status = AMediaMuxer_writeSampleData(muxer, trackIndex, output + info.offset, &sampleInfo);
						if (status != AMEDIA_OK) {
							SDL_Log("Could not write Android media sample: %d", status);
							AMediaCodec_releaseOutputBuffer(codec, outputIndex, false);
							return false;
						}
					}

					AMediaCodec_releaseOutputBuffer(codec, outputIndex, false);
					if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
						return true;
					}
				}
				else if (outputIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
					AMediaFormat* outputFormat = AMediaCodec_getOutputFormat(codec);
					trackIndex = AMediaMuxer_addTrack(muxer, outputFormat);
					AMediaFormat_delete(outputFormat);
					if (trackIndex < 0 || AMediaMuxer_start(muxer) != AMEDIA_OK) {
						SDL_Log("Could not start Android media muxer.");
						return false;
					}
					muxerStarted = true;
				}
				else if (outputIndex == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
					if (endOfStream) {
						continue;
					}
					return true;
				}
				else {
					return true;
				}
			}
		}

		static constexpr int32_t kColorFormatYuv420SemiPlanar = 21;

		AMediaCodec* codec = nullptr;
		AMediaMuxer* muxer = nullptr;
		int fd = -1;
		int trackIndex = -1;
		bool started = false;
		bool muxerStarted = false;
		int width = 0;
		int height = 0;
		int fps = 30;
		int64_t frameIndex = 0;
	};
#endif

#ifndef __ANDROID__
	class RawVideoPipe {
	public:
		~RawVideoPipe() {
			Stop();
		}

		bool Start(const std::string& command) {
			Stop();
#ifdef _WIN32
			SECURITY_ATTRIBUTES securityAttrs{};
			securityAttrs.nLength = sizeof(securityAttrs);
			securityAttrs.bInheritHandle = TRUE;

			HANDLE stdinRead = nullptr;
			if (!CreatePipe(&stdinRead, &stdinWrite, &securityAttrs, 0)) {
				return false;
			}
			if (!SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0)) {
				CloseHandle(stdinRead);
				CloseHandle(stdinWrite);
				stdinWrite = nullptr;
				return false;
			}

			HANDLE nullOutput = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
				&securityAttrs, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

			STARTUPINFOA startupInfo{};
			startupInfo.cb = sizeof(startupInfo);
			startupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
			startupInfo.wShowWindow = SW_HIDE;
			startupInfo.hStdInput = stdinRead;
			startupInfo.hStdOutput = nullOutput != INVALID_HANDLE_VALUE ? nullOutput : GetStdHandle(STD_OUTPUT_HANDLE);
			startupInfo.hStdError = nullOutput != INVALID_HANDLE_VALUE ? nullOutput : GetStdHandle(STD_ERROR_HANDLE);

			PROCESS_INFORMATION processInfo{};
			std::string mutableCommand = command;
			BOOL created = CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
				CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo);

			CloseHandle(stdinRead);
			if (nullOutput != INVALID_HANDLE_VALUE) {
				CloseHandle(nullOutput);
			}

			if (!created) {
				CloseHandle(stdinWrite);
				stdinWrite = nullptr;
				return false;
			}

			processHandle = processInfo.hProcess;
			CloseHandle(processInfo.hThread);
			return true;
#else
			pipe = ::popen(command.c_str(), "w");
			return pipe != nullptr;
#endif
		}

		bool Write(const uint8_t* data, size_t size) {
#ifdef _WIN32
			if (!stdinWrite) {
				return false;
			}

			size_t offset = 0;
			while (offset < size) {
				DWORD chunk = static_cast<DWORD>(std::min<size_t>(size - offset, 1 << 20));
				DWORD written = 0;
				if (!WriteFile(stdinWrite, data + offset, chunk, &written, nullptr) || written == 0) {
					return false;
				}
				offset += written;
			}
			return true;
#else
			if (!pipe) {
				return false;
			}
			return std::fwrite(data, 1, size, pipe) == size;
#endif
		}

		void Stop() {
#ifdef _WIN32
			if (stdinWrite) {
				CloseHandle(stdinWrite);
				stdinWrite = nullptr;
			}
			if (processHandle) {
				DWORD waitResult = WaitForSingleObject(processHandle, 5000);
				if (waitResult == WAIT_TIMEOUT) {
					SDL_Log("Timed out while finalizing recording; terminating ffmpeg.");
					TerminateProcess(processHandle, 1);
				}
				CloseHandle(processHandle);
				processHandle = nullptr;
			}
#else
			if (pipe) {
				::pclose(pipe);
				pipe = nullptr;
			}
#endif
		}

		bool IsOpen() const {
#ifdef _WIN32
			return stdinWrite != nullptr;
#else
			return pipe != nullptr;
#endif
		}

	private:
#ifdef _WIN32
		HANDLE stdinWrite = nullptr;
		HANDLE processHandle = nullptr;
#else
		FILE* pipe = nullptr;
#endif
	};
#endif

	SDL_Color CaptureBackgroundColour(uint32_t rgb) {
		return {
			static_cast<Uint8>((rgb >> 16) & 0xff),
			static_cast<Uint8>((rgb >> 8) & 0xff),
			static_cast<Uint8>(rgb & 0xff),
			255};
	}

	Uint32 MapScreenshotPixel(SDL_PixelFormat* format, const ColourInfo& ink_colour, const SDL_Color& background, float alpha_value) {
		if (alpha_value > 255.0f) {
			const SDL_Color colour = ScreenPixelColour(ink_colour, alpha_value);
			return SDL_MapRGBA(format, colour.r, colour.g, colour.b, colour.a);
		}

		const int alpha = std::clamp(static_cast<int>(std::lround(alpha_value)), 0, 255);
		const auto blend = [alpha](int foreground, int background_channel) {
			return static_cast<uint8_t>((foreground * alpha + background_channel * (255 - alpha) + 127) / 255);
		};
		return SDL_MapRGBA(format,
			blend(ink_colour.r, background.r),
			blend(ink_colour.g, background.g),
			blend(ink_colour.b, background.b),
			255);
	}

	void FillScaledPixel(SDL_Surface* surface, int x, int y, int scale, Uint32 colour) {
		for (int dy = 0; dy < scale; ++dy) {
			const int py = y + dy;
			if (py < 0 || py >= surface->h)
				continue;
			auto* row = reinterpret_cast<Uint32*>(static_cast<uint8_t*>(surface->pixels) + py * surface->pitch);
			for (int dx = 0; dx < scale; ++dx) {
				const int px = x + dx;
				if (px >= 0 && px < surface->w)
					row[px] = colour;
			}
		}
	}

	SDL_Rect ScaleScreenshotRect(const SDL_Rect& rect, const SDL_Rect& capture_rect, double sx, double sy) {
		const int x0 = static_cast<int>(std::floor((rect.x - capture_rect.x) * sx));
		const int y0 = static_cast<int>(std::floor((rect.y - capture_rect.y) * sy));
		const int x1 = static_cast<int>(std::ceil((rect.x + rect.w - capture_rect.x) * sx));
		const int y1 = static_cast<int>(std::ceil((rect.y + rect.h - capture_rect.y) * sy));
		return {x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
	}

	Rect ToModelRect(const SDL_Rect& rect) {
		return {rect.x, rect.y, rect.w, rect.h};
	}

	struct ScreenCaptureSource {
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

	struct ScreenCaptureLayout {
		SDL_Rect capture_rect{};
		SDL_Rect scaled_lcd{};
		int scale = 3;
		int content_width = 0;
		int content_height = 0;
		int output_width = 0;
		int output_height = 0;
		double sx = 1.0;
		double sy = 1.0;
	};

	bool BuildScreenCaptureLayout(const ScreenCaptureSource& source, int requested_scale, bool even_output, ScreenCaptureLayout& layout, const char* purpose) {
		if (!source.sprite_info || !source.sprite_available ||
			!source.ink_colour || !source.screen_ink_alpha ||
			source.logical_width <= 0 || source.logical_height <= 0 || source.lcd_dest.w <= 0 || source.lcd_dest.h <= 0) {
			SDL_Log("%s failed: invalid capture source: texture=%p sprites=%p available=%p ink=%p alpha=%p logical=%dx%d lcd=%d,%d %dx%d.",
				purpose,
				static_cast<void*>(source.interface_texture),
				static_cast<const void*>(source.sprite_info),
				static_cast<const void*>(source.sprite_available),
				static_cast<const void*>(source.ink_colour),
				static_cast<const void*>(source.screen_ink_alpha),
				source.logical_width,
				source.logical_height,
				source.lcd_dest.x,
				source.lcd_dest.y,
				source.lcd_dest.w,
				source.lcd_dest.h);
			return false;
		}

		std::vector<SDL_Rect> spriteRects;
		for (size_t ix = 1; ix < source.sprite_info->size() && ix < source.sprite_available->size(); ++ix) {
			if (!(*source.sprite_available)[ix])
				continue;
			spriteRects.push_back((*source.sprite_info)[ix].dest);
		}

		layout = {};
		if (!GetCaptureRect(spriteRects, std::vector<SDL_Rect>{source.lcd_dest}, layout.capture_rect)) {
			SDL_Log("%s failed: invalid capture region.", purpose);
			return false;
		}

		layout.scale = std::max(1, requested_scale);
		layout.sx = static_cast<double>(source.logical_width * layout.scale) / static_cast<double>(source.lcd_dest.w);
		layout.sy = static_cast<double>(source.logical_height * layout.scale) / static_cast<double>(source.lcd_dest.h);
		layout.content_width = std::max(1, static_cast<int>(std::ceil(layout.capture_rect.w * layout.sx)));
		layout.content_height = std::max(1, static_cast<int>(std::ceil(layout.capture_rect.h * layout.sy)));
		layout.output_width = even_output ? ((layout.content_width + 1) & ~1) : layout.content_width;
		layout.output_height = even_output ? ((layout.content_height + 1) & ~1) : layout.content_height;
		layout.scaled_lcd = ScaleScreenshotRect(source.lcd_dest, layout.capture_rect, layout.sx, layout.sy);
		return true;
	}

	class ScreenCaptureComposer {
	public:
		~ScreenCaptureComposer() {
			Reset();
		}

		void Reset() {
			if (target) {
				SDL_DestroyTexture(target);
				target = nullptr;
			}
			target_width = 0;
			target_height = 0;
		}

		bool Render(SDL_Renderer* renderer, const ScreenCaptureSource& source, const ScreenCaptureLayout& layout, SDL_Surface* surface, const SDL_Color& background, const char* purpose) {
			if (!renderer || !surface || surface->w != layout.output_width || surface->h != layout.output_height) {
				SDL_Log("%s failed: invalid capture surface.", purpose);
				return false;
			}
			if (!EnsureTarget(renderer, layout.output_width, layout.output_height, purpose)) {
				return false;
			}

			SDL_Texture* old_target = SDL_GetRenderTarget(renderer);
			SDL_Rect old_viewport{};
			SDL_Rect old_clip{};
			float old_scale_x = 1.0f;
			float old_scale_y = 1.0f;
			SDL_BlendMode old_blend_mode{};
			SDL_RenderGetViewport(renderer, &old_viewport);
			SDL_RenderGetClipRect(renderer, &old_clip);
			const SDL_bool old_clip_enabled = SDL_RenderIsClipEnabled(renderer);
			SDL_RenderGetScale(renderer, &old_scale_x, &old_scale_y);
			SDL_GetRenderDrawBlendMode(renderer, &old_blend_mode);
			bool render_target_active = false;
			auto restore = [&]() {
				if (!render_target_active)
					return;
				SDL_SetRenderTarget(renderer, old_target);
				SDL_RenderSetViewport(renderer, &old_viewport);
				SDL_RenderSetClipRect(renderer, old_clip_enabled ? &old_clip : nullptr);
				SDL_RenderSetScale(renderer, old_scale_x, old_scale_y);
				SDL_SetRenderDrawBlendMode(renderer, old_blend_mode);
				render_target_active = false;
			};

			if (SDL_SetRenderTarget(renderer, target) != 0) {
				SDL_Log("%s failed: cannot bind render target: %s", purpose, SDL_GetError());
				return false;
			}
			render_target_active = true;
			SDL_RenderSetViewport(renderer, nullptr);
			SDL_RenderSetClipRect(renderer, nullptr);
			SDL_RenderSetScale(renderer, 1.0f, 1.0f);
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
			SDL_SetRenderDrawColor(renderer, background.r, background.g, background.b, background.a);
			SDL_RenderClear(renderer);

			if (svg_texture_cache.size() < source.sprite_info->size())
				svg_texture_cache.resize(source.sprite_info->size());
			for (size_t ix = 1; ix < source.sprite_info->size() && ix < source.sprite_available->size(); ++ix) {
				if (!(*source.sprite_available)[ix])
					continue;
				const int alpha_index = static_cast<int>(ix - 1);
				SpriteInfo sprite = (*source.sprite_info)[ix];
				sprite.dest = ToModelRect(ScaleScreenshotRect(sprite.dest, layout.capture_rect, layout.sx, layout.sy));
				const uint8_t alpha = Uint8(std::clamp(static_cast<int>(source.screen_ink_alpha[alpha_index]), 0, 255));
				RenderModelSprite(renderer, source.interface_texture, &svg_texture_cache[ix], sprite, *source.ink_colour, alpha);
			}

			if (SDL_MUSTLOCK(surface) && SDL_LockSurface(surface) != 0) {
				SDL_Log("%s failed: cannot lock capture surface: %s", purpose, SDL_GetError());
				restore();
				return false;
			}

			if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, surface->pixels, surface->pitch) != 0) {
				SDL_Log("%s failed: cannot read capture target pixels: %s", purpose, SDL_GetError());
				if (SDL_MUSTLOCK(surface))
					SDL_UnlockSurface(surface);
				restore();
				return false;
			}
			restore();

			if (source.render_pixel_layer) {
				for (int y = 0; y < source.logical_height; ++y) {
					const int source_y = y + 1;
					for (int x = 0; x < source.logical_width; ++x) {
						const float alpha_value = source.screen_ink_alpha[x + source_y * 192];
						if (alpha_value <= 0.0f)
							continue;
						FillScaledPixel(surface, layout.scaled_lcd.x + x * layout.scale, layout.scaled_lcd.y + y * layout.scale, layout.scale, MapScreenshotPixel(surface->format, *source.ink_colour, background, alpha_value));
					}
				}
			}
			if (SDL_MUSTLOCK(surface))
				SDL_UnlockSurface(surface);
			return true;
		}

	private:
		bool EnsureTarget(SDL_Renderer* renderer, int width, int height, const char* purpose) {
			if (target && target_width == width && target_height == height)
				return true;
			Reset();
			target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width, height);
			if (!target) {
				SDL_Log("%s failed: cannot create render target: %s", purpose, SDL_GetError());
				return false;
			}
			SDL_SetTextureBlendMode(target, SDL_BLENDMODE_NONE);
			target_width = width;
			target_height = height;
			return true;
		}

		SDL_Texture* target = nullptr;
		int target_width = 0;
		int target_height = 0;
		std::vector<SvgSpriteTextureCache> svg_texture_cache;
	};

	class ScreenRecorder {
	public:
		~ScreenRecorder() {
			Stop();
		}

		bool Start(const ScreenCaptureSource& source, int capture_scale, int requestedFps = 30) {
			Stop();
			if (!BuildScreenCaptureLayout(source, capture_scale, true, layout, "Recording")) {
				return false;
			}

			fps = std::max(1, requestedFps);
			outputWidth = layout.output_width;
			outputHeight = layout.output_height;
			frameCount = 0;
			nextCaptureTick = 0;
			if (!EnsureFrameSurface()) {
				return false;
			}

			const std::string stem = MakeTimestampedName("recording-", "");
			outputPath = GetRecordingOutputPath(stem + ".mp4");
#ifdef __ANDROID__
			if (encoder.Start(outputPath, outputWidth, outputHeight, fps)) {
				frameSequence = false;
				recording = true;
				SDL_Log("Recording started: %s", outputPath.string().c_str());
				return true;
			}
#else
			const std::string command = BuildFfmpegCommand(outputPath);
			if (encoder.Start(command)) {
				frameSequence = false;
				recording = true;
				SDL_Log("Recording started: %s", outputPath.string().c_str());
				return true;
			}
#endif

			frameSequence = true;
			frameDirectory = GetRecordingOutputPath(stem + "-frames");
			std::error_code ec;
			std::filesystem::create_directories(frameDirectory, ec);
			if (ec) {
				SDL_Log("Recording failed: cannot create frame directory %s (%s)",
					frameDirectory.string().c_str(), ec.message().c_str());
				ResetFrameSurface();
				return false;
			}

			recording = true;
#ifdef __ANDROID__
			SDL_Log("Android video encoder was not available; recording PNG frames to %s", frameDirectory.string().c_str());
#else
			SDL_Log("ffmpeg was not available; recording PNG frames to %s", frameDirectory.string().c_str());
#endif
			return true;
		}

		void Stop() {
			if (!recording && !encoder.IsOpen()) {
				ResetFrameSurface();
				composer.Reset();
				return;
			}
			encoder.Stop();
			if (recording) {
				if (frameSequence) {
					SDL_Log("Recording stopped: %u frames saved to %s",
						frameCount, frameDirectory.string().c_str());
				}
				else {
					SDL_Log("Recording stopped: %u frames saved to %s",
						frameCount, outputPath.string().c_str());
				}
			}
			recording = false;
			ResetFrameSurface();
			composer.Reset();
		}

		bool CaptureFrame(SDL_Renderer* renderer, const ScreenCaptureSource& source, const SDL_Color& background) {
			if (!recording) {
				return false;
			}

			Uint64 now = SDL_GetTicks64();
			if (nextCaptureTick != 0 && now < nextCaptureTick) {
				return true;
			}
			nextCaptureTick = now + static_cast<Uint64>(1000 / fps);

			if (!frameSurface || !composer.Render(renderer, source, layout, frameSurface, background, "Recording")) {
				Stop();
				return false;
			}

			bool success = frameSequence
				? SaveFrameAsPng()
#ifdef __ANDROID__
				: encoder.WriteRgbaFrame(framePixels.data(), frameSurface->pitch);
#else
				: encoder.Write(framePixels.data(), framePixels.size());
#endif
			if (!success) {
				SDL_Log("Recording stopped because frame writing failed.");
				Stop();
				return false;
			}

			++frameCount;
			return true;
		}

		bool IsRecording() const {
			return recording;
		}

		unsigned int FrameCount() const {
			return frameCount;
		}

	private:
		std::string BuildFfmpegCommand(const std::filesystem::path& path) const {
			std::ostringstream command;
			command << "ffmpeg -y -hide_banner -loglevel error"
				<< " -f rawvideo -vcodec rawvideo"
				<< " -pixel_format rgba"
				<< " -video_size " << outputWidth << "x" << outputHeight
				<< " -framerate " << fps
				<< " -i - -an -c:v mpeg4 -q:v 3 -pix_fmt yuv420p "
				<< "\"" << path.string() << "\"";
			return command.str();
		}

		bool EnsureFrameSurface() {
			ResetFrameSurface();
			const int pitch = outputWidth * 4;
			framePixels.assign(static_cast<size_t>(pitch) * outputHeight, 255);
			frameSurface = SDL_CreateRGBSurfaceWithFormatFrom(
				framePixels.data(),
				outputWidth,
				outputHeight,
				32,
				pitch,
				SDL_PIXELFORMAT_RGBA32);
			if (!frameSurface) {
				SDL_Log("Recording failed: cannot create frame surface: %s", SDL_GetError());
				framePixels.clear();
				return false;
			}
			return true;
		}

		void ResetFrameSurface() {
			if (frameSurface) {
				SDL_FreeSurface(frameSurface);
				frameSurface = nullptr;
			}
			framePixels.clear();
		}

		bool SaveFrameAsPng() const {
			std::ostringstream filename;
			filename << "frame-" << std::setw(6) << std::setfill('0') << frameCount << ".png";
			std::filesystem::path framePath = frameDirectory / filename.str();

			const std::string pathString = framePath.string();
			int result = IMG_SavePNG(frameSurface, pathString.c_str());
			if (result != 0) {
				SDL_Log("Error saving recording frame: %s", IMG_GetError());
				return false;
			}
			return true;
		}

#ifdef __ANDROID__
		AndroidVideoEncoder encoder;
#else
		RawVideoPipe encoder;
#endif
		ScreenCaptureLayout layout{};
		ScreenCaptureComposer composer;
		std::vector<uint8_t> framePixels;
		SDL_Surface* frameSurface = nullptr;
		int fps = 30;
		int outputWidth = 0;
		int outputHeight = 0;
		Uint64 nextCaptureTick = 0;
		unsigned int frameCount = 0;
		bool recording = false;
		bool frameSequence = false;
		std::filesystem::path outputPath;
		std::filesystem::path frameDirectory;
	};

	class ScreenMirrorComposer {
	public:
		~ScreenMirrorComposer() {
			Reset();
		}

		void Reset() {
			if (pixel_texture) {
				SDL_DestroyTexture(pixel_texture);
				pixel_texture = nullptr;
			}
			if (interface_texture) {
				SDL_DestroyTexture(interface_texture);
				interface_texture = nullptr;
			}
			pixel_width = 0;
			pixel_height = 0;
			interface_surface = nullptr;
			pixel_pixels.clear();
			svg_texture_cache.clear();
		}

		bool Render(ScreenMirror& mirror, const ScreenCaptureSource& source, const SDL_Color& background) {
			SDL_Renderer* mirror_renderer = mirror.renderer();
			if (!mirror_renderer)
				return false;

			SDL_Rect capture_rect{};
			if (!BuildCaptureRect(source, capture_rect)) {
				SDL_Log("Mirror update failed: invalid capture region.");
				return false;
			}

			mirror.clear(background);
			const SDL_Rect content_rect = mirror.contentRect();
			if (content_rect.w <= 0 || content_rect.h <= 0)
				return false;

			const double sx = static_cast<double>(content_rect.w) / static_cast<double>(capture_rect.w);
			const double sy = static_cast<double>(content_rect.h) / static_cast<double>(capture_rect.h);
			SDL_Texture* fallback_texture = EnsureInterfaceTexture(mirror_renderer, source);

			if (svg_texture_cache.size() < source.sprite_info->size())
				svg_texture_cache.resize(source.sprite_info->size());
			for (size_t ix = 1; ix < source.sprite_info->size() && ix < source.sprite_available->size(); ++ix) {
				if (!(*source.sprite_available)[ix])
					continue;
				const int alpha_index = static_cast<int>(ix - 1);
				SpriteInfo sprite = (*source.sprite_info)[ix];
				SDL_Rect dest = ScaleScreenshotRect(sprite.dest, capture_rect, sx, sy);
				dest.x += content_rect.x;
				dest.y += content_rect.y;
				sprite.dest = ToModelRect(dest);
				const uint8_t alpha = Uint8(std::clamp(static_cast<int>(source.screen_ink_alpha[alpha_index]), 0, 255));
				RenderModelSprite(mirror_renderer, fallback_texture, &svg_texture_cache[ix], sprite, *source.ink_colour, alpha);
			}

			if (source.render_pixel_layer) {
				SDL_Rect lcd_dest = ScaleScreenshotRect(source.lcd_dest, capture_rect, sx, sy);
				lcd_dest.x += content_rect.x;
				lcd_dest.y += content_rect.y;
				if (!RenderPixelTexture(mirror_renderer, source, lcd_dest))
					return false;
			}
			mirror.present();
			return true;
		}

	private:
		bool BuildCaptureRect(const ScreenCaptureSource& source, SDL_Rect& capture_rect) const {
			if (!source.sprite_info || !source.sprite_available || !source.ink_colour || !source.screen_ink_alpha ||
				source.logical_width <= 0 || source.logical_height <= 0 || source.lcd_dest.w <= 0 || source.lcd_dest.h <= 0)
				return false;

			std::vector<SDL_Rect> sprite_rects;
			for (size_t ix = 1; ix < source.sprite_info->size() && ix < source.sprite_available->size(); ++ix) {
				if ((*source.sprite_available)[ix])
					sprite_rects.push_back((*source.sprite_info)[ix].dest);
			}
			return GetCaptureRect(sprite_rects, std::vector<SDL_Rect>{source.lcd_dest}, capture_rect);
		}

		SDL_Texture* EnsureInterfaceTexture(SDL_Renderer* renderer, const ScreenCaptureSource& source) {
			if (!renderer || !source.interface_surface)
				return nullptr;
			if (interface_texture && interface_surface == source.interface_surface)
				return interface_texture;
			if (interface_texture) {
				SDL_DestroyTexture(interface_texture);
				interface_texture = nullptr;
			}
			interface_surface = source.interface_surface;
			interface_texture = SDL_CreateTextureFromSurface(renderer, source.interface_surface);
			if (!interface_texture) {
				SDL_Log("Mirror update failed: cannot create interface texture: %s", SDL_GetError());
				interface_surface = nullptr;
				return nullptr;
			}
			SDL_SetTextureBlendMode(interface_texture, SDL_BLENDMODE_BLEND);
			return interface_texture;
		}

		bool EnsurePixelTexture(SDL_Renderer* renderer, int width, int height) {
			if (!renderer || width <= 0 || height <= 0)
				return false;
			if (pixel_texture && pixel_width == width && pixel_height == height)
				return true;
			if (pixel_texture) {
				SDL_DestroyTexture(pixel_texture);
				pixel_texture = nullptr;
			}
			pixel_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height);
			if (!pixel_texture) {
				SDL_Log("Mirror update failed: cannot create pixel texture: %s", SDL_GetError());
				pixel_width = 0;
				pixel_height = 0;
				pixel_pixels.clear();
				return false;
			}
			SDL_SetTextureBlendMode(pixel_texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
			SDL_SetTextureScaleMode(pixel_texture, SDL_ScaleModeNearest);
#endif
			pixel_width = width;
			pixel_height = height;
			pixel_pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
			return true;
		}

		bool RenderPixelTexture(SDL_Renderer* renderer, const ScreenCaptureSource& source, const SDL_Rect& dest) {
			if (!EnsurePixelTexture(renderer, source.logical_width, source.logical_height))
				return false;
			for (int y = 0; y < source.logical_height; ++y) {
				const int source_y = y + 1;
				for (int x = 0; x < source.logical_width; ++x) {
					const SDL_Color colour = ScreenPixelColour(*source.ink_colour, source.screen_ink_alpha[x + source_y * 192]);
					const size_t pixel_offset = (static_cast<size_t>(y) * static_cast<size_t>(source.logical_width) + static_cast<size_t>(x)) * 4;
					pixel_pixels[pixel_offset + 0] = colour.r;
					pixel_pixels[pixel_offset + 1] = colour.g;
					pixel_pixels[pixel_offset + 2] = colour.b;
					pixel_pixels[pixel_offset + 3] = colour.a;
				}
			}
			if (SDL_UpdateTexture(pixel_texture, nullptr, pixel_pixels.data(), source.logical_width * 4) != 0) {
				SDL_Log("Mirror update failed: cannot update pixel texture: %s", SDL_GetError());
				return false;
			}
			if (SDL_RenderCopy(renderer, pixel_texture, nullptr, &dest) != 0) {
				SDL_Log("Mirror update failed: cannot render pixel texture: %s", SDL_GetError());
				return false;
			}
			return true;
		}

		SDL_Texture* pixel_texture = nullptr;
		int pixel_width = 0;
		int pixel_height = 0;
		std::vector<uint8_t> pixel_pixels;
		SDL_Surface* interface_surface = nullptr;
		SDL_Texture* interface_texture = nullptr;
		std::vector<SvgSpriteTextureCache> svg_texture_cache;
	};

	bool CapturePixelPerfectScreenshot(
		SDL_Renderer* renderer,
		SDL_Texture* interface_texture,
		SDL_Surface* interface_surface,
		const std::vector<SpriteInfo>& sprite_info,
		const std::vector<uint8_t>& sprite_available,
		const ColourInfo& ink_colour,
		const float* screen_ink_alpha,
		int logical_width,
		int logical_height,
		const SDL_Rect& lcd_dest,
		int capture_scale,
		const SDL_Color& background,
		bool render_pixel_layer = true) {
		ScreenCaptureSource source{
			interface_texture,
			interface_surface,
			&sprite_info,
			&sprite_available,
			&ink_colour,
			screen_ink_alpha,
			logical_width,
			logical_height,
			lcd_dest,
			render_pixel_layer};

		ScreenCaptureLayout layout{};
		if (!BuildScreenCaptureLayout(source, capture_scale, false, layout, "Screenshot"))
			return false;

		SDL_Surface* screenSurface = SDL_CreateRGBSurfaceWithFormat(0, layout.output_width, layout.output_height, 32, SDL_PIXELFORMAT_RGBA32);
		if (!screenSurface) {
			SDL_Log("Error creating screenshot surface: %s", SDL_GetError());
			return false;
		}

		ScreenCaptureComposer composer;
		if (!composer.Render(renderer, source, layout, screenSurface, background, "Screenshot")) {
			SDL_FreeSurface(screenSurface);
			return false;
		}
		SaveScreenshotSurface(screenSurface, MakeTimestampedName("screenshot-", ".png"));
		SDL_FreeSurface(screenSurface);
		return true;
	}

	std::pair<int, int> GetSize(const ScreenCaptureSource& source) {
		SDL_Rect captureRect{};
		if (!source.sprite_info || !source.sprite_available)
			return {0, 0};
		std::vector<SDL_Rect> spriteRects;
		for (size_t ix = 1; ix < source.sprite_info->size() && ix < source.sprite_available->size(); ++ix) {
			if ((*source.sprite_available)[ix])
				spriteRects.push_back((*source.sprite_info)[ix].dest);
		}
		if (!GetCaptureRect(spriteRects, std::vector<SDL_Rect>{source.lcd_dest}, captureRect)) {
			return { 0, 0 };
		}
		return { captureRect.w, captureRect.h };
	}
#endif

#if !defined(__EMSCRIPTEN__) && !defined(CASIOEMU_CORE_WEB)
struct ScreenOutput::Impl {
	Emulator& emulator;
	ScreenRecorder recorder;
	ScreenMirror* mirror = nullptr;
	ScreenMirrorComposer mirror_composer;

	explicit Impl(Emulator& emu) : emulator(emu) {}
	~Impl() {
		Stop();
	}

	void Stop() {
		recorder.Stop();
		mirror_composer.Reset();
		delete mirror;
		mirror = nullptr;
		emulator.recording_active.store(false);
	}
};
#else
struct ScreenOutput::Impl {
	explicit Impl(Emulator&) {}
	void Stop() {}
};
#endif

ScreenOutput::ScreenOutput(Emulator& emulator)
	: impl(std::make_unique<Impl>(emulator)) {}

ScreenOutput::~ScreenOutput() = default;

void ScreenOutput::Stop() {
	if (impl)
		impl->Stop();
}

void ScreenOutput::Render(const ScreenOutputFrame& frame) {
#if !defined(__EMSCRIPTEN__) && !defined(CASIOEMU_CORE_WEB)
	if (!impl || !frame.sprite_info || !frame.sprite_available || !frame.ink_colour)
		return;

	ScreenCaptureSource source{
		frame.interface_texture,
		frame.interface_surface,
		frame.sprite_info,
		frame.sprite_available,
		frame.ink_colour,
		frame.screen_ink_alpha,
		frame.logical_width,
		frame.logical_height,
		frame.lcd_dest,
		frame.render_pixel_layer};
	const SDL_Color capture_background =
		CaptureBackgroundColour(impl->emulator.capture_background_rgb.load());

	if (impl->emulator.screenshot_requested.load()) {
		CapturePixelPerfectScreenshot(
			frame.renderer,
			frame.interface_texture,
			frame.interface_surface,
			*frame.sprite_info,
			*frame.sprite_available,
			*frame.ink_colour,
			frame.screen_ink_alpha,
			frame.logical_width,
			frame.logical_height,
			frame.lcd_dest,
			impl->emulator.capture_scale.load(),
			capture_background,
			frame.render_pixel_layer);
		impl->emulator.screenshot_requested.store(false);
	}

	if (impl->emulator.recording_requested.exchange(false)) {
		if (!impl->recorder.IsRecording()) {
			if (impl->recorder.Start(
				source,
				impl->emulator.capture_scale.load(),
				30)) {
				impl->emulator.recording_frame_count.store(0);
				impl->emulator.recording_active.store(true);
			}
			else {
				impl->emulator.recording_active.store(false);
			}
		}
	}

	if (impl->emulator.recording_stop_requested.exchange(false)) {
		impl->recorder.Stop();
		impl->emulator.recording_active.store(false);
	}

	if (impl->recorder.IsRecording()) {
		if (impl->recorder.CaptureFrame(
				frame.renderer,
				source,
				capture_background)) {
			impl->emulator.recording_active.store(true);
			impl->emulator.recording_frame_count.store(impl->recorder.FrameCount());
		}
		else {
			impl->emulator.recording_active.store(false);
		}
	}
	else {
		impl->emulator.recording_active.store(false);
	}

	if (impl->emulator.mirroring_requested.load()) {
		auto size = GetSize(source);
		if (impl->mirror) {
			impl->mirror_composer.Reset();
			delete impl->mirror;
			impl->mirror = nullptr;
		}
		if (size.first > 0 && size.second > 0) {
			auto mirror = new ScreenMirror(size.first, size.second);
			if (mirror->create())
				impl->mirror = mirror;
			else
				delete mirror;
		}
		impl->emulator.mirroring_requested.store(false);
	}

	if (impl->mirror) {
		if (impl->mirror->handleEvents()) {
			impl->mirror_composer.Render(*impl->mirror, source, capture_background);
		}
		else {
			impl->mirror_composer.Reset();
			delete impl->mirror;
			impl->mirror = nullptr;
		}
	}
#else
	(void)frame;
#endif
}
} // namespace casioemu
