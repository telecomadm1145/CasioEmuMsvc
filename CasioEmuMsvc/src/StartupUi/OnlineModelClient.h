#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "OnlineDeviceIdentity.h"

namespace casioemu {
	class OnlineAuthenticationError : public std::runtime_error {
	public:
		using std::runtime_error::runtime_error;
	};

	struct OnlineAuthRequest {
		std::string device_code;
		std::string verification_uri;
		int interval = 2;
	};

	struct OnlineModelEntry {
		std::string id;
		std::string name;
		std::string model_type;
		bool real_hardware = false;
		bool sample_rom = false;
	};

	class OnlineModelClient {
	public:
		explicit OnlineModelClient(std::string api_base);
		OnlineAuthRequest StartAuthorization(const std::string& redirect_uri = {}) const;
		bool PollAuthorization(const std::string& device_code, std::string& access_token) const;
		void RevokeDevice(const std::string& access_token) const;
		std::vector<OnlineModelEntry> ListModels(const std::string& access_token) const;
		std::vector<std::uint8_t> DownloadModel(const std::string& access_token, const std::string& model_id) const;
		const std::string& ApiBase() const { return api_base_; }

	private:
		std::string api_base_;
		OnlineDeviceIdentity identity_;
	};

	void SaveOnlineToken(const std::string& api_base, const std::string& token);
	std::string LoadOnlineToken(const std::string& api_base);
	void ClearOnlineToken(const std::string& api_base);
	bool OnlineTokenPersistenceAvailable();
}
