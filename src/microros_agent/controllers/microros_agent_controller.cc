#include "microros_agent/controllers/microros_agent_controller.h"

#include <sstream>

#include <uxr/agent/transport/custom/CustomAgent.hpp>

#include "core/log.h"
#include "core/notification_queue.h"
#include "microros_agent/jni_serial_transport.h"

namespace ros2_android {

const char* AgentStateName(AgentState state) {
  switch (state) {
    case AgentState::STOPPED: return "STOPPED";
    case AgentState::STARTING: return "STARTING";
    case AgentState::RUNNING: return "RUNNING";
    case AgentState::STOPPING: return "STOPPING";
    case AgentState::ERROR: return "ERROR";
  }
  return "UNKNOWN";
}

MicroRosAgentController::MicroRosAgentController(const std::string& device_id,
                                                 int baudrate)
    : SensorDataProvider("microros_agent"),
      device_id_(device_id),
      baudrate_(baudrate) {}

MicroRosAgentController::~MicroRosAgentController() {
  Disable();
}

std::string MicroRosAgentController::PrettyName() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return std::string("micro-ROS Agent [") + AgentStateName(state_) + "]";
}

std::string MicroRosAgentController::GetLastMeasurementJson() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  std::ostringstream ss;
  ss << "{\"state\":\"" << AgentStateName(state_) << "\""
     << ",\"device\":\"" << device_id_ << "\""
     << ",\"baudrate\":" << baudrate_
     << "}";
  return ss.str();
}

bool MicroRosAgentController::GetLastMeasurement(
    jni::SensorReadingData& /*out_data*/) {
  return false;
}

AgentState MicroRosAgentController::GetState() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return state_;
}

void MicroRosAgentController::Enable() {
  if (enabled_) {
    LOGW("MicroRosAgentController already enabled");
    return;
  }

  LOGI("Enabling MicroRosAgentController for %s at %d baud",
       device_id_.c_str(), baudrate_);

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_ = AgentState::STARTING;
  }

  stop_requested_ = false;
  agent_thread_ = std::thread(&MicroRosAgentController::AgentThreadFunc, this);
  enabled_ = true;
}

void MicroRosAgentController::Disable() {
  if (!enabled_) {
    return;
  }

  LOGI("Disabling MicroRosAgentController");

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_ = AgentState::STOPPING;
  }

  stop_requested_ = true;

  // Stop the agent (unblocks recv in agent thread)
  if (agent_) {
    agent_->stop();
  }

  if (agent_thread_.joinable()) {
    agent_thread_.join();
  }

  // Clean up in order
  agent_.reset();
  endpoint_.reset();
  if (transport_) {
    transport_->Fini();
    transport_.reset();
  }

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_ = AgentState::STOPPED;
  }

  enabled_ = false;
  LOGI("MicroRosAgentController disabled");
}

void MicroRosAgentController::AgentThreadFunc() {
  // Initialize transport
  transport_ = std::make_unique<JniSerialTransport>(device_id_, baudrate_);
  if (!transport_->Init()) {
    LOGE("Failed to initialize serial transport for %s", device_id_.c_str());
    PostNotification(NotificationSeverity::ERROR,
                     "micro-ROS Agent: failed to open serial port. "
                     "Check USB connection.");
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_ = AgentState::ERROR;
    return;
  }

  // Create endpoint (serial transport is point-to-point, single endpoint)
  endpoint_ = std::make_unique<eprosima::uxr::CustomEndPoint>();
  endpoint_->add_member<uint32_t>("index");

  // Create CustomAgent callback functions
  // These are stored as std::function references - they must outlive the agent
  eprosima::uxr::CustomAgent::InitFunction init_func =
      [this]() -> bool { return true; };  // Already initialized above

  eprosima::uxr::CustomAgent::FiniFunction fini_func =
      [this]() -> bool { return true; };  // Cleaned up in Disable()

  eprosima::uxr::CustomAgent::SendMsgFunction send_func =
      [this](const eprosima::uxr::CustomEndPoint* dest, uint8_t* buf,
             size_t len, eprosima::uxr::TransportRc& rc) -> ssize_t {
        return transport_->SendMsg(dest, buf, len, rc);
      };

  eprosima::uxr::CustomAgent::RecvMsgFunction recv_func =
      [this](eprosima::uxr::CustomEndPoint* src, uint8_t* buf, size_t len,
             int timeout, eprosima::uxr::TransportRc& rc) -> ssize_t {
        return transport_->RecvMsg(src, buf, len, timeout, rc);
      };

  // Create the agent with framing enabled (serial transport uses HDLC framing)
  agent_ = std::make_unique<eprosima::uxr::CustomAgent>(
      "microros_serial_agent", endpoint_.get(),
      eprosima::uxr::Middleware::Kind::FASTDDS,
      true,  // framing = true for serial
      init_func, fini_func, send_func, recv_func);

  if (!agent_->start()) {
    LOGE("Failed to start micro-ROS Agent");
    PostNotification(NotificationSeverity::ERROR,
                     "micro-ROS Agent: failed to start. Check logs.");
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_ = AgentState::ERROR;
    return;
  }

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_ = AgentState::RUNNING;
  }

  LOGI("micro-ROS Agent running on %s", device_id_.c_str());

  // Block until stop is requested
  // The agent runs its own internal threads for processing
  while (!stop_requested_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  LOGI("micro-ROS Agent thread exiting");
}

}  // namespace ros2_android
