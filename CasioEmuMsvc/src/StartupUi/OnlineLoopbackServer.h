#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#else
using SOCKET = int;
#endif

namespace casioemu {
	class OnlineLoopbackServer {
	public:
		OnlineLoopbackServer();
		~OnlineLoopbackServer();

		OnlineLoopbackServer(const OnlineLoopbackServer&) = delete;
		OnlineLoopbackServer& operator=(const OnlineLoopbackServer&) = delete;

		std::string RedirectUri() const;
		bool Completed() const;
		std::string ApprovalGrant() const;
		std::string Error() const;
		void Stop();

	private:
		void Run();
		void SetError(const std::string& message);

		SOCKET listen_socket_;
		std::uint16_t port_{};
		std::string path_;
		std::thread thread_;
		std::atomic_bool stopping_{false};
		std::atomic_bool completed_{false};
		mutable std::mutex error_mutex_;
		std::string error_;
		mutable std::mutex grant_mutex_;
		std::string approval_grant_;
	};
}
