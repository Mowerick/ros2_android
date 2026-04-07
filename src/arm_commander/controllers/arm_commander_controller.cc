#include "arm_commander/controllers/arm_commander_controller.h"

#include <cmath>
#include <sstream>

#include "core/log.h"
#include "core/notification_queue.h"

namespace {

// State machine tick rate (10 Hz)
constexpr int kTimerMs = 100;

// Timeout constants - from upstream arm_commander.py parameters
constexpr int kAckTimeoutMs = 2000;
constexpr int kDoneTimeoutMs = 10000;
constexpr int kPostDoneDelayMs = 1200;
constexpr int kPostNackDelayMs = 1000;
constexpr int kMaxNackCount = 15;

}  // namespace

namespace ros2_android {

const char* ArmCommanderStateName(ArmCommanderState state) {
  switch (state) {
    case ArmCommanderState::IDLE: return "IDLE";
    case ArmCommanderState::AWAITING_ACK: return "AWAITING_ACK";
    case ArmCommanderState::AWAITING_DONE: return "AWAITING_DONE";
    case ArmCommanderState::WAIT_AFTER_DONE: return "WAIT_AFTER_DONE";
    case ArmCommanderState::WAIT_AFTER_NACK: return "WAIT_AFTER_NACK";
    case ArmCommanderState::NACK_LIMIT_EXCEEDED: return "NACK_LIMIT_EXCEEDED";
  }
  return "UNKNOWN";
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

ArmCommanderController::ArmCommanderController(RosInterface& ros)
    : SensorDataProvider("arm_commander"),
      ros_(ros),
      command_pub_(ros),
      feedback_pub_(ros) {
  auto qos = rclcpp::QoS(rclcpp::KeepLast(10))
                  .reliable()
                  .durability_volatile();

  command_pub_.SetTopic("/PointNShoot");
  command_pub_.SetQos(qos);

  feedback_pub_.SetTopic("/arm_position_feedback");
  feedback_pub_.SetQos(qos);

  LOGD("ArmCommanderController initialized");
}

ArmCommanderController::~ArmCommanderController() {
  Disable();
}

// ============================================================================
// SensorDataProvider Interface
// ============================================================================

std::string ArmCommanderController::PrettyName() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  std::string name = "Arm Commander [";
  name += ArmCommanderStateName(state_);
  name += "]";
  return name;
}

std::string ArmCommanderController::GetLastMeasurementJson() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  std::ostringstream ss;
  ss << "{\"state\":\"" << ArmCommanderStateName(state_) << "\""
     << ",\"msg_id\":" << msg_id_
     << ",\"nack_count\":" << nack_count_
     << "}";
  return ss.str();
}

bool ArmCommanderController::GetLastMeasurement(
    jni::SensorReadingData& out_data) {
  return false;
}

// ============================================================================
// Enable / Disable
// ============================================================================

void ArmCommanderController::Enable() {
  if (enabled_) {
    LOGW("ArmCommanderController already enabled");
    return;
  }

  auto node = ros_.get_node();
  if (!node) {
    LOGE("ROS node not initialized");
    return;
  }

  LOGD("Enabling ArmCommanderController");

  // Goal subscription - reliable QoS (matches target_manager publisher)
  auto reliable_qos = rclcpp::QoS(rclcpp::KeepLast(10))
                          .reliable()
                          .durability_volatile();

  goal_sub_ = node->create_subscription<std_msgs::msg::Float32MultiArray>(
      "/arm_position_goal", reliable_qos,
      std::bind(&ArmCommanderController::OnGoal, this,
                std::placeholders::_1));

  // ACK/DONE/NACK subscriptions - best effort QoS (matches micro-ROS)
  auto best_effort_qos = rclcpp::QoS(rclcpp::KeepLast(10))
                             .best_effort()
                             .durability_volatile();

  ack_sub_ = node->create_subscription<std_msgs::msg::Float32>(
      "/PointNShoot_ACK", best_effort_qos,
      std::bind(&ArmCommanderController::OnAck, this,
                std::placeholders::_1));

  done_sub_ = node->create_subscription<std_msgs::msg::Float32>(
      "/PointNShoot_DONE", best_effort_qos,
      std::bind(&ArmCommanderController::OnDone, this,
                std::placeholders::_1));

  nack_sub_ = node->create_subscription<std_msgs::msg::Float32>(
      "/PointNShoot_NACK", best_effort_qos,
      std::bind(&ArmCommanderController::OnNack, this,
                std::placeholders::_1));

  timer_ = node->create_wall_timer(
      std::chrono::milliseconds(kTimerMs),
      std::bind(&ArmCommanderController::Loop, this));

  command_pub_.Enable();
  feedback_pub_.Enable();

  // Reset state
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_ = ArmCommanderState::IDLE;
    msg_id_ = 0;
    nack_count_ = 0;
    target_received_ = false;
    current_goal_ = {};
    last_sent_ = {};
    last_sent_time_.reset();
    last_ack_time_.reset();
    transition_time_.reset();
  }

  // Publish initial IDLE feedback
  TransitionTo(ArmCommanderState::IDLE);

  enabled_ = true;
  LOGI("ArmCommanderController enabled");
}

void ArmCommanderController::Disable() {
  if (!enabled_) {
    return;
  }

  LOGI("Disabling ArmCommanderController");

  goal_sub_.reset();
  ack_sub_.reset();
  done_sub_.reset();
  nack_sub_.reset();
  timer_.reset();

  command_pub_.Disable();
  feedback_pub_.Disable();

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_ = ArmCommanderState::IDLE;
    msg_id_ = 0;
    nack_count_ = 0;
    target_received_ = false;
    current_goal_ = {};
    last_sent_ = {};
    last_sent_time_.reset();
    last_ack_time_.reset();
    transition_time_.reset();
  }

  enabled_ = false;
  LOGI("ArmCommanderController disabled");
}

// ============================================================================
// JNI-callable
// ============================================================================

ArmCommanderState ArmCommanderController::GetState() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return state_;
}

// ============================================================================
// Subscription Callbacks (executor thread - serialized)
// ============================================================================

void ArmCommanderController::OnGoal(
    const std_msgs::msg::Float32MultiArray::SharedPtr msg) {
  std::lock_guard<std::mutex> lock(state_mutex_);

  if (msg->data.size() != 7) {
    LOGW("Invalid goal message size: %zu (expected 7)", msg->data.size());
    return;
  }

  // Validate no NaN/Inf in fields 1-6 (field 0 is msg_id from target_manager)
  for (size_t i = 1; i < 7; ++i) {
    if (!std::isfinite(msg->data[i])) {
      LOGW("Goal contains NaN/Inf at index %zu, ignoring", i);
      return;
    }
  }

  for (size_t i = 0; i < 7; ++i) {
    current_goal_[i] = msg->data[i];
  }

  target_received_ = true;

  LOGI("New target: tilt=%.2f, pan=%.2f, time=%.1fms",
       current_goal_[1], current_goal_[2], current_goal_[5]);

  // Force immediate send if IDLE (matches upstream behavior)
  if (state_ == ArmCommanderState::IDLE) {
    // Release lock and let Loop() handle it on next tick
    // (don't call Loop() directly to avoid recursive lock)
  }
}

void ArmCommanderController::OnAck(
    const std_msgs::msg::Float32::SharedPtr msg) {
  std::lock_guard<std::mutex> lock(state_mutex_);

  int received_id = static_cast<int>(msg->data);
  if (received_id != (msg_id_ - 1) ||
      state_ != ArmCommanderState::AWAITING_ACK) {
    return;
  }

  LOGI("ACK received for msgID %d", received_id);
  last_ack_time_ = Clock::now();
  nack_count_ = 0;
  TransitionTo(ArmCommanderState::AWAITING_DONE);
}

void ArmCommanderController::OnDone(
    const std_msgs::msg::Float32::SharedPtr msg) {
  std::lock_guard<std::mutex> lock(state_mutex_);

  int received_id = static_cast<int>(msg->data);
  if (received_id != (msg_id_ - 1) ||
      state_ != ArmCommanderState::AWAITING_DONE) {
    return;
  }

  LOGI("DONE received for msgID %d", received_id);
  transition_time_ = Clock::now();
  TransitionTo(ArmCommanderState::WAIT_AFTER_DONE);
}

void ArmCommanderController::OnNack(
    const std_msgs::msg::Float32::SharedPtr msg) {
  std::lock_guard<std::mutex> lock(state_mutex_);

  int received_id = static_cast<int>(msg->data);
  if (received_id != (msg_id_ - 1) ||
      state_ != ArmCommanderState::AWAITING_ACK) {
    return;
  }

  LOGW("NACK received for msgID %d", received_id);
  msg_id_--;  // Retry same ID
  nack_count_++;

  if (nack_count_ >= kMaxNackCount) {
    LOGE("Too many NACKs (%d). Entering error state.", nack_count_);
    TransitionTo(ArmCommanderState::NACK_LIMIT_EXCEEDED);
    PostNotification(NotificationSeverity::ERROR,
                     "Arm commander: too many NACKs from microcontroller. "
                     "Check hardware connection and restart from pipeline UI.");
    // Disable must be called without holding the lock
    // Schedule disable on next loop tick instead
    return;
  }

  transition_time_ = Clock::now();
  TransitionTo(ArmCommanderState::WAIT_AFTER_NACK);
}

// ============================================================================
// State Machine
// ============================================================================

void ArmCommanderController::Loop() {
  std::lock_guard<std::mutex> lock(state_mutex_);

  // Handle NACK_LIMIT_EXCEEDED - disable outside of callback
  if (state_ == ArmCommanderState::NACK_LIMIT_EXCEEDED) {
    // Release lock, then disable
    // Can't call Disable() while holding lock, so just return.
    // The PostNotification already fired in OnNack.
    return;
  }

  auto now = Clock::now();

  if (ShouldBlock()) {
    return;
  }

  if (!target_received_) {
    return;
  }

  target_received_ = false;

  // Duplicate detection: compare fields [1..6] (skip msg_id at [0])
  bool is_duplicate = true;
  for (size_t i = 1; i < 7; ++i) {
    if (current_goal_[i] != last_sent_[i]) {
      is_duplicate = false;
      break;
    }
  }

  // Only skip if we've actually sent something before (last_sent_time_ set)
  if (is_duplicate && last_sent_time_.has_value()) {
    LOGI("Skipping publish - target unchanged (tilt=%.2f, pan=%.2f)",
         current_goal_[1], current_goal_[2]);
    TransitionTo(ArmCommanderState::IDLE);
    return;
  }

  SendCommand();
}

bool ArmCommanderController::ShouldBlock() {
  auto now = Clock::now();

  auto elapsed_ms = [&now](const TimePoint& from) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - from)
        .count();
  };

  switch (state_) {
    case ArmCommanderState::AWAITING_ACK:
      if (last_sent_time_.has_value() &&
          elapsed_ms(*last_sent_time_) > kAckTimeoutMs) {
        LOGW("ACK timeout for msgID %d. Retrying...", msg_id_ - 1);
        ResendLast();
      }
      return true;

    case ArmCommanderState::AWAITING_DONE:
      if (last_ack_time_.has_value() &&
          elapsed_ms(*last_ack_time_) > kDoneTimeoutMs) {
        LOGW("DONE timeout for msgID %d. Retrying...", msg_id_ - 1);
        ResendLast();
      }
      return true;

    case ArmCommanderState::WAIT_AFTER_DONE:
      if (transition_time_.has_value() &&
          elapsed_ms(*transition_time_) >= kPostDoneDelayMs) {
        TransitionTo(ArmCommanderState::IDLE);
        return false;
      }
      return true;

    case ArmCommanderState::WAIT_AFTER_NACK:
      if (transition_time_.has_value() &&
          elapsed_ms(*transition_time_) >= kPostNackDelayMs) {
        TransitionTo(ArmCommanderState::IDLE);
        return false;
      }
      return true;

    case ArmCommanderState::NACK_LIMIT_EXCEEDED:
      return true;

    case ArmCommanderState::IDLE:
      return false;
  }

  return false;
}

void ArmCommanderController::SendCommand() {
  // Build message with current msg_id
  std_msgs::msg::Float32MultiArray msg;
  msg.data.resize(7);
  msg.data[0] = static_cast<float>(msg_id_);
  for (size_t i = 1; i < 7; ++i) {
    msg.data[i] = current_goal_[i];
  }

  command_pub_.Publish(msg);

  // Track what we sent
  for (size_t i = 0; i < 7; ++i) {
    last_sent_[i] = msg.data[i];
  }
  last_sent_time_ = Clock::now();
  last_ack_time_.reset();

  LOGI("Sent msgID %d: pan=%.2f, tilt=%.2f",
       msg_id_, current_goal_[2], current_goal_[1]);

  msg_id_++;
  TransitionTo(ArmCommanderState::AWAITING_ACK);
}

void ArmCommanderController::ResendLast() {
  std_msgs::msg::Float32MultiArray msg;
  msg.data.resize(7);
  msg.data[0] = static_cast<float>(msg_id_);
  for (size_t i = 1; i < 7; ++i) {
    msg.data[i] = last_sent_[i];
  }

  command_pub_.Publish(msg);

  last_sent_[0] = static_cast<float>(msg_id_);
  last_sent_time_ = Clock::now();
  last_ack_time_.reset();

  LOGW("Resent msgID %d (retry): pan=%.2f, tilt=%.2f",
       msg_id_, last_sent_[2], last_sent_[1]);

  msg_id_++;
  TransitionTo(ArmCommanderState::AWAITING_ACK);
}

void ArmCommanderController::TransitionTo(ArmCommanderState new_state) {
  state_ = new_state;

  // Publish state name as feedback for target_manager
  std_msgs::msg::String feedback;
  feedback.data = ArmCommanderStateName(new_state);
  feedback_pub_.Publish(feedback);
}

}  // namespace ros2_android
