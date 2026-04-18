#include "DiscordRPC.h"

#ifndef DISABLE_DISCORD_RPC

#include <cstring>
#include <cstdio>
#include <sstream>
#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

using namespace std;

namespace discord_rpc {

enum OpCode {
	OP_HANDSHAKE = 0,
	OP_FRAME = 1,
	OP_CLOSE = 2,
	OP_PING = 3,
	OP_PONG = 4,
};

struct FrameHeader {
	uint32_t opcode;
	uint32_t length;
};

static string JsonEscape(const string& s) {
	string result;
	result.reserve(s.size() + 8);
	for (char c : s) {
		switch (c) {
		case '"':  result += "\\\""; break;
		case '\\': result += "\\\\"; break;
		case '\n': result += "\\n"; break;
		case '\r': result += "\\r"; break;
		case '\t': result += "\\t"; break;
		default:   result += c; break;
		}
	}
	return result;
}

DiscordRPC::DiscordRPC(const string& client_id)
	: m_client_id(client_id) {
}

DiscordRPC::~DiscordRPC() {
	try {
		Disconnect();
	} catch (...) {}
}

bool DiscordRPC::IsConnected() const {
	return m_connected.load();
}

bool DiscordRPC::Connect() {
	lock_guard<mutex> lock(m_mutex);
	if (m_connected)
		return true;

	if (!OpenPipe())
		return false;

	string handshake = BuildHandshakeJson();
	if (!WriteFrame(OP_HANDSHAKE, handshake)) {
		ClosePipe();
		return false;
	}

	string response = ReadFrame();
	if (response.empty()) {
		ClosePipe();
		return false;
	}

	m_connected = true;
	m_reconnect_failures = 0;
	printf("[discord rpc] connected\n");
	return true;
}

void DiscordRPC::Disconnect() {
	lock_guard<mutex> lock(m_mutex);
	if (!m_connected)
		return;
	m_connected = false;
	ClosePipe();
	printf("[discord rpc] disconnected\n");
}

bool DiscordRPC::TryReconnect() {
	if (m_connected)
		return true;

	auto now = chrono::steady_clock::now();
	int backoff_seconds = 60 * (1 << min(m_reconnect_failures, 4));
	if (now - m_last_reconnect_attempt < chrono::seconds(backoff_seconds))
		return false;

	m_last_reconnect_attempt = now;

	if (Connect())
		return true;

	m_reconnect_failures++;
	return false;
}

void DiscordRPC::UpdatePresence(const RichPresence& presence) {
	lock_guard<mutex> lock(m_mutex);
	if (!m_connected)
		return;

	string json = BuildPresenceJson(presence, m_nonce++);
	if (!WriteFrame(OP_FRAME, json)) {
		m_connected = false;
		ClosePipe();
		printf("[discord rpc] lost connection\n");
	}
}

void DiscordRPC::ClearPresence() {
	lock_guard<mutex> lock(m_mutex);
	if (!m_connected)
		return;

	ostringstream ss;
	ss << "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" 
#ifdef _WIN32
	   << GetCurrentProcessId()
#else
	   << getpid()
#endif
	   << ",\"activity\":null},\"nonce\":\"" << m_nonce++ << "\"}";

	if (!WriteFrame(OP_FRAME, ss.str())) {
		m_connected = false;
		ClosePipe();
	}
}

string DiscordRPC::BuildHandshakeJson() {
	return "{\"v\":1,\"client_id\":\"" + m_client_id + "\"}";
}

string DiscordRPC::BuildPresenceJson(const RichPresence& presence, int nonce) {
	ostringstream ss;
	ss << "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":";
#ifdef _WIN32
	ss << GetCurrentProcessId();
#else
	ss << getpid();
#endif
	ss << ",\"activity\":{";
	
	bool first = true;
	
	if (!presence.state.empty()) {
		ss << "\"state\":\"" << JsonEscape(presence.state) << "\"";
		first = false;
	}
	if (!presence.details.empty()) {
		if (!first) ss << ",";
		ss << "\"details\":\"" << JsonEscape(presence.details) << "\"";
		first = false;
	}
	
	if (presence.startTimestamp != 0 || presence.endTimestamp != 0) {
		if (!first) ss << ",";
		ss << "\"timestamps\":{";
		bool tfirst = true;
		if (presence.startTimestamp != 0) {
			ss << "\"start\":" << presence.startTimestamp;
			tfirst = false;
		}
		if (presence.endTimestamp != 0) {
			if (!tfirst) ss << ",";
			ss << "\"end\":" << presence.endTimestamp;
		}
		ss << "}";
		first = false;
	}
	
	bool hasAssets = !presence.largeImageKey.empty() || !presence.smallImageKey.empty();
	if (hasAssets) {
		if (!first) ss << ",";
		ss << "\"assets\":{";
		bool afirst = true;
		if (!presence.largeImageKey.empty()) {
			ss << "\"large_image\":\"" << JsonEscape(presence.largeImageKey) << "\"";
			afirst = false;
			if (!presence.largeImageText.empty()) {
				ss << ",\"large_text\":\"" << JsonEscape(presence.largeImageText) << "\"";
			}
		}
		if (!presence.smallImageKey.empty()) {
			if (!afirst) ss << ",";
			ss << "\"small_image\":\"" << JsonEscape(presence.smallImageKey) << "\"";
			if (!presence.smallImageText.empty()) {
				ss << ",\"small_text\":\"" << JsonEscape(presence.smallImageText) << "\"";
			}
		}
		ss << "}";
		first = false;
	}
	
	ss << "}},\"nonce\":\"" << nonce << "\"}";
	return ss.str();
}

#ifdef _WIN32

bool DiscordRPC::OpenPipe() {
	for (int i = 0; i < 10; i++) {
		char pipeName[64];
		snprintf(pipeName, sizeof(pipeName), "\\\\.\\pipe\\discord-ipc-%d", i);

		m_pipe = CreateFileA(
			pipeName,
			GENERIC_READ | GENERIC_WRITE,
			0, NULL,
			OPEN_EXISTING,
			0, NULL);

		if (m_pipe != INVALID_HANDLE_VALUE)
			return true;
	}
	m_pipe = nullptr;
	return false;
}

void DiscordRPC::ClosePipe() {
	if (m_pipe && m_pipe != INVALID_HANDLE_VALUE)
		CloseHandle(m_pipe);
	m_pipe = nullptr;
}

bool DiscordRPC::WriteFrame(int opcode, const string& json) {
	if (!m_pipe || m_pipe == INVALID_HANDLE_VALUE)
		return false;

	FrameHeader header;
	header.opcode = (uint32_t)opcode;
	header.length = (uint32_t)json.size();

	DWORD written;
	if (!WriteFile(m_pipe, &header, sizeof(header), &written, NULL))
		return false;
	if (!WriteFile(m_pipe, json.c_str(), (DWORD)json.size(), &written, NULL))
		return false;

	return true;
}

string DiscordRPC::ReadFrame() {
	if (!m_pipe || m_pipe == INVALID_HANDLE_VALUE)
		return "";

	FrameHeader header;
	DWORD bytesRead;
	if (!ReadFile(m_pipe, &header, sizeof(header), &bytesRead, NULL))
		return "";
	if (bytesRead != sizeof(header))
		return "";
	if (header.length > 65536)
		return "";

	string data(header.length, '\0');
	if (!ReadFile(m_pipe, &data[0], header.length, &bytesRead, NULL))
		return "";
	if (bytesRead != header.length)
		return "";

	return data;
}

#else

static string GetDiscordSocketPath(int pipeNum) {
	string base;

	const char* xdg = getenv("XDG_RUNTIME_DIR");
	if (xdg) {
		base = xdg;
	} else {
		const char* tmpdir = getenv("TMPDIR");
		base = tmpdir ? tmpdir : "/tmp";
	}

	char path[256];
	snprintf(path, sizeof(path), "%s/discord-ipc-%d", base.c_str(), pipeNum);
	return path;
}

bool DiscordRPC::OpenPipe() {
	for (int i = 0; i < 10; i++) {
		string socketPath = GetDiscordSocketPath(i);

		m_pipe_fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (m_pipe_fd < 0)
			continue;

		struct sockaddr_un addr;
		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

		if (connect(m_pipe_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0)
			return true;

		close(m_pipe_fd);
		m_pipe_fd = -1;
	}

	// snap/flatpak paths
	const char* xdg = getenv("XDG_RUNTIME_DIR");
	if (xdg) {
		const char* appDirs[] = {
			"app/com.discordapp.Discord",
			"snap.discord",
		};
		for (const char* appDir : appDirs) {
			for (int i = 0; i < 10; i++) {
				char socketPath[512];
				snprintf(socketPath, sizeof(socketPath), "%s/%s/discord-ipc-%d", xdg, appDir, i);

				m_pipe_fd = socket(AF_UNIX, SOCK_STREAM, 0);
				if (m_pipe_fd < 0) continue;

				struct sockaddr_un addr;
				memset(&addr, 0, sizeof(addr));
				addr.sun_family = AF_UNIX;
				strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path) - 1);

				if (connect(m_pipe_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0)
					return true;

				close(m_pipe_fd);
				m_pipe_fd = -1;
			}
		}
	}

	return false;
}

void DiscordRPC::ClosePipe() {
	if (m_pipe_fd >= 0)
		close(m_pipe_fd);
	m_pipe_fd = -1;
}

bool DiscordRPC::WriteFrame(int opcode, const string& json) {
	if (m_pipe_fd < 0)
		return false;

	FrameHeader header;
	header.opcode = (uint32_t)opcode;
	header.length = (uint32_t)json.size();

	if (write(m_pipe_fd, &header, sizeof(header)) != sizeof(header))
		return false;
	if (write(m_pipe_fd, json.c_str(), json.size()) != (ssize_t)json.size())
		return false;

	return true;
}

string DiscordRPC::ReadFrame() {
	if (m_pipe_fd < 0)
		return "";

	FrameHeader header;
	if (read(m_pipe_fd, &header, sizeof(header)) != sizeof(header))
		return "";
	if (header.length > 65536)
		return "";

	string data(header.length, '\0');
	ssize_t total = 0;
	while (total < (ssize_t)header.length) {
		ssize_t r = read(m_pipe_fd, &data[total], header.length - total);
		if (r <= 0)
			return "";
		total += r;
	}

	return data;
}

#endif

} // namespace discord_rpc

#endif
