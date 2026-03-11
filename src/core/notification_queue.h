#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace ros2_android {

enum class NotificationSeverity { WARNING, ERROR };

struct Notification {
  NotificationSeverity severity;
  std::string message;
};

class NotificationQueue {
 public:
  using CallbackType = std::function<void(NotificationSeverity, const std::string&)>;

  static NotificationQueue& Instance() {
    static NotificationQueue instance;
    return instance;
  }

  void SetCallback(CallbackType callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = std::move(callback);
  }

  void Post(NotificationSeverity severity, const std::string& message) {
    CallbackType callback_copy;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (queue_.size() >= kMaxEntries) {
        queue_.erase(queue_.begin());
      }
      queue_.push_back({severity, message});
      callback_copy = callback_;
    }
    // Call callback outside the lock to avoid deadlock
    if (callback_copy) {
      callback_copy(severity, message);
    }
  }

  void Drain(std::vector<Notification>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    out.swap(queue_);
    queue_.clear();
  }

 private:
  NotificationQueue() = default;
  NotificationQueue(const NotificationQueue&) = delete;
  NotificationQueue& operator=(const NotificationQueue&) = delete;

  static constexpr size_t kMaxEntries = 50;
  std::mutex mutex_;
  std::vector<Notification> queue_;
  CallbackType callback_;
};

inline void PostNotification(NotificationSeverity severity,
                             const std::string& message) {
  NotificationQueue::Instance().Post(severity, message);
}

}  // namespace ros2_android
