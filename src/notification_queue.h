#pragma once

#include <cstdint>
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
  static NotificationQueue& Instance() {
    static NotificationQueue instance;
    return instance;
  }

  void Post(NotificationSeverity severity, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= kMaxEntries) {
      queue_.erase(queue_.begin());
    }
    queue_.push_back({severity, message});
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
};

inline void PostNotification(NotificationSeverity severity,
                             const std::string& message) {
  NotificationQueue::Instance().Post(severity, message);
}

}  // namespace ros2_android
