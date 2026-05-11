#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include <rclcpp/rclcpp.hpp>

#include <geometry_msgs/msg/point.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <std_msgs/msg/float32_multi_array.hpp>
#include <vermin_collector_ros_msgs/msg/command.hpp>
#include <vermin_collector_ros_msgs/msg/feedback.hpp>

#include "ros/ros_interface.h"
#include "sensors/base/sensor_data_provider.h"

namespace ros2_android {

enum class TargetManagerState {
  INIT,
  HARDHOME,
  SETUP_PHASE,
  CALIBRATING,
  SOFTHOME,
  READY,
  FIXED_POSITION_MODE,
  SENT_CMD,
  EXECUTING,
};

// Desired motor configuration (sent to ESP32, compared against Feedback echo-back).
struct DesiredSetup {
  std::array<uint16_t, 3> frequencies = {1000, 1000, 1000};
  std::array<uint8_t, 3> en_motors = {0, 0, 0};  // starts disabled (motor gate)
  uint8_t resolution = 8;                          // r_coarse initial
};

// Full snapshot of the last received ESP32_Feedback message.
struct LatestFeedback {
  uint8_t state = 0;
  std::array<int32_t, 3> current_steps = {0, 0, 0};
  std::array<uint16_t, 3> frequencies = {0, 0, 0};
  std::array<uint8_t, 3> en_motors = {0, 0, 0};
  uint8_t resolution = 0;
};

const char* TargetManagerStateName(TargetManagerState state);

class TargetManagerController : public SensorDataProvider {
 public:
  TargetManagerController(RosInterface& ros);
  ~TargetManagerController() override;

  // SensorDataProvider interface
  std::string PrettyName() const override;
  std::string GetLastMeasurementJson() override;
  bool GetLastMeasurement(jni::SensorReadingData& out_data) override;

  void Enable() override;
  void Disable() override;
  bool IsEnabled() const override { return enabled_; }

  // JNI-callable controls
  void SetFixedPositionMode(bool enabled);
  bool IsFixedPositionMode() const;
  TargetManagerState GetState() const;

 private:
  // Subscription callbacks (run on executor thread - no mutual exclusion needed)
  void OnTarget(const geometry_msgs::msg::Point::SharedPtr msg);
  void OnImu(const sensor_msgs::msg::Imu::SharedPtr msg);
  void OnFeedback(const vermin_collector_ros_msgs::msg::Feedback::SharedPtr msg);
  void OnFixedPosition(const std_msgs::msg::Float32MultiArray::SharedPtr msg);

  // State machine (100ms timer)
  void StateMachineCallback();
  void TickInit();
  void TickHardhome();
  void TickSetupPhase();
  void TickCalibrating();
  void TickSofthome();
  void TickReady();
  void TickExecuting();

  // Helpers (caller holds state_mutex_)
  bool MotorGateOpen();
  std::array<uint8_t, 3> EffectiveEnable();
  bool SetupEchoed() const;
  void SendCurrentSetup();
  void SendHardHoming();
  void SendSoftHoming();
  void SendTarget(const geometry_msgs::msg::Point& msg);
  void ReturnToZero();

  // Math (static, pure)
  static std::tuple<float, float, float> GravityInCamera(
      float qx, float qy, float qz, float qw);
  static std::pair<float, float> ComputeRollTiltDegrees(float tx, float ty, float tz);
  static void ClampRollTiltAngles(float& roll, float& tilt);
  static int32_t DegreesToSteps(float deg, float steps_per_deg_at_res1, uint8_t resolution);
  static uint8_t SelectResolution(int32_t delta_at_res1,
                                   int32_t t_coarse, int32_t t_fine,
                                   uint8_t r_coarse, uint8_t r_mid, uint8_t r_fine);

  // ROS infrastructure
  RosInterface& ros_;
  bool enabled_ = false;

  Publisher<vermin_collector_ros_msgs::msg::Command> command_pub_;

  rclcpp::Subscription<geometry_msgs::msg::Point>::SharedPtr target_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr imu_sub_;
  rclcpp::Subscription<vermin_collector_ros_msgs::msg::Feedback>::SharedPtr feedback_sub_;
  rclcpp::Subscription<std_msgs::msg::Float32MultiArray>::SharedPtr fixed_pos_sub_;

  rclcpp::TimerBase::SharedPtr timer_;

  // State (protected by state_mutex_ for JNI access)
  mutable std::mutex state_mutex_;
  TargetManagerState state_ = TargetManagerState::INIT;
  bool fixed_position_mode_ = false;

  // Motor enable gate: motors are disabled for kMotorEnableDelayS after Enable()
  std::chrono::steady_clock::time_point t_start_;
  bool motors_latched_ = false;

  // Boot phase one-shot flags
  bool hh_seen_non_ready_ = false;       // HARDHOME: saw firmware leave READY
  bool softhome_seen_non_ready_ = false;  // SOFTHOME: saw firmware leave READY
  std::chrono::steady_clock::time_point softhome_enter_time_;  // timeout fallback

  // Desired motor config (compared against Feedback for echo-back and drift detection)
  DesiredSetup desired_setup_;

  // Latest Feedback snapshot (all fields, not just state)
  std::optional<LatestFeedback> latest_feedback_;

  // IMU: full 4-component quaternion for GravityInCamera()
  std::optional<std::array<float, 4>> imu_quat_;

  // Pending queued action (post-fire return-to-zero, dispatched from TickReady)
  std::optional<vermin_collector_ros_msgs::msg::Command> pending_action_;

  // Last fixed position (deduplication)
  std::array<float, 2> last_fixed_position_ = {0.0f, 0.0f};
};

}  // namespace ros2_android
