#include "UpdateChecker.h"
#include "Config.hpp"
#include "../../../McpPlugin/json.hpp"
#ifndef __ANDROID__
#include <curl/curl.h>
#else
#include <jni.h>
#include <SDL_system.h>
#endif
#include <chrono>
#include <stdexcept>
#include <SDL.h>

namespace casioemu {
namespace {
constexpr const char* kApi = "https://api.github.com/repos/telecomadm1145/CasioEmuMsvc/releases?per_page=1";
constexpr std::size_t kMaxResponseSize = 1024 * 1024;
size_t Write(void* p, size_t s, size_t n, void* u) {
	const auto bytes = s * n; auto& body = *static_cast<std::string*>(u);
	if (bytes > kMaxResponseSize || body.size() > kMaxResponseSize - bytes) return 0;
	body.append(static_cast<char*>(p), bytes); return bytes;
}
UpdateInfo Check() {
	std::string body;
	const char* api = kApi;
	SDL_Log("[UpdateChecker] Checking releases: %s", api);
#ifdef __ANDROID__
	auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
	if (!env) throw std::runtime_error("Android runtime unavailable");
	jobject activity = static_cast<jobject>(SDL_AndroidGetActivity()); jclass cls = env->GetObjectClass(activity);
	jmethodID method = env->GetStaticMethodID(cls, "checkUpdate", "(Ljava/lang/String;)[B");
	if (!method) throw std::runtime_error("Android update API unavailable");
	jstring url = env->NewStringUTF(api); auto bytes = static_cast<jbyteArray>(env->CallStaticObjectMethod(cls, method, url));
	env->DeleteLocalRef(url); if (!bytes) throw std::runtime_error("release request failed");
	const jsize len = env->GetArrayLength(bytes); body.assign(static_cast<size_t>(len), '\0');
	env->GetByteArrayRegion(bytes, 0, len, reinterpret_cast<jbyte*>(body.data())); env->DeleteLocalRef(bytes);
#else
	CURL* c = curl_easy_init(); if (!c) throw std::runtime_error("curl init failed");
	curl_slist* h = nullptr;
	h = curl_slist_append(h, "Accept: application/vnd.github+json");
	h = curl_slist_append(h, "User-Agent: CasioEmuMsvc-update-checker");
	curl_easy_setopt(c, CURLOPT_URL, api); curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 10000L); curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, Write); curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	const auto code = curl_easy_perform(c); long status = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
	SDL_Log("[UpdateChecker] HTTP result: curl=%d status=%ld bytes=%zu", static_cast<int>(code), status, body.size());
	curl_slist_free_all(h); curl_easy_cleanup(c); if (code != CURLE_OK || status < 200 || status >= 300) throw std::runtime_error("release request failed");

#endif
	const auto releases = nlohmann::json::parse(body); if (!releases.is_array() || releases.empty()) { SDL_Log("[UpdateChecker] No releases returned"); return {}; }
	const auto& json = releases.front(); UpdateInfo r{json.value("tag_name", ""), json.value("html_url", ""), json.value("name", "")};
	SDL_Log("[UpdateChecker] Local commit=%s ref=%s remote tag=%s", GIT_COMMIT_HASH, GIT_LATEST_TAG, r.tag.c_str());
	if (r.tag.empty() || r.tag == GIT_LATEST_TAG || r.tag.find(GIT_COMMIT_HASH) != std::string::npos) { SDL_Log("[UpdateChecker] No update available"); return {}; }
	SDL_Log("[UpdateChecker] Update available: %s", r.tag.c_str());
	return r;
}
}
UpdateChecker::UpdateChecker() : state_(std::make_shared<State>()) {}
UpdateChecker::~UpdateChecker() = default;
void UpdateChecker::Start() {
	if (started_.exchange(true)) return;
	auto state = state_;
	std::thread([state]() {
		UpdateInfo result;
		try { result = Check(); }
		catch (const std::exception& e) { SDL_Log("[UpdateChecker] Check failed: %s", e.what()); }
		catch (...) { SDL_Log("[UpdateChecker] Check failed: unknown error"); }
		{ std::lock_guard lock(state->mutex); state->result = std::move(result); }
		state->ready.store(true, std::memory_order_release);
	}).detach();
}
bool UpdateChecker::Ready() const { return state_->ready.load(std::memory_order_acquire); }
UpdateInfo UpdateChecker::TakeResult() { std::lock_guard lock(state_->mutex); return state_->result; }
}
