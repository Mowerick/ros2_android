#pragma once

#include <geometry_msgs/msg/twist_stamped.hpp>

#include "core/events.h"
#include "sensors/base/sensor.h"

namespace ros2_android {
class GyroscopeSensor
    : public Sensor,
      public event::Emitter<geometry_msgs::msg::TwistStamped> {
 public:
  using Sensor::Sensor;
  virtual ~GyroscopeSensor() = default;

 protected:
  void OnEvent(const ASensorEvent& event) override;
};
}  // namespace ros2_android
