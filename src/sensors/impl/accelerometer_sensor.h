#pragma once

#include <geometry_msgs/msg/accel_stamped.hpp>

#include "core/events.h"
#include "sensors/base/sensor.h"

namespace ros2_android {
class AccelerometerSensor
    : public Sensor,
      public event::Emitter<geometry_msgs::msg::AccelStamped> {
 public:
  using Sensor::Sensor;
  virtual ~AccelerometerSensor() = default;

 protected:
  void OnEvent(const ASensorEvent& event) override;
};
}  // namespace ros2_android
