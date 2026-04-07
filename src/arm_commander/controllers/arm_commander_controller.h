#pragma once

#include <array>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include <std_msgs/msg/float32.hpp>
#include <std_msgs/msg/float32_multi_array.hpp>
#include <std_msgs/msg/string.hpp>

#include "ros/ros_interface.h"
#include "sensors/base/sensor_data_provider.h"

namespace ros2_android {

enum class ArmCommanderState {
  IDLE,
  AWAITING_ACK,
  AWAITING_DONE,
  WAIT_AFTER_DONE,
  WAIT_AFTER_NACK,
  NACK_LIMIT_EXCEEDED
};

const char* ArmCommanderStateName(ArmCommanderState state);

class ArmCommanderController : public SensorDataProvider {
 public:
  ArmCommanderController(RosInterface& ros);
  ~ArmCommanderController() override;

  // SensorDataProvider interface
  std::string PrettyName() const override;
  std::string GetLastMeasurementJson() override;
  bool GetLastMeasurement(jni::SensorReadingData& out_data) override;

  void Enable() override;
  void Disable() override;
  bool IsEnabled() const override { return enabled_; }

  // JNI-callable
  ArmCommanderState GetState() const;

 private:
  // Subscription callbacks (run on executor thread)
  void OnGoal(const std_msgs::msg::Float32MultiArray::SharedPtr msg);
  void OnAck(const std_msgs::msg::Float32::SharedPtr msg);
  void OnDone(const std_msgs::msg::Float32::SharedPtr msg);
  void OnNack(const std_msgs::msg::Float32::SharedPtr msg);

  // State machine
  void Loop();
  bool ShouldBlock();
  void SendCommand();
  void ResendLast();
  void TransitionTo(ArmCommanderState new_state);

  // ROS infrastructure
  RosInterface& ros_;
  bool enabled_ = false;

  Publisher<std_msgs::msg::Float32MultiArray> command_pub_;
  Publisher<std_msgs::msg::String> feedback_pub_;

  rclcpp::Subscription<std_msgs::msg::Float32MultiArray>::SharedPtr goal_sub_;
  rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr ack_sub_;
  rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr done_sub_;
  rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr nack_sub_;

  rclcpp::TimerBase::SharedPtr timer_;

  // State (protected by state_mutex_ for JNI access)
  mutable std::mutex state_mutex_;
  ArmCommanderState state_ = ArmCommanderState::IDLE;

  // Command tracking
  int msg_id_ = 0;
  int nack_count_ = 0;
  bool target_received_ = false;
  std::array<float, 7> current_goal_ = {};
  std::array<float, 7> last_sent_ = {};

  // Timing (steady_clock for monotonic timestamps)
  using Clock = std::chrono::steady_clock;
  using TimePoint = std::chrono::steady_clock::time_point;
  std::optional<TimePoint> last_sent_time_;
  std::optional<TimePoint> last_ack_time_;
  std::optional<TimePoint> transition_time_;
};

}  // namespace ros2_android
