#pragma once

// -DDISABLE_DISCORD_RPC to disable at compile time (CI/CD, headless builds)

#ifndef DISABLE_DISCORD_RPC

#include <string>
#include <cstdint>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

namespace discord_rpc {

struct RichPresence {
	std::string state;
	std::string details;
	std::string largeImageKey;
	std::string largeImageText;
	std::string smallImageKey;
	std::string smallImageText;
	int64_t startTimestamp = 0;
	int64_t endTimestamp = 0;
};

class DiscordRPC {
public:
	explicit DiscordRPC(const std::string& client_id);
	~DiscordRPC();

	bool Connect();
	void Disconnect();
	void UpdatePresence(const RichPresence& presence);
	void ClearPresence();
	bool IsConnected() const;
	bool TryReconnect();

private:
	bool OpenPipe();
	void ClosePipe();
	bool WriteFrame(int opcode, const std::string& json);
	std::string ReadFrame();
	std::string BuildHandshakeJson();
	std::string BuildPresenceJson(const RichPresence& presence, int nonce);

	std::string m_client_id;
	std::atomic<bool> m_connected{false};
	int m_nonce = 1;
	std::mutex m_mutex;

	std::chrono::steady_clock::time_point m_last_reconnect_attempt{};
	int m_reconnect_failures = 0;

#ifdef _WIN32
	void* m_pipe = nullptr;
#else
	int m_pipe_fd = -1;
#endif
};

} // namespace discord_rpc

#else

#include <string>
#include <cstdint>

namespace discord_rpc {

struct RichPresence {
	std::string state;
	std::string details;
	std::string largeImageKey;
	std::string largeImageText;
	std::string smallImageKey;
	std::string smallImageText;
	int64_t startTimestamp = 0;
	int64_t endTimestamp = 0;
};

class DiscordRPC {
public:
	explicit DiscordRPC(const std::string&) {}
	~DiscordRPC() = default;
	bool Connect() { return false; }
	void Disconnect() {}
	void UpdatePresence(const RichPresence&) {}
	void ClearPresence() {}
	bool IsConnected() const { return false; }
	bool TryReconnect() { return false; }
};

} // namespace discord_rpc

#endif
