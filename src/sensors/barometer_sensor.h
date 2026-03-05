#pragma once

#include <sensor_msgs/msg/fluid_pressure.hpp>

#include "events.h"
#include "sensor.h"

namespace ros2_android {
class BarometerSensor : public Sensor,
                        public event::Emitter<sensor_msgs::msg::FluidPressure> {
 public:
  using Sensor::Sensor;
  virtual ~BarometerSensor() = default;

 protected:
  void OnEvent(const ASensorEvent& event) override;
};
}  // namespace ros2_android
