// Lightweight thread-safe event bus for decoupling modules
#pragma once

#include <any>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace util {

class EventBus {
public:
  using ListenerId = std::uint64_t;
  using Callback = std::function<void(const std::any& payload)>;

  ListenerId subscribe(const std::string& topic, Callback callback) {
    std::lock_guard<std::mutex> guard(mutex_);
    const ListenerId id = ++last_id_;
    topic_to_listeners_[topic].emplace(id, std::move(callback));
    return id;
  }

  void unsubscribe(const std::string& topic, ListenerId id) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto topic_iter = topic_to_listeners_.find(topic);
    if (topic_iter == topic_to_listeners_.end()) return;
    topic_iter->second.erase(id);
    if (topic_iter->second.empty()) {
      topic_to_listeners_.erase(topic_iter);
    }
  }

  void publish(const std::string& topic, const std::any& payload = {}) {
    // Copy listeners to avoid holding the lock while invoking callbacks
    std::unordered_map<ListenerId, Callback> listeners_copy;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      auto iter = topic_to_listeners_.find(topic);
      if (iter == topic_to_listeners_.end()) return;
      listeners_copy = iter->second;
    }
    for (auto& kv : listeners_copy) {
      try {
        kv.second(payload);
      } catch (...) {
        // Swallow exceptions from listeners to keep the bus robust
      }
    }
  }

private:
  std::mutex mutex_{};
  std::unordered_map<std::string, std::unordered_map<ListenerId, Callback>> topic_to_listeners_{};
  ListenerId last_id_ = 0;
};

} // namespace util



