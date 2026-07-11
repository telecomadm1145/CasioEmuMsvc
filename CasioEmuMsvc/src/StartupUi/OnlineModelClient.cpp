#include "OnlineModelClient.h"
#include <OnlineBuildConfig.h>

#include "../../../McpPlugin/json.hpp"
#include "Config.hpp"

#include <algorithm>
#ifndef __ANDROID__
#include <curl/curl.h>
#else
#include <jni.h>
#include <SDL_system.h>
#endif
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cctype>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(CASIOEMU_HAS_LIBSECRET)
#include <libsecret/secret.h>
#endif
#endif

namespace casioemu {
	namespace {
		using json = nlohmann::json;
		constexpr std::size_t kMaxOnlineResponseSize = 40ull * 1024 * 1024;

		struct HttpResponse {
			unsigned long status{};
			std::vector<std::uint8_t> body;
		};

#ifndef __ANDROID__
		size_t CurlWrite(void* data, size_t size, size_t count, void* user_data) {
			auto& output = *static_cast<std::vector<std::uint8_t>*>(user_data);
			if (size != 0 && count > kMaxOnlineResponseSize / size) return 0;
			const size_t bytes = size * count;
			if (output.size() > kMaxOnlineResponseSize || bytes > kMaxOnlineResponseSize - output.size()) return 0;
			const auto* begin = static_cast<const std::uint8_t*>(data);
			output.insert(output.end(), begin, begin + bytes);
			return bytes;
		}
#else
		jclass AndroidGameClass(JNIEnv* env) {
			jobject activity = SDL_AndroidGetActivity();
			if (!activity) return nullptr;
			jclass game = env->GetObjectClass(activity);
			env->DeleteLocalRef(activity);
			return game;
		}

		HttpResponse AndroidHttpRequest(const std::string& url, const std::string& body, const std::string& user_agent,
			const std::vector<std::string>& headers) {
			auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
			if (!env) throw std::runtime_error("Android runtime is unavailable.");
			jclass game = AndroidGameClass(env);
			if (!game) { env->ExceptionClear(); throw std::runtime_error("Android Game class is unavailable."); }
			jmethodID method = env->GetStaticMethodID(game, "onlineApiRequest", "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)[B");
			if (!method) { env->ExceptionClear(); env->DeleteLocalRef(game); throw std::runtime_error("Android online HTTP bridge is unavailable."); }
			jclass string_class = env->FindClass("java/lang/String");
			jobjectArray header_array = env->NewObjectArray(static_cast<jsize>(headers.size()), string_class, nullptr);
			for (jsize index = 0; index < static_cast<jsize>(headers.size()); ++index) {
				jstring value = env->NewStringUTF(headers[index].c_str()); env->SetObjectArrayElement(header_array, index, value); env->DeleteLocalRef(value);
			}
			jstring java_url = env->NewStringUTF(url.c_str()); jstring java_body = env->NewStringUTF(body.c_str());
			jstring java_ua = env->NewStringUTF(user_agent.c_str());
			auto result = static_cast<jbyteArray>(env->CallStaticObjectMethod(game, method, java_url, java_body, header_array, java_ua));
			env->DeleteLocalRef(java_ua); env->DeleteLocalRef(java_body); env->DeleteLocalRef(java_url);
			env->DeleteLocalRef(header_array); env->DeleteLocalRef(string_class); env->DeleteLocalRef(game);
			if (env->ExceptionCheck()) { env->ExceptionClear(); throw std::runtime_error("Android online HTTP request failed."); }
			if (!result || env->GetArrayLength(result) < 4) { if (result) env->DeleteLocalRef(result); throw std::runtime_error("Android online HTTP request failed."); }
			const jsize size = env->GetArrayLength(result); std::vector<std::uint8_t> packed(size);
			env->GetByteArrayRegion(result, 0, size, reinterpret_cast<jbyte*>(packed.data())); env->DeleteLocalRef(result);
			HttpResponse response;
			response.status = static_cast<unsigned long>(packed[0]) |
				(static_cast<unsigned long>(packed[1]) << 8) |
				(static_cast<unsigned long>(packed[2]) << 16) |
				(static_cast<unsigned long>(packed[3]) << 24);
			response.body.assign(packed.begin() + 4, packed.end()); return response;
		}
#endif

		std::string SafeUaField(std::string value) {
			for (char& ch : value) if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '.' && ch != '_' && ch != '-') ch = '_';
			return value.empty() ? "unknown" : value;
		}

		std::string UserAgent() {
			return "CasioEmuMsvc/" + SafeUaField(GIT_LATEST_TAG) + " (" CASIOEMU_ONLINE_BUILD_OS "; " CASIOEMU_ONLINE_BUILD_ARCH ") git/" +
				SafeUaField(GIT_COMMIT_HASH) + " commit-date/" + SafeUaField(GIT_COMMIT_DATE) + " build/" CASIOEMU_ONLINE_BUILD_TIMESTAMP +
				" emu-api/" + std::to_string(CASIOEMU_ONLINE_API_PROTOCOL);
		}

		std::string RequestCanonical(const std::string& body, const std::string& timestamp, const std::string& nonce) {
			const auto body_hash = OnlineSha256(body);
			return "POST\n/emu/api\n" + timestamp + "\n" + nonce + "\n" + OnlineBase64UrlEncode(body_hash.data(), body_hash.size());
		}

		HttpResponse HttpRequest(const std::string& url, const std::string& body, const std::string& token,
			const OnlineDeviceIdentity* identity, bool sign_device) {
			auto build_key = OnlineBase64UrlDecode(CASIOEMU_ONLINE_CLIENT_KEY_B64);
			if (std::string(CASIOEMU_ONLINE_CLIENT_KEY_ID).empty() || build_key.size() < 32)
				throw std::runtime_error("Online API build key is not configured.");
			const auto timestamp = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
			std::array<std::uint8_t, 16> nonce_bytes{};
			OnlineRandomBytes(nonce_bytes.data(), nonce_bytes.size());
			const auto nonce = OnlineBase64UrlEncode(nonce_bytes.data(), nonce_bytes.size());
			const auto canonical = RequestCanonical(body, timestamp, nonce);
			const auto build_signature = OnlineHmacSha256(build_key, canonical);
			std::fill(build_key.begin(), build_key.end(), 0);
			const auto user_agent = UserAgent();
			std::string device_signature;
			if (sign_device) {
				if (!identity) throw std::runtime_error("Online device identity is unavailable.");
				const auto token_hash = OnlineSha256(token);
				const auto proof = canonical + "\n" + OnlineBase64UrlEncode(token_hash.data(), token_hash.size());
				device_signature = SignOnlineDeviceRequest(*identity, proof);
			}

#ifdef __ANDROID__
			std::vector<std::string> android_headers{
				"Accept: application/json, application/vnd.webcalcemu.model-encrypted", "Content-Type: application/json",
				"X-CasioEmu-Key-Id: " + std::string(CASIOEMU_ONLINE_CLIENT_KEY_ID), "X-CasioEmu-Timestamp: " + timestamp,
				"X-CasioEmu-Nonce: " + nonce,
				"X-CasioEmu-Signature: " + OnlineBase64UrlEncode(build_signature.data(), build_signature.size())};
			if (!token.empty()) android_headers.push_back("Authorization: Bearer " + token);
			if (!device_signature.empty()) android_headers.push_back("X-CasioEmu-Device-Signature: " + device_signature);
			return AndroidHttpRequest(url, body, user_agent, android_headers);
#else

			CURL* curl = curl_easy_init();
			if (!curl) throw std::runtime_error("Failed to initialize HTTP client.");
			HttpResponse result;
			curl_slist* headers = curl_slist_append(nullptr, "Accept: application/json, application/vnd.webcalcemu.model-encrypted");
			headers = curl_slist_append(headers, "Content-Type: application/json");
			headers = curl_slist_append(headers, ("X-CasioEmu-Key-Id: " + std::string(CASIOEMU_ONLINE_CLIENT_KEY_ID)).c_str());
			headers = curl_slist_append(headers, ("X-CasioEmu-Timestamp: " + timestamp).c_str());
			headers = curl_slist_append(headers, ("X-CasioEmu-Nonce: " + nonce).c_str());
			headers = curl_slist_append(headers, ("X-CasioEmu-Signature: " + OnlineBase64UrlEncode(build_signature.data(), build_signature.size())).c_str());
			if (!token.empty()) headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());
			if (!device_signature.empty()) headers = curl_slist_append(headers, ("X-CasioEmu-Device-Signature: " + device_signature).c_str());
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.c_str());
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
			curl_easy_setopt(curl, CURLOPT_POST, 1L);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
			const CURLcode code = curl_easy_perform(curl);
			if (code == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
			curl_slist_free_all(headers);
			curl_easy_cleanup(curl);
			if (code != CURLE_OK) throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(code));
			return result;
#endif
		}

		json ParseJson(const HttpResponse& response) {
			return json::parse(response.body.begin(), response.body.end());
		}

		void RequireSuccess(const HttpResponse& response, const char* operation) {
			if (response.status == 401)
				throw OnlineAuthenticationError("Online login has expired. Please log in again.");
			if (response.status < 200 || response.status >= 300)
				throw std::runtime_error(std::string(operation) + " failed (HTTP " + std::to_string(response.status) + ").");
		}
	}

	OnlineModelClient::OnlineModelClient(std::string api_base) : api_base_(std::move(api_base)) {
		while (!api_base_.empty() && api_base_.back() == '/') api_base_.pop_back();
		if (api_base_.empty()) throw std::runtime_error("API address is empty.");
		identity_ = LoadOrCreateOnlineDeviceIdentity(api_base_);
	}

	OnlineAuthRequest OnlineModelClient::StartAuthorization(const std::string& redirect_uri) const {
		json request{{"action", "auth-start"}, {"redirect_uri", redirect_uri},
			{"auth_public_key", identity_.auth_public_b64}, {"wrap_public_key", identity_.wrap_public_b64}};
		const auto response = HttpRequest(api_base_ + "/emu/api", request.dump(), {}, &identity_, false);
		RequireSuccess(response, "Authorization start");
		const auto value = ParseJson(response);
		if (value.value("device_id", "") != identity_.device_id)
			throw std::runtime_error("Authorization response does not match this device.");
		return {value.at("device_code").get<std::string>(), value.at("verification_uri").get<std::string>(),
			value.value("interval", 2)};
	}

	bool OnlineModelClient::PollAuthorization(const std::string& device_code, std::string& access_token) const {
		const auto body = json{{"action", "auth-poll"}, {"device_code", device_code}}.dump();
		const auto response = HttpRequest(api_base_ + "/emu/api", body, {}, &identity_, true);
		if (response.status == 428) return false;
		RequireSuccess(response, "Authorization poll");
		const auto value = ParseJson(response);
		if (value.value("device_id", "") != identity_.device_id)
			throw std::runtime_error("Authorization response does not match this device.");
		access_token = value.at("access_token").get<std::string>();
		return true;
	}

	int OnlineModelClient::CheckStatus(const std::string& access_token) const {
		const auto response = HttpRequest(api_base_ + "/emu/api", json{{"action", "status"}}.dump(), access_token, &identity_, true);
		RequireSuccess(response, "Authentication status");
		return ParseJson(response).value("expires_in", 0);
	}

	void OnlineModelClient::RevokeDevice(const std::string& access_token) const {
		const auto response = HttpRequest(api_base_ + "/emu/api", json{{"action", "device-revoke"}}.dump(), access_token, &identity_, true);
		RequireSuccess(response, "Device revoke");
	}

	std::vector<OnlineModelEntry> OnlineModelClient::ListModels(const std::string& access_token) const {
		const auto response = HttpRequest(api_base_ + "/emu/api", json{{"action", "list"}}.dump(), access_token, &identity_, true);
		RequireSuccess(response, "Model list");
		const auto value = ParseJson(response);
		std::vector<OnlineModelEntry> result;
		for (const auto& item : value.at("models"))
			result.push_back({
				item.at("id").get<std::string>(),
				item.at("name").get<std::string>(),
				item.at("modelType").get<std::string>(),
				item.value("realHardware", false),
				item.value("sampleRom", false),
			});
		return result;
	}

	std::vector<std::uint8_t> OnlineModelClient::DownloadModel(const std::string& access_token, const std::string& model_id) const {
		const auto response = HttpRequest(api_base_ + "/emu/api", json{{"action", "export"}, {"model", model_id}}.dump(), access_token, &identity_, true);
		RequireSuccess(response, "Model download");
		return DecryptOnlineModelEnvelope(identity_, response.body, model_id);
	}

	void SaveOnlineToken(const std::string& api_base, const std::string& token) {
#ifdef _WIN32
		const std::string payload = api_base + "\n" + token;
		DATA_BLOB input{static_cast<DWORD>(payload.size()), reinterpret_cast<BYTE*>(const_cast<char*>(payload.data()))};
		DATA_BLOB output{};
		if (!CryptProtectData(&input, L"CasioEmuMsvc online token", nullptr, nullptr, nullptr,
			CRYPTPROTECT_UI_FORBIDDEN, &output))
			throw std::runtime_error("Failed to protect online login token.");
		std::ofstream stream(OnlineStorageFileName("online_token", api_base), std::ios::binary | std::ios::trunc);
		if (!stream || !stream.write(reinterpret_cast<const char*>(output.pbData), output.cbData)) {
			LocalFree(output.pbData);
			throw std::runtime_error("Failed to save online login token.");
		}
		LocalFree(output.pbData);
	#elif defined(__ANDROID__)
		auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
		if (!env) throw std::runtime_error("Android runtime is unavailable.");
		jclass game = AndroidGameClass(env);
		if (!game) { env->ExceptionClear(); throw std::runtime_error("Android Game class is unavailable."); }
		jmethodID method = env->GetStaticMethodID(game, "saveOnlineToken", "(Ljava/lang/String;Ljava/lang/String;)Z");
		if (!method) { env->ExceptionClear(); env->DeleteLocalRef(game); throw std::runtime_error("Android online token storage is unavailable."); }
		jstring api = env->NewStringUTF(api_base.c_str());
		jstring value = env->NewStringUTF(token.c_str());
		const bool saved = env->CallStaticBooleanMethod(game, method, api, value) == JNI_TRUE;
		env->DeleteLocalRef(value); env->DeleteLocalRef(api); env->DeleteLocalRef(game);
		if (env->ExceptionCheck()) { env->ExceptionClear(); throw std::runtime_error("Failed to save online login token."); }
		if (!saved) throw std::runtime_error("Failed to save online login token.");
#else
#ifdef __APPLE__
		CFStringRef account = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(api_base.data()), api_base.size(), kCFStringEncodingUTF8, false);
		CFDataRef data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(token.data()), token.size());
		const void* delete_keys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
		const void* delete_values[] = {kSecClassGenericPassword, CFSTR("CasioEmuMsvc online token"), account};
		CFDictionaryRef delete_query = CFDictionaryCreate(nullptr, delete_keys, delete_values, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		SecItemDelete(delete_query);
		CFRelease(delete_query);
		const void* keys[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData};
		const void* values[] = {kSecClassGenericPassword, CFSTR("CasioEmuMsvc online token"), account, data};
		CFDictionaryRef query = CFDictionaryCreate(nullptr, keys, values, 4, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		const OSStatus status = SecItemAdd(query, nullptr);
		CFRelease(query);
		CFRelease(data);
		CFRelease(account);
		if (status != errSecSuccess) throw std::runtime_error("Failed to save online login token to Keychain.");
#elif defined(CASIOEMU_HAS_LIBSECRET)
		static const SecretSchema schema = {"com.casioemu.online-token", SECRET_SCHEMA_NONE,
			{{"api", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}}};
		GError* error = nullptr;
		if (!secret_password_store_sync(&schema, SECRET_COLLECTION_DEFAULT, "CasioEmuMsvc online token", token.c_str(),
			nullptr, &error, "api", api_base.c_str(), nullptr)) {
			const std::string message = error ? error->message : "unknown error";
			if (error) g_error_free(error);
			throw std::runtime_error("Failed to save online login token: " + message);
		}
#else
		(void)api_base;
		(void)token;
#endif
#endif
	}

	std::string LoadOnlineToken(const std::string& api_base) {
#ifdef _WIN32
		std::ifstream stream(OnlineStorageFileName("online_token", api_base), std::ios::binary);
		if (!stream) return {};
		std::vector<std::uint8_t> encrypted{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
		if (encrypted.empty()) return {};
		DATA_BLOB input{static_cast<DWORD>(encrypted.size()), encrypted.data()};
		DATA_BLOB output{};
		if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return {};
		const std::string payload(reinterpret_cast<const char*>(output.pbData), output.cbData);
		LocalFree(output.pbData);
		const auto newline = payload.find('\n');
		if (newline == std::string::npos || payload.substr(0, newline) != api_base) return {};
		return payload.substr(newline + 1);
	#elif defined(__ANDROID__)
		auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
		if (!env) return {};
		jclass game = AndroidGameClass(env);
		if (!game) { env->ExceptionClear(); return {}; }
		jmethodID method = env->GetStaticMethodID(game, "loadOnlineToken", "(Ljava/lang/String;)Ljava/lang/String;");
		if (!method) { env->ExceptionClear(); env->DeleteLocalRef(game); return {}; }
		jstring api = env->NewStringUTF(api_base.c_str());
		auto value = static_cast<jstring>(env->CallStaticObjectMethod(game, method, api));
		env->DeleteLocalRef(api); env->DeleteLocalRef(game);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return {}; }
		if (!value) return {};
		const char* chars = env->GetStringUTFChars(value, nullptr);
		const std::string token = chars ? chars : "";
		if (chars) env->ReleaseStringUTFChars(value, chars);
		env->DeleteLocalRef(value);
		return token;
#else
#ifdef __APPLE__
		CFStringRef account = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(api_base.data()), api_base.size(), kCFStringEncodingUTF8, false);
		const void* keys[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecReturnData, kSecMatchLimit};
		const void* values[] = {kSecClassGenericPassword, CFSTR("CasioEmuMsvc online token"), account, kCFBooleanTrue, kSecMatchLimitOne};
		CFDictionaryRef query = CFDictionaryCreate(nullptr, keys, values, 5, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		CFTypeRef result = nullptr;
		const OSStatus status = SecItemCopyMatching(query, &result);
		CFRelease(query);
		CFRelease(account);
		if (status != errSecSuccess || !result) return {};
		auto data = static_cast<CFDataRef>(result);
		std::string token(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), CFDataGetLength(data));
		CFRelease(result);
		return token;
#elif defined(CASIOEMU_HAS_LIBSECRET)
		static const SecretSchema schema = {"com.casioemu.online-token", SECRET_SCHEMA_NONE,
			{{"api", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}}};
		GError* error = nullptr;
		char* password = secret_password_lookup_sync(&schema, nullptr, &error, "api", api_base.c_str(), nullptr);
		if (error) { g_error_free(error); return {}; }
		if (!password) return {};
		const std::string token(password);
		secret_password_free(password);
		return token;
#else
		(void)api_base;
		return {};
#endif
#endif
	}

	void ClearOnlineToken(const std::string& api_base) {
#ifdef _WIN32
		std::error_code error;
		std::filesystem::remove(OnlineStorageFileName("online_token", api_base), error);
#elif defined(__ANDROID__)
		auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
		if (!env) return;
		jclass game = AndroidGameClass(env);
		if (!game) { env->ExceptionClear(); return; }
		jmethodID method = env->GetStaticMethodID(game, "clearOnlineToken", "(Ljava/lang/String;)V");
		if (!method) { env->ExceptionClear(); env->DeleteLocalRef(game); return; }
		jstring api = env->NewStringUTF(api_base.c_str());
		env->CallStaticVoidMethod(game, method, api);
		env->DeleteLocalRef(api); env->DeleteLocalRef(game);
		if (env->ExceptionCheck()) env->ExceptionClear();
#elif defined(__APPLE__)
		CFStringRef account = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(api_base.data()), api_base.size(), kCFStringEncodingUTF8, false);
		const void* keys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
		const void* values[] = {kSecClassGenericPassword, CFSTR("CasioEmuMsvc online token"), account};
		CFDictionaryRef query = CFDictionaryCreate(nullptr, keys, values, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		SecItemDelete(query);
		CFRelease(query);
		CFRelease(account);
#elif defined(CASIOEMU_HAS_LIBSECRET)
		if (api_base.empty()) return;
		static const SecretSchema schema = {"com.casioemu.online-token", SECRET_SCHEMA_NONE,
			{{"api", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}}};
		secret_password_clear_sync(&schema, nullptr, nullptr, "api", api_base.c_str(), nullptr);
#else
		(void)api_base;
#endif
	}

	bool OnlineTokenPersistenceAvailable() {
#if defined(_WIN32) || defined(__ANDROID__) || defined(__APPLE__) || defined(CASIOEMU_HAS_LIBSECRET)
		return true;
#else
		return false;
#endif
	}
}
