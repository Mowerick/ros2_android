#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <chrono>
#include <unordered_map>

namespace ros2_android
{

  /**
   * Callback queue for perception debug frame updates
   *
   * Follows same pattern as CameraFrameCallbackQueue.
   * Used to notify Android UI when new annotated frames are available
   * for visualization.
   */
  class DebugFrameCallbackQueue
  {
  public:
    using CallbackType = std::function<void(const std::string &frame_id)>;

    static DebugFrameCallbackQueue &Instance()
    {
      static DebugFrameCallbackQueue instance;
      return instance;
    }

    void SetCallback(CallbackType callback)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      callback_ = std::move(callback);
    }

    /**
     * Post debug frame update notification
     * Throttled to avoid overwhelming JNI callback
     * @param frame_id Frame identifier ("rgb_annotated" or "depth_annotated")
     * @param min_interval Minimum time between notifications (default: 100ms = 10 FPS)
     */
    void Post(const std::string &frame_id,
              std::chrono::milliseconds min_interval = std::chrono::milliseconds(100))
    {
      auto now = std::chrono::steady_clock::now();

      CallbackType callback_copy;
      {
        std::lock_guard<std::mutex> lock(mutex_);

        // Check throttle
        auto &last_time = last_post_time_[frame_id];
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time);
        if (elapsed < min_interval)
        {
          return; // Skip this update
        }
        last_time = now;

        callback_copy = callback_;
      }

      // Call outside lock to avoid deadlock
      if (callback_copy)
      {
        callback_copy(frame_id);
      }
    }

  private:
    DebugFrameCallbackQueue() = default;
    DebugFrameCallbackQueue(const DebugFrameCallbackQueue &) = delete;
    DebugFrameCallbackQueue &operator=(const DebugFrameCallbackQueue &) = delete;

    std::mutex mutex_;
    CallbackType callback_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_post_time_;
  };

  /**
   * Helper function to post debug frame update
   * @param frame_id "rgb_annotated" or "depth_annotated"
   */
  inline void PostDebugFrameUpdate(const std::string &frame_id)
  {
    DebugFrameCallbackQueue::Instance().Post(frame_id);
  }

} // namespace ros2_android
