#include "targeting/controllers/target_manager_controller.h"

#include <cmath>
#include <sstream>

#include "core/log.h"
#include "core/notification_queue.h"

namespace {

// Physical offsets (meters) - from Python target_manager.py reference
constexpr float kCameraOffsetX = 0.06f;
constexpr float kCameraOffsetY = -0.023f;
constexpr float kCameraOffsetZ = 0.035f;

constexpr float kLaserOffsetX = 0.0f;
constexpr float kLaserOffsetY = 0.025f;
constexpr float kLaserOffsetZ = 0.0f;

// Roll/tilt limits (degrees) - hardware travel limits
constexpr float kRollMin = -90.0f;
constexpr float kRollMax = 90.0f;
constexpr float kTiltMin = -30.0f;
constexpr float kTiltMax = 40.0f;

// Shooting dwell time (milliseconds)
// NOTE: laser_duration_ms is sent but currently ignored by ESP32 firmware
constexpr uint32_t kShootingTimeMs = 1000;

// State machine tick rate
constexpr int kTimerMs = 100;

// Motor enable safety delay (seconds) - mirrors Python motor_enable_delay_s
constexpr float kMotorEnableDelayS = 5.0f;

// Step conversion - mirrors Python tilt_steps_per_deg_at_res1 / roll_steps_per_deg_at_res1.
// At resolution=1, steps per degree. Actual steps = round(deg * steps_per_deg * resolution).
constexpr float kTiltStepsPerDegAtRes1 = 100.0f;
constexpr float kRollStepsPerDegAtRes1 = 100.0f;

// Star pattern physical radius (meters) - mirrors Python star_diameter_m
constexpr float kStarDiameterM = 0.005f;

// Resolution tiers (microstep resolution values) - mirrors Python r_coarse/r_mid/r_fine
constexpr uint8_t kResCoarse = 8;
constexpr uint8_t kResMid = 32;
constexpr uint8_t kResFine = 64;

// Step delta thresholds at res=1 for resolution selection - mirrors Python t_coarse/t_fine
constexpr int32_t kThresholdCoarse = 1000;
constexpr int32_t kThresholdFine = 100;

// Calibration parameters - mirrors Python calib_* params
constexpr float kCalibDeadbandDeg = 0.3f;
constexpr float kCalibGain = 0.5f;
constexpr float kCalibMaxStepDeg = 1.0f;
constexpr float kCalibCoarseDeg = 5.0f;  // error above this -> r_coarse
constexpr float kCalibFineDeg = 1.0f;    // error above this -> r_mid, else r_fine

// Default motor frequency (Hz per axis)
constexpr uint16_t kDefaultFreq = 1000;

// ESP32 Feedback state constants (mirrors vermin_collector_ros_msgs/msg/Feedback)
constexpr uint8_t kEsp32StateReady = 0;

constexpr float kRadToDeg = 180.0f / static_cast<float>(M_PI);

// ---------- Pure math helpers (no ROS, no I/O) ----------

static int32_t DegreesToSteps(float deg, float steps_per_deg_at_res1, uint8_t resolution) {
  return static_cast<int32_t>(
      std::round(deg * steps_per_deg_at_res1 * static_cast<float>(resolution)));
}

static uint8_t SelectResolution(int32_t delta_at_res1,
                                 int32_t t_coarse, int32_t t_fine,
                                 uint8_t r_coarse, uint8_t r_mid, uint8_t r_fine) {
  int32_t d = std::abs(delta_at_res1);
  if (d > t_coarse) return r_coarse;
  if (d > t_fine) return r_mid;
  return r_fine;
}

// Compute the gravity unit vector in the camera/IMU frame from a world->sensor quaternion.
// Equivalent to aim_math.gravity_in_camera(qx, qy, qz, qw).
// Returns the third row of the rotation matrix R(q): direction of world +Z in sensor frame.
static std::tuple<float, float, float> GravityInCamera(
    float qx, float qy, float qz, float qw) {
  return {
    2.0f * (qx * qz - qw * qy),
    2.0f * (qw * qx + qy * qz),
    1.0f - 2.0f * (qx * qx + qy * qy),
  };
}

}  // namespace

namespace ros2_android {

const char* TargetManagerStateName(TargetManagerState state) {
  switch (state) {
    case TargetManagerState::INIT:             return "INIT";
    case TargetManagerState::HARDHOME:         return "HARDHOME";
    case TargetManagerState::SETUP_PHASE:      return "SETUP_PHASE";
    case TargetManagerState::CALIBRATING:      return "CALIBRATING";
    case TargetManagerState::SOFTHOME:         return "SOFTHOME";
    case TargetManagerState::READY:            return "READY";
    case TargetManagerState::FIXED_POSITION_MODE: return "FIXED_POSITION_MODE";
    case TargetManagerState::SENT_CMD:         return "SENT_CMD";
    case TargetManagerState::EXECUTING:        return "EXECUTING";
  }
  return "UNKNOWN";
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

TargetManagerController::TargetManagerController(RosInterface& ros)
    : SensorDataProvider("target_manager"),
      ros_(ros),
      command_pub_(ros) {
  command_pub_.SetTopic("ESP32_Command");

  auto qos = rclcpp::QoS(rclcpp::KeepLast(10))
                  .reliable()
                  .durability_volatile();
  command_pub_.SetQos(qos);

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
     << ",\"esp32_state\":" << (latest_feedback_.has_value()
                                    ? static_cast<int>(latest_feedback_->state) : -1)
     << ",\"fixed_position_mode\":" << (fixed_position_mode_ ? "true" : "false")
     << ",\"motors_latched\":" << (motors_latched_ ? "true" : "false")
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

  feedback_sub_ = node->create_subscription<vermin_collector_ros_msgs::msg::Feedback>(
      "ESP32_Feedback", qos,
      std::bind(&TargetManagerController::OnFeedback, this,
                std::placeholders::_1));

  fixed_pos_sub_ = node->create_subscription<std_msgs::msg::Float32MultiArray>(
      "/pan_tilt_fixed_position", qos,
      std::bind(&TargetManagerController::OnFixedPosition, this,
                std::placeholders::_1));

  timer_ = node->create_wall_timer(
      std::chrono::milliseconds(kTimerMs),
      std::bind(&TargetManagerController::StateMachineCallback, this));

  command_pub_.Enable();

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_ = TargetManagerState::INIT;
    latest_feedback_.reset();
    imu_quat_.reset();
    pending_action_.reset();
    desired_setup_.frequencies = {kDefaultFreq, kDefaultFreq, kDefaultFreq};
    desired_setup_.en_motors = {0, 0, 0};  // disabled until motor gate opens
    desired_setup_.resolution = kResCoarse;
    hh_seen_non_ready_ = false;
    softhome_seen_non_ready_ = false;
    motors_latched_ = false;
    t_start_ = std::chrono::steady_clock::now();
    fixed_position_mode_ = false;
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

  command_pub_.Disable();

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_ = TargetManagerState::INIT;
    latest_feedback_.reset();
    imu_quat_.reset();
    pending_action_.reset();
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

  if (state_ != TargetManagerState::READY ||
      !latest_feedback_.has_value() ||
      latest_feedback_->state != kEsp32StateReady) {
    LOGD("Target ignored - state=%s", TargetManagerStateName(state_));
    return;
  }

  if (!std::isfinite(msg->x) || !std::isfinite(msg->y) ||
      !std::isfinite(msg->z)) {
    LOGW("Target contains NaN/Inf, ignoring");
    return;
  }

  SendTarget(*msg);
  state_ = TargetManagerState::SENT_CMD;
}

void TargetManagerController::OnImu(
    const sensor_msgs::msg::Imu::SharedPtr msg) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  // Cache full quaternion for GravityInCamera()
  imu_quat_ = std::array<float, 4>{
    static_cast<float>(msg->orientation.x),
    static_cast<float>(msg->orientation.y),
    static_cast<float>(msg->orientation.z),
    static_cast<float>(msg->orientation.w),
  };
}

void TargetManagerController::OnFeedback(
    const vermin_collector_ros_msgs::msg::Feedback::SharedPtr msg) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  LatestFeedback fb;
  fb.state = msg->state;
  fb.current_steps = msg->current_steps;
  fb.frequencies = msg->frequencies;
  fb.en_motors = msg->en_motors;
  fb.resolution = msg->resolution;
  latest_feedback_ = fb;
}

void TargetManagerController::OnFixedPosition(
    const std_msgs::msg::Float32MultiArray::SharedPtr msg) {
  std::lock_guard<std::mutex> lock(state_mutex_);

  if (state_ != TargetManagerState::FIXED_POSITION_MODE ||
      !latest_feedback_.has_value() ||
      latest_feedback_->state != kEsp32StateReady) {
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

  uint8_t res = desired_setup_.resolution;
  vermin_collector_ros_msgs::msg::Command cmd;
  cmd.command_type = vermin_collector_ros_msgs::msg::Command::TARGET;
  cmd.step_goals[0] = DegreesToSteps(tilt, kTiltStepsPerDegAtRes1, res);
  cmd.step_goals[1] = DegreesToSteps(pan, kRollStepsPerDegAtRes1, res);
  cmd.step_goals[2] = 0;
  cmd.laser_duration_ms = 0;  // no laser in fixed position mode
  cmd.star_diameter = 0;
  cmd.scan_limit = 0;
  cmd.frequency_goals = desired_setup_.frequencies;
  cmd.en_motors = desired_setup_.en_motors;
  cmd.resolution = res;
  command_pub_.Publish(cmd);

  state_ = TargetManagerState::SENT_CMD;
  last_fixed_position_ = {pan, tilt};
  LOGI("Fixed position: tilt=%.2f, pan=%.2f", tilt, pan);
}

// ============================================================================
// Motor Gate and Setup Helpers (caller holds state_mutex_)
// ============================================================================

// Returns true once kMotorEnableDelayS has elapsed since Enable().
bool TargetManagerController::MotorGateOpen() {
  if (motors_latched_) return true;
  auto elapsed = std::chrono::steady_clock::now() - t_start_;
  float seconds = std::chrono::duration<float>(elapsed).count();
  if (seconds >= kMotorEnableDelayS) {
    motors_latched_ = true;
    LOGI("Motor gate opened (%.1fs elapsed)", seconds);
    return true;
  }
  return false;
}

// Returns motor enable flags respecting the 5-second gate.
std::array<uint8_t, 3> TargetManagerController::EffectiveEnable() {
  if (MotorGateOpen()) return {1, 1, 1};
  return {0, 0, 0};
}

// Returns true when the latest Feedback echoes back the desired setup exactly.
bool TargetManagerController::SetupEchoed() const {
  if (!latest_feedback_.has_value()) return false;
  const auto& fb = *latest_feedback_;
  return fb.state == kEsp32StateReady &&
         fb.frequencies == desired_setup_.frequencies &&
         fb.en_motors == desired_setup_.en_motors &&
         fb.resolution == desired_setup_.resolution;
}

// ============================================================================
// Command Senders (caller holds state_mutex_)
// ============================================================================

void TargetManagerController::SendCurrentSetup() {
  desired_setup_.en_motors = EffectiveEnable();

  vermin_collector_ros_msgs::msg::Command cmd;
  cmd.command_type = vermin_collector_ros_msgs::msg::Command::SETUP;
  cmd.frequency_goals = desired_setup_.frequencies;
  cmd.en_motors = desired_setup_.en_motors;
  cmd.resolution = desired_setup_.resolution;
  command_pub_.Publish(cmd);

  LOGI("SETUP: freq=%d Hz, resolution=%dx, motors en=(%d,%d,%d)",
       desired_setup_.frequencies[0], desired_setup_.resolution,
       desired_setup_.en_motors[0], desired_setup_.en_motors[1],
       desired_setup_.en_motors[2]);
}

void TargetManagerController::SendHardHoming() {
  vermin_collector_ros_msgs::msg::Command cmd;
  cmd.command_type = vermin_collector_ros_msgs::msg::Command::HARD_HOMING;
  command_pub_.Publish(cmd);
  LOGI("Sent HARD_HOMING");
}

void TargetManagerController::SendSoftHoming() {
  vermin_collector_ros_msgs::msg::Command cmd;
  cmd.command_type = vermin_collector_ros_msgs::msg::Command::SOFT_HOMING;
  command_pub_.Publish(cmd);
  LOGI("Sent SOFT_HOMING");
}

// Returns arm to software zero using a regular TARGET move.
// Mirrors Python: pending_action = CMD_TARGET step_goals=(0,0,0).
void TargetManagerController::ReturnToZero() {
  vermin_collector_ros_msgs::msg::Command cmd;
  cmd.command_type = vermin_collector_ros_msgs::msg::Command::TARGET;
  cmd.step_goals[0] = 0;
  cmd.step_goals[1] = 0;
  cmd.step_goals[2] = 0;
  cmd.laser_duration_ms = 0;
  cmd.star_diameter = 0;
  cmd.scan_limit = 0;
  cmd.frequency_goals = desired_setup_.frequencies;
  cmd.en_motors = desired_setup_.en_motors;
  cmd.resolution = desired_setup_.resolution;
  command_pub_.Publish(cmd);
  LOGI("ReturnToZero: sent TARGET (0,0,0)");
}

// ============================================================================
// Targeting
// ============================================================================

void TargetManagerController::SendTarget(
    const geometry_msgs::msg::Point& msg) {
  if (!latest_feedback_.has_value()) {
    LOGW("SendTarget: no feedback received yet, skipping");
    return;
  }
  const auto& fb = *latest_feedback_;

  float tx = static_cast<float>(msg.x);
  float ty = static_cast<float>(msg.y);
  float tz = static_cast<float>(msg.z);

  auto [roll, tilt] = ComputeRollTiltDegrees(tx, ty, tz);
  ClampRollTiltAngles(roll, tilt);

  // Adaptive resolution based on planned move distance
  int32_t tilt_at_r1 = DegreesToSteps(tilt, kTiltStepsPerDegAtRes1, 1);
  int32_t roll_at_r1 = DegreesToSteps(roll, kRollStepsPerDegAtRes1, 1);
  int32_t cur_tilt_r1 = (fb.resolution > 0) ? fb.current_steps[0] / fb.resolution : 0;
  int32_t cur_roll_r1 = (fb.resolution > 0) ? fb.current_steps[1] / fb.resolution : 0;
  int32_t delta = std::max(std::abs(tilt_at_r1 - cur_tilt_r1),
                           std::abs(roll_at_r1 - cur_roll_r1));
  uint8_t res = SelectResolution(delta, kThresholdCoarse, kThresholdFine,
                                  kResCoarse, kResMid, kResFine);

  int32_t tilt_steps = DegreesToSteps(tilt, kTiltStepsPerDegAtRes1, res);
  int32_t roll_steps = DegreesToSteps(roll, kRollStepsPerDegAtRes1, res);

  // Star diameter: angular size of the physical target at the target distance
  float distance = std::sqrt(tx * tx + ty * ty + tz * tz);
  uint32_t star = 0;
  if (distance > 0.0f) {
    float angular_radius_rad = std::atan(kStarDiameterM / distance);
    star = static_cast<uint32_t>(std::round(
        angular_radius_rad * kRadToDeg * kTiltStepsPerDegAtRes1 * res));
  }

  vermin_collector_ros_msgs::msg::Command cmd;
  cmd.command_type = vermin_collector_ros_msgs::msg::Command::TARGET;
  cmd.step_goals[0] = tilt_steps;
  cmd.step_goals[1] = roll_steps;
  cmd.step_goals[2] = 0;
  cmd.laser_duration_ms = kShootingTimeMs;
  cmd.star_diameter = star;
  cmd.scan_limit = 0;
  cmd.frequency_goals = desired_setup_.frequencies;
  cmd.en_motors = desired_setup_.en_motors;
  cmd.resolution = res;
  command_pub_.Publish(cmd);

  LOGI("Sent TARGET: tilt=%.2f° roll=%.2f° res=%d steps=(%d,%d) star=%u",
       tilt, roll, res, tilt_steps, roll_steps, star);
}

// ============================================================================
// Math Utilities
// ============================================================================

// Compute the gravity unit vector in camera/IMU frame.
// Mirrors aim_math.gravity_in_camera(qx, qy, qz, qw).
std::tuple<float, float, float> TargetManagerController::GravityInCamera(
    float qx, float qy, float qz, float qw) {
  return {
    2.0f * (qx * qz - qw * qy),
    2.0f * (qw * qx + qy * qz),
    1.0f - 2.0f * (qx * qx + qy * qy),
  };
}

// Compute roll (pan) and tilt angles in degrees to aim the laser at a target.
// Mirrors aim_math.compute_roll_tilt_degrees() exactly.
// Camera frame: +X right, +Y up, +Z forward (laser default direction).
// Roll = rotation around Z (forward axis). Tilt = elevation angle.
// Picks solution with |roll| <= 90° so tilt handles above/below without
// forcing the roll motor past its limits.
std::pair<float, float> TargetManagerController::ComputeRollTiltDegrees(
    float tx, float ty, float tz) {
  LOGI("Target point %.3f\t%.3f\t%.3f", tx, ty, tz);

  // Combined offset: (camera_offset - laser_offset) applied to target
  float x = tx + kCameraOffsetX - kLaserOffsetX;
  float y = ty + kCameraOffsetY - kLaserOffsetY;
  float z = tz + kCameraOffsetZ - kLaserOffsetZ;

  // Degenerate case: target collinear with laser optical axis
  if (x * x + y * y < 1e-9f) {
    return {0.0f, 0.0f};
  }

  float roll_deg = std::atan2(x, -y) * kRadToDeg;
  float tilt_deg = std::atan2(std::sqrt(x * x + y * y), z) * kRadToDeg;

  // Pick the solution with |roll| <= 90
  if (roll_deg > 90.0f) {
    roll_deg -= 180.0f;
    tilt_deg = -tilt_deg;
  } else if (roll_deg < -90.0f) {
    roll_deg += 180.0f;
    tilt_deg = -tilt_deg;
  }

  return {roll_deg, tilt_deg};
}

void TargetManagerController::ClampRollTiltAngles(float& roll, float& tilt) {
  float cr = std::max(kRollMin, std::min(roll, kRollMax));
  float ct = std::max(kTiltMin, std::min(tilt, kTiltMax));
  if (cr != roll || ct != tilt) {
    LOGW("Target clamped to within roll/tilt arm limits");
    roll = cr;
    tilt = ct;
  }
}

int32_t TargetManagerController::DegreesToSteps(float deg,
                                                  float steps_per_deg_at_res1,
                                                  uint8_t resolution) {
  return static_cast<int32_t>(
      std::round(deg * steps_per_deg_at_res1 * static_cast<float>(resolution)));
}

uint8_t TargetManagerController::SelectResolution(int32_t delta_at_res1,
                                                    int32_t t_coarse, int32_t t_fine,
                                                    uint8_t r_coarse, uint8_t r_mid,
                                                    uint8_t r_fine) {
  int32_t d = std::abs(delta_at_res1);
  if (d > t_coarse) return r_coarse;
  if (d > t_fine) return r_mid;
  return r_fine;
}

// ============================================================================
// State Machine
// ============================================================================

void TargetManagerController::StateMachineCallback() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  switch (state_) {
    case TargetManagerState::INIT:          TickInit(); break;
    case TargetManagerState::HARDHOME:      TickHardhome(); break;
    case TargetManagerState::SETUP_PHASE:   TickSetupPhase(); break;
    case TargetManagerState::CALIBRATING:   TickCalibrating(); break;
    case TargetManagerState::SOFTHOME:      TickSofthome(); break;
    case TargetManagerState::READY:
    case TargetManagerState::FIXED_POSITION_MODE:
      TickReady(); break;
    case TargetManagerState::SENT_CMD:
    case TargetManagerState::EXECUTING:     TickExecuting(); break;
  }
}

// INIT: wait for first FB_READY, then send HARD_HOMING and enter HARDHOME.
void TargetManagerController::TickInit() {
  if (latest_feedback_.has_value() && latest_feedback_->state == kEsp32StateReady) {
    state_ = TargetManagerState::HARDHOME;
    SendHardHoming();
    LOGI("INIT -> HARDHOME");
  }
}

// HARDHOME: wait for firmware to leave READY then return to READY.
// This prevents a race condition where HARD_HOMING is still queued when feedback arrives.
void TargetManagerController::TickHardhome() {
  if (!latest_feedback_.has_value()) return;
  if (!hh_seen_non_ready_) {
    if (latest_feedback_->state != kEsp32StateReady) {
      hh_seen_non_ready_ = true;
    }
    return;
  }
  if (latest_feedback_->state == kEsp32StateReady) {
    state_ = TargetManagerState::SETUP_PHASE;
    SendCurrentSetup();
    LOGI("HARDHOME -> SETUP_PHASE");
  }
}

// SETUP_PHASE: wait for firmware to echo back the desired setup configuration.
// Re-issues SETUP once the motor gate opens (5s after Enable).
void TargetManagerController::TickSetupPhase() {
  // If motor gate just opened and motors are still disabled, re-issue SETUP
  if (MotorGateOpen() &&
      desired_setup_.en_motors != std::array<uint8_t, 3>{1, 1, 1}) {
    SendCurrentSetup();
    return;
  }
  if (SetupEchoed()) {
    state_ = TargetManagerState::CALIBRATING;
    LOGI("SETUP_PHASE -> CALIBRATING");
  }
}

// CALIBRATING: IMU-based leveling using gravity_in_camera().
// Mirrors Python target_logic._tick_calib() exactly:
// - Adaptive resolution (coarse/mid/fine)
// - Proportional gain 0.5, capped at 1.0 degree
// - Deadband 0.3 degrees; must be at r_fine to complete
// - Uses current_steps + delta (not return-to-zero on each iteration)
void TargetManagerController::TickCalibrating() {
  if (!latest_feedback_.has_value() ||
      latest_feedback_->state != kEsp32StateReady ||
      !imu_quat_.has_value()) {
    return;
  }

  const auto& fb = *latest_feedback_;
  const auto& q = *imu_quat_;

  auto [gx, gy, gz] = GravityInCamera(q[0], q[1], q[2], q[3]);
  float tilt_err = std::atan2(gz, -gy) * kRadToDeg;
  float roll_err = std::atan2(gx, -gy) * kRadToDeg;
  float err = std::max(std::abs(tilt_err), std::abs(roll_err));

  // Complete when within deadband AND at fine resolution
  if (err < kCalibDeadbandDeg && fb.resolution == kResFine) {
    state_ = TargetManagerState::SOFTHOME;
    softhome_seen_non_ready_ = false;
    SendSoftHoming();
    LOGI("CALIBRATING complete (err=%.3f°) -> SOFTHOME", err);
    return;
  }

  // Select target resolution based on error magnitude
  uint8_t target_res;
  if (err > kCalibCoarseDeg) {
    target_res = kResCoarse;
  } else if (err > kCalibFineDeg) {
    target_res = kResMid;
  } else {
    target_res = kResFine;
  }

  // If resolution needs to change, send SETUP first and wait for echo-back
  if (target_res != fb.resolution) {
    desired_setup_.resolution = target_res;
    SendCurrentSetup();
    LOGD("Calibration: resolution %d -> %d (err=%.2f°)",
         fb.resolution, target_res, err);
    return;
  }

  // Proportional correction: compute step delta from current position.
  // Sign convention follows Python legacy: subtract the delta.
  float tilt_delta_deg = std::max(-kCalibMaxStepDeg,
                                   std::min(kCalibMaxStepDeg, kCalibGain * tilt_err));
  float roll_delta_deg = std::max(-kCalibMaxStepDeg,
                                   std::min(kCalibMaxStepDeg, kCalibGain * roll_err));

  int32_t tilt_steps_delta = -DegreesToSteps(tilt_delta_deg, kTiltStepsPerDegAtRes1,
                                              fb.resolution);
  int32_t roll_steps_delta = -DegreesToSteps(roll_delta_deg, kRollStepsPerDegAtRes1,
                                              fb.resolution);

  vermin_collector_ros_msgs::msg::Command cmd;
  cmd.command_type = vermin_collector_ros_msgs::msg::Command::TARGET;
  cmd.step_goals[0] = fb.current_steps[0] + tilt_steps_delta;
  cmd.step_goals[1] = fb.current_steps[1] + roll_steps_delta;
  cmd.step_goals[2] = fb.current_steps[2];
  cmd.frequency_goals = desired_setup_.frequencies;
  cmd.en_motors = desired_setup_.en_motors;
  cmd.resolution = fb.resolution;
  command_pub_.Publish(cmd);

  LOGI("Calibration: err=%.3f° tilt_err=%.3f° roll_err=%.3f° delta=(%d,%d) res=%d",
       err, tilt_err, roll_err, tilt_steps_delta, roll_steps_delta, fb.resolution);
}

// SOFTHOME: wait for firmware to leave READY then return after soft home completes.
void TargetManagerController::TickSofthome() {
  if (!latest_feedback_.has_value()) return;
  if (!softhome_seen_non_ready_) {
    if (latest_feedback_->state != kEsp32StateReady) {
      softhome_seen_non_ready_ = true;
    }
    return;
  }
  if (latest_feedback_->state == kEsp32StateReady) {
    if (fixed_position_mode_) {
      state_ = TargetManagerState::FIXED_POSITION_MODE;
      LOGI("SOFTHOME -> FIXED_POSITION_MODE");
    } else {
      state_ = TargetManagerState::READY;
      LOGI("SOFTHOME -> READY");
    }
  }
}

// READY / FIXED_POSITION_MODE: idle, driven by callbacks.
// Handles motor gate re-enable, drift detection, and pending action dispatch.
void TargetManagerController::TickReady() {
  if (!latest_feedback_.has_value() ||
      latest_feedback_->state != kEsp32StateReady) {
    return;
  }
  const auto& fb = *latest_feedback_;

  // Motor gate just opened: re-send SETUP to enable motors.
  // Stays in READY - SETUP doesn't produce a MOVING response.
  if (MotorGateOpen() &&
      desired_setup_.en_motors != std::array<uint8_t, 3>{1, 1, 1}) {
    SendCurrentSetup();
    return;
  }

  // Drift detection: firmware configuration drifted from desired.
  // Silent re-issue - stays in READY, no state change.
  if (fb.frequencies != desired_setup_.frequencies ||
      fb.en_motors != desired_setup_.en_motors ||
      fb.resolution != desired_setup_.resolution) {
    LOGW("Firmware setup drift detected, re-issuing SETUP");
    SendCurrentSetup();
    return;
  }

  // Pending queued action (e.g. post-fire return-to-zero)
  if (pending_action_.has_value()) {
    command_pub_.Publish(*pending_action_);
    pending_action_.reset();
    state_ = TargetManagerState::SENT_CMD;
    return;
  }

  // OnTarget() / OnFixedPosition() drive transitions from here
}

// SENT_CMD / EXECUTING: wait for firmware to acknowledge and complete.
// On completion, queues return-to-zero as pending_action for TickReady.
void TargetManagerController::TickExecuting() {
  if (!latest_feedback_.has_value()) return;
  const auto& fb = *latest_feedback_;

  if (state_ == TargetManagerState::SENT_CMD) {
    // Waiting for firmware to leave READY (acknowledges command receipt)
    if (fb.state != kEsp32StateReady) {
      state_ = TargetManagerState::EXECUTING;
    }
    return;
  }

  // EXECUTING: waiting for firmware to return to READY
  if (fb.state != kEsp32StateReady) return;

  // Command finished.
  // In TARGET mode, queue return-to-zero as pending_action (sent on next TickReady).
  // Mirrors Python: pending_action = CMD_TARGET step_goals=(0,0,0)
  if (!fixed_position_mode_) {
    vermin_collector_ros_msgs::msg::Command ret;
    ret.command_type = vermin_collector_ros_msgs::msg::Command::TARGET;
    ret.step_goals[0] = 0;
    ret.step_goals[1] = 0;
    ret.step_goals[2] = 0;
    ret.laser_duration_ms = 0;
    ret.star_diameter = 0;
    ret.scan_limit = 0;
    ret.frequency_goals = desired_setup_.frequencies;
    ret.en_motors = desired_setup_.en_motors;
    ret.resolution = desired_setup_.resolution;
    pending_action_ = ret;
    state_ = TargetManagerState::READY;
    LOGI("EXECUTING -> READY (return-to-zero pending)");
  } else {
    state_ = TargetManagerState::FIXED_POSITION_MODE;
    LOGI("EXECUTING -> FIXED_POSITION_MODE");
  }
}

}  // namespace ros2_android
