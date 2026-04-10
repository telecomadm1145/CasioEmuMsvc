#pragma once

// -DDISABLE_DISCORD_RPC to disable at compile time (CI/CD, headless builds)

#ifndef DISABLE_DISCORD_RPC

#include <string>
#include <cstdint>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

using namespace std;

namespace discord_rpc {

struct RichPresence {
	string state;
	string details;
	string largeImageKey;
	string largeImageText;
	string smallImageKey;
	string smallImageText;
	int64_t startTimestamp = 0;
	int64_t endTimestamp = 0;
};

class DiscordRPC {
public:
	explicit DiscordRPC(const string& client_id);
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
	bool WriteFrame(int opcode, const string& json);
	string ReadFrame();
	string BuildHandshakeJson();
	string BuildPresenceJson(const RichPresence& presence, int nonce);

	string m_client_id;
	atomic<bool> m_connected{false};
	int m_nonce = 1;
	mutex m_mutex;

	chrono::steady_clock::time_point m_last_reconnect_attempt{};
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

using namespace std;

namespace discord_rpc {

struct RichPresence {
	string state;
	string details;
	string largeImageKey;
	string largeImageText;
	string smallImageKey;
	string smallImageText;
	int64_t startTimestamp = 0;
	int64_t endTimestamp = 0;
};

class DiscordRPC {
public:
	explicit DiscordRPC(const string&) {}
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
