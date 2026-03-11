#pragma once

#include <android/sensor.h>

#include <string>
#include <vector>

#include "core/events.h"
#include "ros/ros_interface.h"
#include "sensors/base/sensor.h"
#include "sensors/base/sensor_descriptor.h"

namespace ros2_android {
class Sensors {
 public:
  Sensors(const std::string& package_name);
  ~Sensors() = default;

  void Initialize();
  void Shutdown();

  const std::vector<std::unique_ptr<Sensor>>& GetSensors() { return sensors_; };

 private:
  std::vector<SensorDescriptor> QuerySensors();

  void EventLoop();

  ASensorManager* sensor_manager_ = nullptr;

  std::vector<std::unique_ptr<Sensor>> sensors_;
};
}  // namespace ros2_android
