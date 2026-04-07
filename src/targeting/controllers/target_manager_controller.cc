#include "targeting/controllers/target_manager_controller.h"

#include <cmath>
#include <sstream>

#include "core/log.h"
#include "core/notification_queue.h"

namespace {

// Physical offsets (meters) - from Python target_manager.py
constexpr float kCameraOffsetX = 0.06f;
constexpr float kCameraOffsetY = -0.023f;
constexpr float kCameraOffsetZ = 0.035f;

constexpr float kLaserOffsetX = 0.0f;
constexpr float kLaserOffsetY = 0.025f;
constexpr float kLaserOffsetZ = 0.0f;

// Pan/tilt limits (degrees)
constexpr float kPanMin = -90.0f;
constexpr float kPanMax = 90.0f;
constexpr float kTiltMin = -30.0f;
constexpr float kTiltMax = 40.0f;

// Shooting dwell time (milliseconds)
constexpr float kShootingTimeMs = 1000.0f;

// State machine tick rate
constexpr int kTimerMs = 100;

// Epsilon for atan2 denominator
constexpr float kEpsilon = 0.00001f;

constexpr float kRadToDeg = 180.0f / static_cast<float>(M_PI);

}  // namespace

namespace ros2_android {

const char* TargetManagerStateName(TargetManagerState state) {
  switch (state) {
    case TargetManagerState::INIT: return "INIT";
    case TargetManagerState::CALIBRATING: return "CALIBRATING";
    case TargetManagerState::READY: return "READY";
    case TargetManagerState::FIXED_POSITION_MODE: return "FIXED_POSITION_MODE";
    case TargetManagerState::SENT_TARGET: return "SENT_TARGET";
    case TargetManagerState::WAITING_TO_RETURN: return "WAITING_TO_RETURN";
    case TargetManagerState::RETURNING: return "RETURNING";
    case TargetManagerState::FINISHED: return "FINISHED";
  }
  return "UNKNOWN";
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

TargetManagerController::TargetManagerController(RosInterface& ros)
    : SensorDataProvider("target_manager"),
      ros_(ros),
      goal_pub_(ros) {
  goal_pub_.SetTopic("/arm_position_goal");

  auto qos = rclcpp::QoS(rclcpp::KeepLast(10))
                  .reliable()
                  .durability_volatile();
  goal_pub_.SetQos(qos);

  LOGD("TargetManagerController initialized");
}

TargetManagerController::~TargetManagerController() {
  Disable();
}

// ============================================================================
// SensorDataProvider Interface
// ============================================================================

std::string TargetManagerController::PrettyName() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  std::string name = "Target Manager [";
  name += TargetManagerStateName(state_);
  name += "]";
  return name;
}

std::string TargetManagerController::GetLastMeasurementJson() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  std::ostringstream ss;
  ss << "{\"state\":\"" << TargetManagerStateName(state_) << "\""
     << ",\"arm_state\":\"" << arm_state_ << "\""
     << ",\"tilt_offset\":" << tilt_offset_
     << ",\"pan_offset\":" << pan_offset_
     << ",\"fixed_position_mode\":" << (fixed_position_mode_ ? "true" : "false")
     << "}";
  return ss.str();
}

bool TargetManagerController::GetLastMeasurement(
    jni::SensorReadingData& out_data) {
  return false;
}

// ============================================================================
// Enable / Disable
// ============================================================================

void TargetManagerController::Enable() {
  if (enabled_) {
    LOGW("TargetManagerController already enabled");
    return;
  }

  auto node = ros_.get_node();
  if (!node) {
    LOGE("ROS node not initialized");
    return;
  }

  LOGD("Enabling TargetManagerController");

  auto qos = rclcpp::QoS(rclcpp::KeepLast(10))
                  .reliable()
                  .durability_volatile();

  target_sub_ = node->create_subscription<geometry_msgs::msg::Point>(
      "/cpb_eggs_center", qos,
      std::bind(&TargetManagerController::OnTarget, this,
                std::placeholders::_1));

  imu_sub_ = node->create_subscription<sensor_msgs::msg::Imu>(
      "/zed/zed_node/imu/data", qos,
      std::bind(&TargetManagerController::OnImu, this,
                std::placeholders::_1));

  feedback_sub_ = node->create_subscription<std_msgs::msg::String>(
      "/arm_position_feedback", qos,
      std::bind(&TargetManagerController::OnFeedback, this,
                std::placeholders::_1));

  fixed_pos_sub_ = node->create_subscription<std_msgs::msg::Float32MultiArray>(
      "/pan_tilt_fixed_position", qos,
      std::bind(&TargetManagerController::OnFixedPosition, this,
                std::placeholders::_1));

  timer_ = node->create_wall_timer(
      std::chrono::milliseconds(kTimerMs),
      std::bind(&TargetManagerController::StateMachineCallback, this));

  goal_pub_.Enable();

  // Reset state
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_ = TargetManagerState::INIT;
    arm_state_ = "IDLE";
    tilt_offset_ = 0.0f;
    pan_offset_ = 0.0f;
    imu_orientation_.reset();
    last_fixed_position_ = {0.0f, 0.0f};
  }

  enabled_ = true;
  LOGI("TargetManagerController enabled");
}

void TargetManagerController::Disable() {
  if (!enabled_) {
    return;
  }

  LOGI("Disabling TargetManagerController");

  target_sub_.reset();
  imu_sub_.reset();
  feedback_sub_.reset();
  fixed_pos_sub_.reset();
  timer_.reset();

  goal_pub_.Disable();

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_ = TargetManagerState::INIT;
    arm_state_ = "IDLE";
    imu_orientation_.reset();
  }

  enabled_ = false;
  LOGI("TargetManagerController disabled");
}

// ============================================================================
// JNI-callable Controls
// ============================================================================

void TargetManagerController::SetFixedPositionMode(bool enabled) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  fixed_position_mode_ = enabled;
  LOGI("Fixed position mode %s", enabled ? "enabled" : "disabled");
}

bool TargetManagerController::IsFixedPositionMode() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return fixed_position_mode_;
}

TargetManagerState TargetManagerController::GetState() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return state_;
}

// ============================================================================
// Subscription Callbacks (executor thread - serialized)
// ============================================================================

void TargetManagerController::OnTarget(
    const geometry_msgs::msg::Point::SharedPtr msg) {
  std::lock_guard<std::mutex> lock(state_mutex_);

  if (state_ != TargetManagerState::READY || arm_state_ != "IDLE") {
    LOGD("Target ignored - state=%s, arm=%s",
         TargetManagerStateName(state_), arm_state_.c_str());
    return;
  }

  if (!std::isfinite(msg->x) || !std::isfinite(msg->y) ||
      !std::isfinite(msg->z)) {
    LOGW("Target contains NaN/Inf, ignoring");
    return;
  }

  SendTarget(*msg);
  state_ = TargetManagerState::SENT_TARGET;
}

void TargetManagerController::OnImu(
    const sensor_msgs::msg::Imu::SharedPtr msg) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  imu_orientation_ = {static_cast<float>(msg->orientation.x),
                      static_cast<float>(msg->orientation.y)};
}

void TargetManagerController::OnFeedback(
    const std_msgs::msg::String::SharedPtr msg) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  arm_state_ = msg->data;
}

void TargetManagerController::OnFixedPosition(
    const std_msgs::msg::Float32MultiArray::SharedPtr msg) {
  std::lock_guard<std::mutex> lock(state_mutex_);

  if (state_ != TargetManagerState::FIXED_POSITION_MODE ||
      arm_state_ != "IDLE") {
    return;
  }

  if (msg->data.size() != 2) {
    LOGW("Invalid fixed position message size: %zu (expected 2)",
         msg->data.size());
    return;
  }

  float pan = msg->data[0];
  float tilt = msg->data[1];

  if (last_fixed_position_[0] == pan && last_fixed_position_[1] == tilt) {
    return;  // No change
  }

  std_msgs::msg::Float32MultiArray goal;
  goal.data = {0.0f, tilt + tilt_offset_, pan + pan_offset_,
               0.0f, 0.0f, 0.0f, 0.0f};
  goal_pub_.Publish(goal);

  state_ = TargetManagerState::SENT_TARGET;
  last_fixed_position_ = {pan, tilt};
  LOGI("Fixed position: tilt=%.2f, pan=%.2f", tilt, pan);
}

// ============================================================================
// State Machine
// ============================================================================

void TargetManagerController::StateMachineCallback() {
  std::lock_guard<std::mutex> lock(state_mutex_);

  switch (state_) {
    case TargetManagerState::INIT:
      LOGI("Entering calibration state");
      state_ = TargetManagerState::CALIBRATING;
      ReturnToZero();
      break;

    case TargetManagerState::CALIBRATING:
      if (Calibrate()) {
        if (fixed_position_mode_) {
          state_ = TargetManagerState::FIXED_POSITION_MODE;
          LOGI("Entering fixed position mode");
        } else {
          state_ = TargetManagerState::READY;
          LOGI("Entering ready state");
        }
      }
      break;

    case TargetManagerState::SENT_TARGET:
      if (arm_state_ == "WAIT_AFTER_DONE") {
        state_ = TargetManagerState::WAITING_TO_RETURN;
      }
      break;

    case TargetManagerState::WAITING_TO_RETURN:
      if (arm_state_ == "IDLE") {
        if (fixed_position_mode_) {
          state_ = TargetManagerState::FIXED_POSITION_MODE;
        } else {
          ReturnToZero();
          state_ = TargetManagerState::RETURNING;
        }
      }
      break;

    case TargetManagerState::RETURNING:
      if (arm_state_ == "WAIT_AFTER_DONE") {
        state_ = TargetManagerState::FINISHED;
      }
      break;

    case TargetManagerState::FINISHED:
      if (arm_state_ == "IDLE") {
        state_ = TargetManagerState::READY;
        LOGI("Ready for next target");
      }
      break;

    case TargetManagerState::READY:
    case TargetManagerState::FIXED_POSITION_MODE:
      // Waiting for callbacks to drive transitions
      break;
  }
}

bool TargetManagerController::Calibrate() {
  if (arm_state_ != "IDLE") {
    return false;
  }

  if (!imu_orientation_.has_value()) {
    LOGD("Calibration: waiting for IMU data");
    return false;
  }

  auto [x, y] = imu_orientation_.value();

  // Check IMU values are within sane range
  if (std::abs(x) >= 1.0f || std::abs(y) >= 1.0f) {
    LOGW("IMU data out of range: x=%.3f, y=%.3f", x, y);
    return false;
  }

  // Check if level
  if (std::abs(x) < 0.01f && std::abs(y) < 0.01f) {
    LOGI("Calibration complete");
    return true;
  }

  // Adjust offsets
  tilt_offset_ -= std::floor(y * 100.0f);
  pan_offset_ -= std::floor(x * 100.0f);
  LOGI("Calibration offset - tilt: %.0f, pan: %.0f (imu x=%.3f, y=%.3f)",
       tilt_offset_, pan_offset_, x, y);

  ReturnToZero();
  return false;
}

// ============================================================================
// Targeting
// ============================================================================

void TargetManagerController::SendTarget(
    const geometry_msgs::msg::Point& msg) {
  float x = static_cast<float>(msg.x);
  float y = static_cast<float>(msg.y);
  float z = static_cast<float>(msg.z);

  auto [pan, tilt] = ComputePanTiltDegrees(x, y, z);
  ClampPanTiltAngles(pan, tilt);

  std_msgs::msg::Float32MultiArray goal;
  goal.data = {0.0f, tilt + tilt_offset_, pan + pan_offset_,
               0.0f, 0.0f, kShootingTimeMs, 0.0f};
  goal_pub_.Publish(goal);

  LOGI("Sent target: tilt=%.2f, pan=%.2f", tilt, pan);
}

void TargetManagerController::ReturnToZero() {
  std_msgs::msg::Float32MultiArray goal;
  goal.data = {0.0f, tilt_offset_, pan_offset_,
               0.0f, 0.0f, 0.0f, 0.0f};
  goal_pub_.Publish(goal);

  LOGI("Returning to (0, 0)");
}

std::pair<float, float> TargetManagerController::ComputePanTiltDegrees(
    float tx, float ty, float tz) {
  LOGI("Target point %.3f\t%.3f\t%.3f", tx, ty, tz);

  // Step 1: Transform to base frame (add camera offset)
  float bx = tx + kCameraOffsetX;
  float by = ty + kCameraOffsetY;
  float bz = tz + kCameraOffsetZ;

  // Step 2: Vector from laser to target
  float lx = bx - kLaserOffsetX;
  float ly = by - kLaserOffsetY;
  float lz = bz - kLaserOffsetZ;

  // Step 3: Pan angle (rotation around Z axis)
  float pan_rad = std::atan2(lx, lz + kEpsilon);

  // Step 4: Rotate into pan frame (inverse rotation around Z)
  float cos_pan = std::cos(-pan_rad);
  float sin_pan = std::sin(-pan_rad);
  // Rz_inv * target_laser (z unchanged by Z-rotation)
  float ry = sin_pan * lx + cos_pan * ly;
  float rz = lz;

  // Step 5: Tilt angle (rotation around X axis)
  float tilt_rad = std::atan2(-ry, rz + kEpsilon);

  return {pan_rad * kRadToDeg, tilt_rad * kRadToDeg};
}

void TargetManagerController::ClampPanTiltAngles(float& pan, float& tilt) {
  float clamped_pan = std::max(kPanMin, std::min(pan, kPanMax));
  float clamped_tilt = std::max(kTiltMin, std::min(tilt, kTiltMax));

  if (clamped_pan != pan || clamped_tilt != tilt) {
    LOGW("Target clamped to within pan/tilt arm limits");
    pan = clamped_pan;
    tilt = clamped_tilt;
  }
}

}  // namespace ros2_android
