#include "sensors/impl/illuminance_sensor.h"

#include "core/log.h"

using ros2_android::IlluminanceSensor;

void IlluminanceSensor::OnEvent(const ASensorEvent& event) {
  if (ASENSOR_TYPE_LIGHT != event.type) {
    LOGW("Event type was unexpected: %d", event.type);
    return;
  }
  sensor_msgs::msg::Illuminance msg;
  msg.illuminance = event.light;
  Emit(msg);
}
