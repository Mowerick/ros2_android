#include "ros/ros_interface.h"

#include <chrono>
#include <fstream>
#include <thread>

#include "core/log.h"
#include "core/notification_queue.h"

using ros2_android::RosInterface;

RosInterface::RosInterface() : device_id_("android") {}

RosInterface::RosInterface(const std::string& device_id)
    : device_id_(device_id.empty() ? "android" : device_id) {}

RosInterface::~RosInterface() {
  LOGI("RosInterface destructor called");

  // Shutdown context and wait for executor thread to finish
  if (context_ && context_->is_valid()) {
    try {
      context_->shutdown("RosInterface destructor");
    } catch (const std::exception& e) {
      LOGE("Exception during context shutdown: %s", e.what());
    }
  }

  if (executor_thread_.joinable()) {
    LOGI("Joining executor thread");
    executor_thread_.join();
  }

  LOGI("RosInterface destructor complete");
}

bool RosInterface::Initialize(size_t ros_domain_id) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    try {
      if (attempt > 0) {
        LOGW("ROS init retry %d - cleaning up stale state", attempt);
        // Clean up any partial state from the failed attempt
        node_.reset();
        if (context_ && context_->is_valid()) {
          context_->shutdown("cleanup before retry");
        }
        context_.reset();
        // Brief pause to let OS release sockets/resources
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }

      // Diagnostic: verify env vars and config file before DDS init
      const char* uri = getenv("CYCLONEDDS_URI");
      const char* domain = getenv("ROS_DOMAIN_ID");
      const char* rmw = getenv("RMW_IMPLEMENTATION");
      LOGD("CYCLONEDDS_URI=%s", uri ? uri : "(null)");
      LOGD("ROS_DOMAIN_ID=%s", domain ? domain : "(null)");
      LOGD("RMW_IMPLEMENTATION=%s", rmw ? rmw : "(null)");

      if (uri) {
        std::ifstream cfg(uri);
        if (cfg.is_open()) {
          std::string content((std::istreambuf_iterator<char>(cfg)),
                               std::istreambuf_iterator<char>());
          LOGD("Config file (%zu bytes):\n%s", content.size(), content.c_str());
        } else {
          LOGE("Cannot open config file: %s", uri);
        }
      }

      rclcpp::InitOptions init_options;
      init_options.set_domain_id(ros_domain_id);
      init_options.shutdown_on_signal = false;
      context_ = std::make_shared<rclcpp::Context>();
      context_->init(0, nullptr, init_options);

      rclcpp::NodeOptions node_options;
      node_options.context(context_);
      std::string node_name = device_id_ + "_node";
      node_ = std::make_shared<rclcpp::Node>(node_name, node_options);

      rclcpp::ExecutorOptions executor_options;
      executor_options.context = context_;
      executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>(
          executor_options);
      executor_->add_node(node_);

      executor_thread_ = std::thread(&rclcpp::Executor::spin, executor_.get());

      NotifyInitChanged();
      return true;

    } catch (const std::exception& e) {
      LOGE("ROS initialization failed (attempt %d): %s", attempt + 1, e.what());
    }
  }

  LOGE("ROS initialization failed after retries");
  PostNotification(NotificationSeverity::ERROR,
      "Failed to start ROS 2 - DDS domain creation failed. "
      "Try force-stopping the app or restarting the device.");
  return false;
}

bool RosInterface::Initialized() const {
  return context_ && context_->is_valid();
}

rclcpp::Context::SharedPtr RosInterface::get_context() const {
  return context_;
}

rclcpp::Node::SharedPtr RosInterface::get_node() const { return node_; }

const std::string& RosInterface::GetDeviceId() const { return device_id_; }

ros2_android::ObserverId RosInterface::AddObserver(std::function<void(void)> init_or_shutdown) {
  std::lock_guard<std::mutex> lock(observers_mutex_);
  ros2_android::ObserverId id = next_observer_id_++;
  observers_[id] = init_or_shutdown;
  return id;
}

void RosInterface::RemoveObserver(ros2_android::ObserverId id) {
  std::lock_guard<std::mutex> lock(observers_mutex_);
  auto it = observers_.find(id);
  if (it != observers_.end()) {
    observers_.erase(it);
    LOGI("Removed observer with ID %llu", static_cast<unsigned long long>(id));
  }
}

void RosInterface::NotifyInitChanged() {
  std::map<ros2_android::ObserverId, std::function<void(void)>> observers_copy;
  {
    std::lock_guard<std::mutex> lock(observers_mutex_);
    observers_copy = observers_;
    observers_.clear();
  }

  // Invoke observers outside the lock to avoid deadlock
  for (const auto& [id, observer] : observers_copy) {
    LOGI("Notifying observer ID %llu", static_cast<unsigned long long>(id));
    observer();
  }
}
