#include "OnlineLoopbackServer.h"

#include <array>
#include <chrono>
#include <cctype>
#include <random>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

namespace casioemu {
	namespace {
		std::string MakeRandomPath() {
			static constexpr char alphabet[] = "0123456789abcdef";
			std::random_device rd;
			std::uniform_int_distribution<int> dist(0, 15);
			std::string token;
			token.reserve(32);
			for (int i = 0; i < 32; ++i) token.push_back(alphabet[dist(rd)]);
			return "/casioemu-auth-" + token;
		}

		void CloseSocket(SOCKET socket) {
			if (socket == INVALID_SOCKET) return;
#ifdef _WIN32
			closesocket(socket);
#else
			close(socket);
#endif
		}

		void SendAll(SOCKET socket, std::string_view data) {
			while (!data.empty()) {
#ifdef _WIN32
				const int sent = send(socket, data.data(), static_cast<int>(data.size()), 0);
#else
				#ifdef MSG_NOSIGNAL
				const auto sent = send(socket, data.data(), data.size(), MSG_NOSIGNAL);
				#else
				const auto sent = send(socket, data.data(), data.size(), 0);
				#endif
#endif
				if (sent <= 0) return;
				data.remove_prefix(static_cast<std::size_t>(sent));
			}
		}

		std::string_view RequestTarget(std::string_view request) {
			const auto first_space = request.find(' ');
			if (first_space == std::string_view::npos) return {};
			const auto second_space = request.find(' ', first_space + 1);
			if (second_space == std::string_view::npos) return {};
			return request.substr(first_space + 1, second_space - first_space - 1);
		}

		bool RequestMatchesPath(std::string_view target, const std::string& path) {
			if (target.size() < path.size() || target.substr(0, path.size()) != path) return false;
			return target.size() == path.size() || target[path.size()] == '?';
		}

		std::string ApprovalGrantFromTarget(std::string_view target) {
			const auto query = target.find('?');
			if (query == std::string_view::npos) return {};
			constexpr std::string_view name = "approval_grant=";
			auto start = target.find(name, query + 1);
			if (start == std::string_view::npos || (start > query + 1 && target[start - 1] != '&')) return {};
			start += name.size();
			const auto end = target.find('&', start);
			const auto value = target.substr(start, end == std::string_view::npos ? target.size() - start : end - start);
			if (value.empty() || value.size() > 4096) return {};
			for (const char c : value) {
				if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.') return {};
			}
			return std::string(value);
		}

	}

	OnlineLoopbackServer::OnlineLoopbackServer()
		: listen_socket_(INVALID_SOCKET), path_(MakeRandomPath()) {
#ifdef _WIN32
		WSADATA data{};
		if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
			throw std::runtime_error("Failed to initialize loopback socket.");
#endif
		listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listen_socket_ == INVALID_SOCKET) {
#ifdef _WIN32
			WSACleanup();
#endif
			throw std::runtime_error("Failed to create loopback socket.");
		}

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = 0;
		if (bind(listen_socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
			listen(listen_socket_, 4) == SOCKET_ERROR) {
			CloseSocket(listen_socket_);
#ifdef _WIN32
			WSACleanup();
#endif
			throw std::runtime_error("Failed to start loopback authorization listener.");
		}

		sockaddr_in bound{};
#ifdef _WIN32
		int bound_size = sizeof(bound);
#else
		socklen_t bound_size = sizeof(bound);
#endif
		if (getsockname(listen_socket_, reinterpret_cast<sockaddr*>(&bound), &bound_size) == SOCKET_ERROR) {
			CloseSocket(listen_socket_);
#ifdef _WIN32
			WSACleanup();
#endif
			throw std::runtime_error("Failed to read loopback authorization port.");
		}
		port_ = ntohs(bound.sin_port);
		thread_ = std::thread([this] { Run(); });
	}

	OnlineLoopbackServer::~OnlineLoopbackServer() {
		Stop();
#ifdef _WIN32
		WSACleanup();
#endif
	}

	std::string OnlineLoopbackServer::RedirectUri() const {
		return "http://127.0.0.1:" + std::to_string(port_) + path_;
	}

	bool OnlineLoopbackServer::Completed() const {
		return completed_.load();
	}

	std::string OnlineLoopbackServer::ApprovalGrant() const {
		std::lock_guard<std::mutex> lock(grant_mutex_);
		return approval_grant_;
	}

	std::string OnlineLoopbackServer::Error() const {
		std::lock_guard<std::mutex> lock(error_mutex_);
		return error_;
	}

	void OnlineLoopbackServer::Stop() {
		stopping_.store(true);
		if (thread_.joinable()) thread_.join();
	}

	void OnlineLoopbackServer::SetError(const std::string& message) {
		std::lock_guard<std::mutex> lock(error_mutex_);
		error_ = message;
	}

	void OnlineLoopbackServer::Run() {
		while (!stopping_.load() && !completed_.load()) {
			fd_set read_set;
			FD_ZERO(&read_set);
			FD_SET(listen_socket_, &read_set);
			timeval timeout{};
			timeout.tv_sec = 0;
			timeout.tv_usec = 250000;
			const int ready = select(static_cast<int>(listen_socket_ + 1), &read_set, nullptr, nullptr, &timeout);
			if (ready == 0) continue;
			if (ready == SOCKET_ERROR) {
				if (!stopping_.load()) SetError("Loopback authorization listener failed.");
				break;
			}
			sockaddr_in remote{};
#ifdef _WIN32
			int remote_size = sizeof(remote);
#else
			socklen_t remote_size = sizeof(remote);
#endif
			const SOCKET client = accept(listen_socket_, reinterpret_cast<sockaddr*>(&remote), &remote_size);
			if (client == INVALID_SOCKET) continue;
#ifndef _WIN32
#ifdef SO_NOSIGPIPE
			int no_sigpipe = 1;
			setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
#endif
#ifdef _WIN32
			const DWORD receive_timeout_ms = 250;
			setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
				reinterpret_cast<const char*>(&receive_timeout_ms), sizeof(receive_timeout_ms));
#else
			timeval receive_timeout{};
			receive_timeout.tv_sec = 0;
			receive_timeout.tv_usec = 250000;
			setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout));
#endif
			std::array<char, 2048> buffer{};
			std::string request;
			request.reserve(buffer.size());
			while (request.size() < 8192 && request.find("\r\n\r\n") == std::string::npos) {
#ifdef _WIN32
				const int received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
				const auto received = recv(client, buffer.data(), buffer.size(), 0);
#endif
				if (received <= 0) break;
				request.append(buffer.data(), static_cast<std::size_t>(received));
			}
			const auto target = RequestTarget(request);
			const bool matched = RequestMatchesPath(target, path_);
			if (matched) {
				const auto grant = ApprovalGrantFromTarget(target);
				if (!grant.empty()) {
					{
						std::lock_guard<std::mutex> lock(grant_mutex_);
						approval_grant_ = grant;
					}
					static constexpr std::string_view no_content =
						"HTTP/1.1 204 No Content\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
					SendAll(client, no_content);
					completed_.store(true);
				}
				else {
					static constexpr std::string_view bad_request =
						"HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
					SendAll(client, bad_request);
					SetError("Authorization callback did not contain a valid approval grant.");
				}
			}
			else {
				static constexpr std::string_view not_found =
					"HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
				SendAll(client, not_found);
			}
			CloseSocket(client);
		}
		CloseSocket(listen_socket_);
		listen_socket_ = INVALID_SOCKET;
	}
}
