#include "OnlineModelClient.h"

#include "../../../McpPlugin/json.hpp"

#include <algorithm>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
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

		struct HttpResponse {
			unsigned long status{};
			std::vector<std::uint8_t> body;
		};

		std::string UrlEncode(const std::string& value) {
			static constexpr char hex[] = "0123456789ABCDEF";
			std::string result;
			for (const unsigned char ch : value) {
				if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
					(ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
					result.push_back(static_cast<char>(ch));
				}
				else {
					result.push_back('%');
					result.push_back(hex[ch >> 4]);
					result.push_back(hex[ch & 0x0f]);
				}
			}
			return result;
		}

		size_t CurlWrite(void* data, size_t size, size_t count, void* user_data) {
			auto& output = *static_cast<std::vector<std::uint8_t>*>(user_data);
			const size_t bytes = size * count;
			const auto* begin = static_cast<const std::uint8_t*>(data);
			output.insert(output.end(), begin, begin + bytes);
			return bytes;
		}

		HttpResponse HttpRequest(const std::string& url, const wchar_t* method, const std::string& token = {}) {
			CURL* curl = curl_easy_init();
			if (!curl) throw std::runtime_error("Failed to initialize HTTP client.");
			HttpResponse result;
			curl_slist* headers = curl_slist_append(nullptr, "Accept: application/json");
			if (!token.empty()) headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
			if (std::wstring(method) == L"POST") {
				curl_easy_setopt(curl, CURLOPT_POST, 1L);
				curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
				curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
			}
			const CURLcode code = curl_easy_perform(curl);
			if (code == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
			curl_slist_free_all(headers);
			curl_easy_cleanup(curl);
			if (code != CURLE_OK) throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(code));
			return result;
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
	}

	OnlineAuthRequest OnlineModelClient::StartAuthorization() const {
		const auto response = HttpRequest(api_base_ + "/emu/api?action=auth-start", L"POST");
		RequireSuccess(response, "Authorization start");
		const auto value = ParseJson(response);
		return {value.at("device_code").get<std::string>(), value.at("user_code").get<std::string>(),
			value.at("verification_uri").get<std::string>(), value.value("interval", 2)};
	}

	bool OnlineModelClient::PollAuthorization(const std::string& device_code, std::string& access_token) const {
		const auto response = HttpRequest(api_base_ + "/emu/api?action=auth-poll&device_code=" + UrlEncode(device_code), L"POST");
		if (response.status == 428) return false;
		RequireSuccess(response, "Authorization poll");
		access_token = ParseJson(response).at("access_token").get<std::string>();
		return true;
	}

	int OnlineModelClient::CheckStatus(const std::string& access_token) const {
		const auto response = HttpRequest(api_base_ + "/emu/api?action=status", L"GET", access_token);
		RequireSuccess(response, "Authentication status");
		return ParseJson(response).value("expires_in", 0);
	}

	std::vector<OnlineModelEntry> OnlineModelClient::ListModels(const std::string& access_token) const {
		const auto response = HttpRequest(api_base_ + "/emu/api?action=list", L"GET", access_token);
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
		const auto response = HttpRequest(api_base_ + "/emu/api?action=export&model=" + UrlEncode(model_id), L"GET", access_token);
		RequireSuccess(response, "Model download");
		return response.body;
	}

	void SaveOnlineToken(const std::string& api_base, const std::string& token) {
#ifdef _WIN32
		const std::string payload = api_base + "\n" + token;
		DATA_BLOB input{static_cast<DWORD>(payload.size()), reinterpret_cast<BYTE*>(const_cast<char*>(payload.data()))};
		DATA_BLOB output{};
		if (!CryptProtectData(&input, L"CasioEmuMsvc online token", nullptr, nullptr, nullptr,
			CRYPTPROTECT_UI_FORBIDDEN, &output))
			throw std::runtime_error("Failed to protect online login token.");
		std::ofstream stream("online_token.bin", std::ios::binary | std::ios::trunc);
		if (!stream || !stream.write(reinterpret_cast<const char*>(output.pbData), output.cbData)) {
			LocalFree(output.pbData);
			throw std::runtime_error("Failed to save online login token.");
		}
		LocalFree(output.pbData);
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
		std::ifstream stream("online_token.bin", std::ios::binary);
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
		std::filesystem::remove("online_token.bin", error);
#elif defined(__APPLE__)
		const void* keys[] = {kSecClass, kSecAttrService};
		const void* values[] = {kSecClassGenericPassword, CFSTR("CasioEmuMsvc online token")};
		CFDictionaryRef query = CFDictionaryCreate(nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		SecItemDelete(query);
		CFRelease(query);
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
#if defined(_WIN32) || defined(__APPLE__) || defined(CASIOEMU_HAS_LIBSECRET)
		return true;
#else
		return false;
#endif
	}
}
