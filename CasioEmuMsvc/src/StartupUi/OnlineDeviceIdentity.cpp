#include "OnlineDeviceIdentity.h"

#include "../../../McpPlugin/json.hpp"
#include "Ext/RomPackage.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(CASIOEMU_HAS_LIBSECRET)
#include <libsecret/secret.h>
#endif
#ifdef __ANDROID__
#include <jni.h>
#include <SDL_system.h>
#endif
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__ANDROID__)
#include <sys/random.h>
#endif
#ifdef __ANDROID__
#include <cstdlib>
#endif

namespace casioemu {
	namespace {
		using json = nlohmann::json;
		constexpr char kMagic[] = "WCEMDL01";
		constexpr std::size_t kMaxEnvelopeSize = 40ull * 1024 * 1024;
		std::map<std::string, std::string> session_identities;

#ifdef __ANDROID__
		jclass AndroidGameClass(JNIEnv* env) {
			jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
			if (!activity) return nullptr;
			jclass game = env->GetObjectClass(activity);
			env->DeleteLocalRef(activity);
			return game;
		}
#endif

		std::string EncodeIdentity(const OnlineDeviceIdentity& identity) {
			return json{{"version", 1},
				{"auth_seed", OnlineBase64UrlEncode(identity.auth_seed.data(), identity.auth_seed.size())},
				{"wrap_secret", OnlineBase64UrlEncode(identity.wrap_secret.data(), identity.wrap_secret.size())}}.dump();
		}

		OnlineDeviceIdentity DecodeIdentity(const std::string& value) {
			const auto parsed = json::parse(value);
			if (parsed.value("version", 0) != 1) throw std::runtime_error("Unsupported online device identity version.");
			const auto auth_seed = OnlineBase64UrlDecode(parsed.at("auth_seed").get<std::string>());
			const auto wrap_secret = OnlineBase64UrlDecode(parsed.at("wrap_secret").get<std::string>());
			if (auth_seed.size() != 32 || wrap_secret.size() != 32) throw std::runtime_error("Invalid online device identity.");
			OnlineDeviceIdentity identity;
			std::copy(auth_seed.begin(), auth_seed.end(), identity.auth_seed.begin());
			std::copy(wrap_secret.begin(), wrap_secret.end(), identity.wrap_secret.begin());
			auto seed_copy = identity.auth_seed;
			crypto_ed25519_key_pair(identity.auth_secret.data(), identity.auth_public.data(), seed_copy.data());
			crypto_x25519_public_key(identity.wrap_public.data(), identity.wrap_secret.data());
			identity.auth_public_b64 = OnlineBase64UrlEncode(identity.auth_public.data(), identity.auth_public.size());
			identity.wrap_public_b64 = OnlineBase64UrlEncode(identity.wrap_public.data(), identity.wrap_public.size());
			identity.auth_key_id = OnlineBase64UrlEncode(OnlineSha256(identity.auth_public_b64).data(), 32);
			identity.wrap_key_id = OnlineBase64UrlEncode(OnlineSha256(identity.wrap_public_b64).data(), 32);
			identity.device_id = OnlineBase64UrlEncode(OnlineSha256(identity.auth_public_b64 + "." + identity.wrap_public_b64).data(), 32);
			return identity;
		}

		std::string LoadIdentityValue(const std::string& api_base) {
			auto session = session_identities.find(api_base);
			if (session != session_identities.end()) return session->second;
#ifdef _WIN32
			std::ifstream stream(OnlineStorageFileName("online_device", api_base), std::ios::binary);
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
#elif defined(__APPLE__)
			CFStringRef account = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(api_base.data()), api_base.size(), kCFStringEncodingUTF8, false);
			const void* keys[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecReturnData, kSecMatchLimit};
			const void* values[] = {kSecClassGenericPassword, CFSTR("CasioEmuMsvc online device"), account, kCFBooleanTrue, kSecMatchLimitOne};
			CFDictionaryRef query = CFDictionaryCreate(nullptr, keys, values, 5, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
			CFTypeRef result = nullptr;
			const OSStatus status = SecItemCopyMatching(query, &result);
			CFRelease(query); CFRelease(account);
			if (status != errSecSuccess || !result) return {};
			auto data = static_cast<CFDataRef>(result);
			std::string output(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), CFDataGetLength(data));
			CFRelease(result);
			return output;
#elif defined(__ANDROID__)
			auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
			if (!env) return {};
			jclass game = AndroidGameClass(env);
			if (!game) { env->ExceptionClear(); return {}; }
			jmethodID method = env->GetStaticMethodID(game, "loadOnlineDeviceIdentity", "(Ljava/lang/String;)[B");
			if (!method) { env->ExceptionClear(); env->DeleteLocalRef(game); return {}; }
			jstring api = env->NewStringUTF(api_base.c_str());
			auto result = static_cast<jbyteArray>(env->CallStaticObjectMethod(game, method, api));
			env->DeleteLocalRef(api); env->DeleteLocalRef(game);
			if (env->ExceptionCheck()) { env->ExceptionClear(); return {}; }
			if (!result) return {};
			const jsize size = env->GetArrayLength(result);
			std::string output(size, '\0');
			env->GetByteArrayRegion(result, 0, size, reinterpret_cast<jbyte*>(output.data()));
			env->DeleteLocalRef(result);
			return output;
#elif defined(CASIOEMU_HAS_LIBSECRET)
			static const SecretSchema schema = {"com.casioemu.online-device", SECRET_SCHEMA_NONE,
				{{"api", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}}};
			GError* error = nullptr;
			char* password = secret_password_lookup_sync(&schema, nullptr, &error, "api", api_base.c_str(), nullptr);
			if (error) { g_error_free(error); return {}; }
			if (!password) return {};
			std::string output(password); secret_password_free(password); return output;
#else
			return {};
#endif
		}

		void SaveIdentityValue(const std::string& api_base, const std::string& value) {
			session_identities[api_base] = value;
#ifdef _WIN32
			const std::string payload = api_base + "\n" + value;
			DATA_BLOB input{static_cast<DWORD>(payload.size()), reinterpret_cast<BYTE*>(const_cast<char*>(payload.data()))};
			DATA_BLOB output{};
			if (!CryptProtectData(&input, L"CasioEmuMsvc online device", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
				throw std::runtime_error("Failed to protect online device identity.");
			std::ofstream stream(OnlineStorageFileName("online_device", api_base), std::ios::binary | std::ios::trunc);
			if (!stream || !stream.write(reinterpret_cast<const char*>(output.pbData), output.cbData)) {
				LocalFree(output.pbData); throw std::runtime_error("Failed to save online device identity.");
			}
			LocalFree(output.pbData);
#elif defined(__APPLE__)
			CFStringRef account = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(api_base.data()), api_base.size(), kCFStringEncodingUTF8, false);
			CFDataRef data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(value.data()), value.size());
			const void* delete_keys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
			const void* delete_values[] = {kSecClassGenericPassword, CFSTR("CasioEmuMsvc online device"), account};
			CFDictionaryRef delete_query = CFDictionaryCreate(nullptr, delete_keys, delete_values, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
			SecItemDelete(delete_query); CFRelease(delete_query);
			const void* keys[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData};
			const void* values[] = {kSecClassGenericPassword, CFSTR("CasioEmuMsvc online device"), account, data};
			CFDictionaryRef query = CFDictionaryCreate(nullptr, keys, values, 4, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
			const OSStatus status = SecItemAdd(query, nullptr);
			CFRelease(query); CFRelease(data); CFRelease(account);
			if (status != errSecSuccess) throw std::runtime_error("Failed to save online device identity to Keychain.");
#elif defined(__ANDROID__)
			auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
			if (!env) throw std::runtime_error("Android runtime is unavailable.");
			jclass game = AndroidGameClass(env);
			if (!game) { env->ExceptionClear(); throw std::runtime_error("Android Game class is unavailable."); }
			jmethodID method = env->GetStaticMethodID(game, "saveOnlineDeviceIdentity", "(Ljava/lang/String;[B)Z");
			if (!method) { env->ExceptionClear(); env->DeleteLocalRef(game); throw std::runtime_error("Android online identity storage is unavailable."); }
			jstring api = env->NewStringUTF(api_base.c_str());
			jbyteArray data = env->NewByteArray(static_cast<jsize>(value.size()));
			env->SetByteArrayRegion(data, 0, static_cast<jsize>(value.size()), reinterpret_cast<const jbyte*>(value.data()));
			const jboolean saved = env->CallStaticBooleanMethod(game, method, api, data);
			env->DeleteLocalRef(data); env->DeleteLocalRef(api); env->DeleteLocalRef(game);
			if (env->ExceptionCheck()) { env->ExceptionClear(); throw std::runtime_error("Failed to save Android online device identity."); }
			if (!saved) throw std::runtime_error("Failed to save Android online device identity.");
#elif defined(CASIOEMU_HAS_LIBSECRET)
			static const SecretSchema schema = {"com.casioemu.online-device", SECRET_SCHEMA_NONE,
				{{"api", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}}};
			GError* error = nullptr;
			if (!secret_password_store_sync(&schema, SECRET_COLLECTION_DEFAULT, "CasioEmuMsvc online device", value.c_str(),
				nullptr, &error, "api", api_base.c_str(), nullptr)) {
				const std::string message = error ? error->message : "unknown error";
				if (error) g_error_free(error);
				throw std::runtime_error("Failed to save online device identity: " + message);
			}
#endif
		}

		std::vector<std::uint8_t> HkdfSha256(const std::vector<std::uint8_t>& secret, const std::vector<std::uint8_t>& salt, const std::string& info) {
			auto prk = crypto::HMAC_SHA256::hmac(salt, secret);
			std::vector<std::uint8_t> input(info.begin(), info.end());
			input.push_back(1);
			auto result = crypto::HMAC_SHA256::hmac(prk, input);
			crypto_wipe(prk.data(), prk.size());
			return result;
		}
	}

	std::string OnlineStorageFileName(const char* prefix, const std::string& api_base) {
		const auto hash = OnlineSha256(api_base);
		return std::string(prefix) + "_" + OnlineBase64UrlEncode(hash.data(), hash.size()) + ".bin";
	}

	std::string OnlineBase64UrlEncode(const std::uint8_t* data, std::size_t size) {
		static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
		std::string output;
		for (std::size_t index = 0; index < size; index += 3) {
			const std::uint32_t value = (static_cast<std::uint32_t>(data[index]) << 16) |
				(index + 1 < size ? static_cast<std::uint32_t>(data[index + 1]) << 8 : 0) |
				(index + 2 < size ? data[index + 2] : 0);
			output.push_back(table[(value >> 18) & 63]); output.push_back(table[(value >> 12) & 63]);
			if (index + 1 < size) output.push_back(table[(value >> 6) & 63]);
			if (index + 2 < size) output.push_back(table[value & 63]);
		}
		return output;
	}

	std::vector<std::uint8_t> OnlineBase64UrlDecode(const std::string& value) {
		if (value.size() % 4 == 1) throw std::runtime_error("Invalid base64url value.");
		auto decode = [](char ch) -> int {
			if (ch >= 'A' && ch <= 'Z') return ch - 'A'; if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
			if (ch >= '0' && ch <= '9') return ch - '0' + 52; if (ch == '-') return 62; if (ch == '_') return 63; return -1;
		};
		std::vector<std::uint8_t> output; std::uint32_t buffer = 0; int bits = 0;
		for (char ch : value) { const int digit = decode(ch); if (digit < 0) throw std::runtime_error("Invalid base64url value.");
			buffer = (buffer << 6) | digit; bits += 6; if (bits >= 8) { bits -= 8; output.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xff)); } }
		if (bits > 0 && (buffer & ((1u << bits) - 1)) != 0) throw std::runtime_error("Invalid base64url value.");
		return output;
	}

	std::vector<std::uint8_t> OnlineSha256(const std::string& value) { return crypto::SHA256::hash256(value); }
	std::vector<std::uint8_t> OnlineSha256(const std::vector<std::uint8_t>& value) { return crypto::SHA256::hash256(value); }
	std::vector<std::uint8_t> OnlineHmacSha256(const std::vector<std::uint8_t>& key, const std::string& value) {
		return crypto::HMAC_SHA256::hmac(key, std::vector<std::uint8_t>(value.begin(), value.end()));
	}

	void OnlineRandomBytes(std::uint8_t* data, std::size_t size) {
#ifdef _WIN32
		if (BCryptGenRandom(nullptr, data, static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
			throw std::runtime_error("Failed to obtain secure random bytes.");
#elif defined(__APPLE__)
		if (SecRandomCopyBytes(kSecRandomDefault, size, data) != errSecSuccess)
			throw std::runtime_error("Failed to obtain secure random bytes.");
#elif defined(__ANDROID__)
		arc4random_buf(data, size);
#else
		std::size_t offset = 0;
		while (offset < size) {
			const auto count = getrandom(data + offset, size - offset, 0);
			if (count <= 0) throw std::runtime_error("Failed to obtain secure random bytes.");
			offset += static_cast<std::size_t>(count);
		}
#endif
	}

	OnlineDeviceIdentity LoadOrCreateOnlineDeviceIdentity(const std::string& api_base) {
		const auto stored = LoadIdentityValue(api_base);
		if (!stored.empty()) return DecodeIdentity(stored);
		OnlineDeviceIdentity identity;
		OnlineRandomBytes(identity.auth_seed.data(), identity.auth_seed.size());
		OnlineRandomBytes(identity.wrap_secret.data(), identity.wrap_secret.size());
		auto seed_copy = identity.auth_seed;
		crypto_ed25519_key_pair(identity.auth_secret.data(), identity.auth_public.data(), seed_copy.data());
		crypto_x25519_public_key(identity.wrap_public.data(), identity.wrap_secret.data());
		identity.auth_public_b64 = OnlineBase64UrlEncode(identity.auth_public.data(), identity.auth_public.size());
		identity.wrap_public_b64 = OnlineBase64UrlEncode(identity.wrap_public.data(), identity.wrap_public.size());
		identity.auth_key_id = OnlineBase64UrlEncode(OnlineSha256(identity.auth_public_b64).data(), 32);
		identity.wrap_key_id = OnlineBase64UrlEncode(OnlineSha256(identity.wrap_public_b64).data(), 32);
		identity.device_id = OnlineBase64UrlEncode(OnlineSha256(identity.auth_public_b64 + "." + identity.wrap_public_b64).data(), 32);
		SaveIdentityValue(api_base, EncodeIdentity(identity));
		return identity;
	}

	std::string SignOnlineDeviceRequest(const OnlineDeviceIdentity& identity, const std::string& value) {
		std::array<std::uint8_t, 64> signature{};
		crypto_ed25519_sign(signature.data(), identity.auth_secret.data(), reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
		return OnlineBase64UrlEncode(signature.data(), signature.size());
	}

	std::vector<std::uint8_t> DecryptOnlineModelEnvelope(const OnlineDeviceIdentity& identity,
		const std::vector<std::uint8_t>& envelope, const std::string& expected_model_id) {
		if (envelope.size() < 12 + 16 || envelope.size() > kMaxEnvelopeSize || std::memcmp(envelope.data(), kMagic, 8) != 0)
			throw std::runtime_error("Invalid online model envelope.");
		const std::uint32_t header_size = static_cast<std::uint32_t>(envelope[8]) |
			(static_cast<std::uint32_t>(envelope[9]) << 8) |
			(static_cast<std::uint32_t>(envelope[10]) << 16) |
			(static_cast<std::uint32_t>(envelope[11]) << 24);
		if (header_size == 0 || header_size > 16 * 1024 || 12ull + header_size + 16 > envelope.size()) throw std::runtime_error("Invalid online model envelope header.");
		const std::string header_text(reinterpret_cast<const char*>(envelope.data() + 12), header_size);
		const auto header = json::parse(header_text);
		if (header.value("version", 0) != 1 || header.value("algorithm", "") != "X25519-HKDF-SHA256+XCHACHA20-POLY1305" ||
			header.value("modelId", "") != expected_model_id || header.value("recipientKeyId", "") != identity.wrap_key_id ||
			header.value("deviceId", "") != identity.device_id)
			throw std::runtime_error("Online model envelope does not match this device or model.");
		const auto ephemeral = OnlineBase64UrlDecode(header.at("ephemeralPublicKey").get<std::string>());
		const auto salt = OnlineBase64UrlDecode(header.at("salt").get<std::string>());
		const auto nonce = OnlineBase64UrlDecode(header.at("nonce").get<std::string>());
		const auto expected_hash = OnlineBase64UrlDecode(header.at("archiveSha256").get<std::string>());
		if (ephemeral.size() != 32 || salt.size() != 32 || nonce.size() != 24 || expected_hash.size() != 32)
			throw std::runtime_error("Invalid online model encryption parameters.");
		std::array<std::uint8_t, 32> shared{};
		crypto_x25519(shared.data(), identity.wrap_secret.data(), ephemeral.data());
		std::string info = "WebCalcEmu online model v1";
		info.push_back('\0');
		info += header.at("deviceId").get<std::string>();
		info.push_back('\0');
		info += expected_model_id;
		auto key = HkdfSha256(std::vector<std::uint8_t>(shared.begin(), shared.end()), salt, info);
		const std::size_t cipher_size = envelope.size() - 12 - header_size - 16;
		const auto* cipher = envelope.data() + 12 + header_size;
		const auto* mac = cipher + cipher_size;
		std::vector<std::uint8_t> plain(cipher_size);
		const int unlock_result = crypto_aead_unlock(plain.data(), mac, key.data(), nonce.data(),
			reinterpret_cast<const std::uint8_t*>(header_text.data()), header_text.size(), cipher, cipher_size);
		crypto_wipe(shared.data(), shared.size());
		if (unlock_result != 0) {
			crypto_wipe(key.data(), key.size());
			throw std::runtime_error("Online model authentication failed.");
		}
		const bool hash_matches = crypto::constant_time_compare(OnlineSha256(plain), expected_hash);
		crypto_wipe(key.data(), key.size());
		if (!hash_matches) throw std::runtime_error("Online model archive hash mismatch.");
		return plain;
	}
}
