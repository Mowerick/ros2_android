#include "sensors/sensor_manager.h"

#include "core/log.h"
#include "sensors/controllers/accelerometer_sensor_controller.h"
#include "sensors/controllers/barometer_sensor_controller.h"
#include "sensors/controllers/gyroscope_sensor_controller.h"
#include "sensors/controllers/illuminance_sensor_controller.h"
#include "sensors/controllers/magnetometer_sensor_controller.h"

using ros2_android::SensorManager;

SensorManager::SensorManager(const std::string& package_name)
    : package_name_(package_name), sensors_(package_name) {}

SensorManager::~SensorManager() {
  LOGI("SensorManager destructor");
  ClearControllers();
  Shutdown();
}

void SensorManager::Initialize() {
  LOGI("SensorManager: Initializing sensors");
  sensors_.Initialize();
}

void SensorManager::Shutdown() {
  LOGI("SensorManager: Shutting down sensors");
  sensors_.Shutdown();
}

size_t SensorManager::CreateControllers(RosInterface& ros) {
  LOGI("SensorManager: Creating sensor controllers");

  // Clear any existing controllers first
  ClearControllers();

  // Iterate over all discovered sensors and create appropriate controllers
  for (auto& sensor : sensors_.GetSensors()) {
    const auto& desc = sensor->Descriptor();

    switch (desc.type) {
      case ASENSOR_TYPE_LIGHT: {
        auto controller = std::make_unique<IlluminanceSensorController>(
            static_cast<IlluminanceSensor*>(sensor.get()), ros);
        controllers_.emplace_back(std::move(controller));
        break;
      }
      case ASENSOR_TYPE_GYROSCOPE: {
        auto controller = std::make_unique<GyroscopeSensorController>(
            static_cast<GyroscopeSensor*>(sensor.get()), ros);
        controllers_.emplace_back(std::move(controller));
        break;
      }
      case ASENSOR_TYPE_ACCELEROMETER: {
        auto controller = std::make_unique<AccelerometerSensorController>(
            static_cast<AccelerometerSensor*>(sensor.get()), ros);
        controllers_.emplace_back(std::move(controller));
        break;
      }
      case ASENSOR_TYPE_PRESSURE: {
        auto controller = std::make_unique<BarometerSensorController>(
            static_cast<BarometerSensor*>(sensor.get()), ros);
        controllers_.emplace_back(std::move(controller));
        break;
      }
      case ASENSOR_TYPE_MAGNETIC_FIELD: {
        auto controller = std::make_unique<MagnetometerSensorController>(
            static_cast<MagnetometerSensor*>(sensor.get()), ros);
        controllers_.emplace_back(std::move(controller));
        break;
      }
      default:
        LOGW("SensorManager: Unsupported sensor type %d", desc.type);
        break;
    }
  }

  LOGI("SensorManager: Created %zu sensor controllers", controllers_.size());
  return controllers_.size();
}

const std::vector<std::unique_ptr<ros2_android::SensorDataProvider>>&
SensorManager::GetControllers() const {
  return controllers_;
}

bool SensorManager::EnableSensor(const std::string& unique_id) {
  for (auto& controller : controllers_) {
    if (controller->UniqueId() == unique_id) {
      controller->Enable();
      LOGI("SensorManager: Enabled sensor %s", unique_id.c_str());
      return true;
    }
  }
  LOGW("SensorManager: Sensor %s not found", unique_id.c_str());
  return false;
}

bool SensorManager::DisableSensor(const std::string& unique_id) {
  for (auto& controller : controllers_) {
    if (controller->UniqueId() == unique_id) {
      controller->Disable();
      LOGI("SensorManager: Disabled sensor %s", unique_id.c_str());
      return true;
    }
  }
  LOGW("SensorManager: Sensor %s not found", unique_id.c_str());
  return false;
}

void SensorManager::ClearControllers() {
  LOGI("SensorManager: Clearing %zu controllers", controllers_.size());

  // Disable all controllers before clearing
  for (auto& controller : controllers_) {
    if (controller->IsEnabled()) {
      controller->Disable();
    }
  }

  controllers_.clear();
}
