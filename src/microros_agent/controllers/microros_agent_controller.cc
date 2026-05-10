#include "microros_agent/controllers/microros_agent_controller.h"

#include <sstream>

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

  // Clean up in order. Function members must be cleared after agent_ so that
  // fini() can still invoke them during stop() above.
  agent_.reset();
  agent_init_func_ = nullptr;
  agent_fini_func_ = nullptr;
  agent_send_func_ = nullptr;
  agent_recv_func_ = nullptr;
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

  // Assign CustomAgent callback functions to members. CustomAgent stores these
  // as references (not copies), so they must outlive agent_->stop() in
  // Disable(). Using locals here would cause a dangling reference crash when
  // stop() calls fini() after AgentThreadFunc() returns.
  agent_init_func_ = [this]() -> bool { return true; };
  agent_fini_func_ = [this]() -> bool { return true; };
  agent_send_func_ = [this](const eprosima::uxr::CustomEndPoint* dest,
                             uint8_t* buf, size_t len,
                             eprosima::uxr::TransportRc& rc) -> ssize_t {
    return transport_->SendMsg(dest, buf, len, rc);
  };
  agent_recv_func_ = [this](eprosima::uxr::CustomEndPoint* src, uint8_t* buf,
                             size_t len, int timeout,
                             eprosima::uxr::TransportRc& rc) -> ssize_t {
    return transport_->RecvMsg(src, buf, len, timeout, rc);
  };

  // Create the agent with framing enabled (serial transport uses HDLC framing)
  agent_ = std::make_unique<eprosima::uxr::CustomAgent>(
      "microros_serial_agent", endpoint_.get(),
      eprosima::uxr::Middleware::Kind::FASTDDS,
      true,  // framing = true for serial
      agent_init_func_, agent_fini_func_, agent_send_func_, agent_recv_func_);

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
