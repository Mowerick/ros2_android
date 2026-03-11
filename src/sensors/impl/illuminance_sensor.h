#pragma once

#include <sensor_msgs/msg/illuminance.hpp>

#include "core/events.h"
#include "sensors/base/sensor.h"

namespace ros2_android {
class IlluminanceSensor : public Sensor,
                          public event::Emitter<sensor_msgs::msg::Illuminance> {
 public:
  using Sensor::Sensor;
  virtual ~IlluminanceSensor() = default;

 protected:
  void OnEvent(const ASensorEvent& event) override;
};
}  // namespace ros2_android
