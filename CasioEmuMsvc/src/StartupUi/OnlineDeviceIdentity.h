#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace casioemu {
	struct OnlineDeviceIdentity {
		std::array<std::uint8_t, 32> auth_seed{};
		std::array<std::uint8_t, 64> auth_secret{};
		std::array<std::uint8_t, 32> auth_public{};
		std::array<std::uint8_t, 32> wrap_secret{};
		std::array<std::uint8_t, 32> wrap_public{};
		std::string auth_public_b64;
		std::string wrap_public_b64;
		std::string auth_key_id;
		std::string wrap_key_id;
		std::string device_id;
	};

	OnlineDeviceIdentity LoadOrCreateOnlineDeviceIdentity(const std::string& api_base);
	std::string OnlineBase64UrlEncode(const std::uint8_t* data, std::size_t size);
	std::vector<std::uint8_t> OnlineBase64UrlDecode(const std::string& value);
	std::vector<std::uint8_t> OnlineSha256(const std::string& value);
	std::vector<std::uint8_t> OnlineSha256(const std::vector<std::uint8_t>& value);
	std::vector<std::uint8_t> OnlineHmacSha256(const std::vector<std::uint8_t>& key, const std::string& value);
	std::string OnlineStorageFileName(const char* prefix, const std::string& api_base);
	void OnlineRandomBytes(std::uint8_t* data, std::size_t size);
	std::string SignOnlineDeviceRequest(const OnlineDeviceIdentity& identity, const std::string& value);
	std::vector<std::uint8_t> DecryptOnlineModelEnvelope(
		const OnlineDeviceIdentity& identity,
		const std::vector<std::uint8_t>& envelope,
		const std::string& expected_model_id);
}
