#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <uxr/agent/transport/custom/CustomAgent.hpp>

#include "sensors/base/sensor_data_provider.h"

namespace ros2_android {

class JniSerialTransport;

enum class AgentState {
  STOPPED,
  STARTING,
  RUNNING,
  STOPPING,
  ERROR
};

const char* AgentStateName(AgentState state);

class MicroRosAgentController : public SensorDataProvider {
 public:
  MicroRosAgentController(const std::string& device_id, int baudrate);
  ~MicroRosAgentController() override;

  // SensorDataProvider interface
  std::string PrettyName() const override;
  std::string GetLastMeasurementJson() override;
  bool GetLastMeasurement(jni::SensorReadingData& out_data) override;

  void Enable() override;
  void Disable() override;
  bool IsEnabled() const override { return enabled_; }

  AgentState GetState() const;

 private:
  void AgentThreadFunc();

  std::string device_id_;
  int baudrate_;
  std::atomic<bool> enabled_{false};

  mutable std::mutex state_mutex_;
  AgentState state_ = AgentState::STOPPED;

  std::unique_ptr<JniSerialTransport> transport_;
  std::unique_ptr<eprosima::uxr::CustomEndPoint> endpoint_;
  std::unique_ptr<eprosima::uxr::CustomAgent> agent_;
  std::thread agent_thread_;
  std::atomic<bool> stop_requested_{false};

  // Stored as members so they outlive AgentThreadFunc() - CustomAgent holds
  // references (not copies) to these, so they must remain valid until after
  // agent_->stop() returns in Disable().
  eprosima::uxr::CustomAgent::InitFunction agent_init_func_;
  eprosima::uxr::CustomAgent::FiniFunction agent_fini_func_;
  eprosima::uxr::CustomAgent::SendMsgFunction agent_send_func_;
  eprosima::uxr::CustomAgent::RecvMsgFunction agent_recv_func_;
};

}  // namespace ros2_android
