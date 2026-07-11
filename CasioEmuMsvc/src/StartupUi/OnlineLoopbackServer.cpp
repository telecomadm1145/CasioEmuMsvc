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
				const auto sent = send(socket, data.data(), data.size(), 0);
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

		std::string BuildSuccessResponse() {
			static constexpr std::string_view html =
				R"(<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light dark">
<title data-i18n-tag="Title">CasioEmuMsvc authorization complete</title>
<style>
  :root {
    color-scheme: light dark;
    --bg: #eef1f4;
    --card: #ffffff;
    --text: #17202a;
    --muted: #5d6977;
    --border: #d7dde5;
    --ok: #067647;
  }
  @media (prefers-color-scheme: dark) {
    :root {
      --bg: #11161d;
      --card: #18212b;
      --text: #eef3f8;
      --muted: #aab6c3;
      --border: #2a3848;
      --ok: #32d583;
    }
  }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    min-height: 100vh;
    display: grid;
    place-items: center;
    padding: 24px;
    background:
      radial-gradient(circle at top left, rgba(31, 111, 235, .18), transparent 32rem),
      var(--bg);
    color: var(--text);
    font: 16px/1.5 system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  }
  main {
    width: min(100%, 520px);
    padding: 32px;
    border: 1px solid var(--border);
    border-radius: 12px;
    background: var(--card);
    box-shadow: 0 18px 50px rgba(14, 23, 38, .16);
  }
  .eyebrow {
    margin: 0 0 8px;
    color: var(--muted);
    font-size: 13px;
    letter-spacing: .04em;
    text-transform: uppercase;
  }
  h1 {
    margin: 0 0 16px;
    font-size: 28px;
    line-height: 1.2;
  }
  p { margin: 0 0 18px; color: var(--muted); }
  .status {
    margin: 18px 0 0;
    padding: 12px 14px;
    border-radius: 8px;
    color: var(--ok);
    background: color-mix(in srgb, var(--card), var(--bg) 65%);
  }
</style>
<main>
  <p class="eyebrow">CasioEmuMsvc</p>
  <h1 data-i18n-tag="Heading">Authorization complete</h1>
  <p data-i18n-tag="Description">CasioEmuMsvc has received the authorization result and will continue automatically.</p>
  <div class="status" data-i18n-tag="CloseHint">You can close this page and return to CasioEmuMsvc.</div>
</main>
<script>
  const messages = {
    zh: {
      Title: '授权成功 - CasioEmuMsvc',
      Heading: '授权成功',
      Description: '已完成授权，CasioEmuMsvc 将继续完成登录。',
      CloseHint: '请关闭此页面，并返回 CasioEmuMsvc。',
    },
  };
  const preferredLanguage = new URLSearchParams(location.search).get('language') || '';
  const lang = (preferredLanguage || navigator.language || 'en').split('-')[0];
  const dict = messages[lang];
  if (dict) {
    document.documentElement.lang = lang;
    document.querySelectorAll('[data-i18n-tag]').forEach(el => {
      const text = dict[el.dataset.i18nTag];
      if (text) el.innerText = text;
    });
    document.title = dict.Title || document.title;
  }
</script>
</html>)";
			return "HTTP/1.1 200 OK\r\n"
				"Content-Type: text/html; charset=utf-8\r\n"
				"Cache-Control: no-store\r\n"
				"Connection: close\r\n"
				"Content-Length: " + std::to_string(html.size()) + "\r\n\r\n" + std::string(html);
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
					const auto ok = BuildSuccessResponse();
					SendAll(client, std::string_view(ok.data(), ok.size()));
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
