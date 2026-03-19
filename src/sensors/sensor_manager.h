#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ros/ros_interface.h"
#include "sensors/base/sensor_data_provider.h"
#include "sensors/sensors.h"

namespace ros2_android {

/**
 * @brief Manages the lifecycle of built-in Android sensors and their ROS 2 publishers.
 *
 * This class encapsulates sensor initialization, controller creation, and lifecycle management,
 * separating these concerns from the main application logic.
 */
class SensorManager {
 public:
  /**
   * @brief Construct a new SensorManager.
   * @param package_name The Android package name for sensor identification
   */
  explicit SensorManager(const std::string& package_name);

  ~SensorManager();

  // No copies or moves - manages unique resources
  SensorManager(const SensorManager&) = delete;
  SensorManager& operator=(const SensorManager&) = delete;
  SensorManager(SensorManager&&) = delete;
  SensorManager& operator=(SensorManager&&) = delete;

  /**
   * @brief Initialize sensor hardware and start sensor event loops.
   */
  void Initialize();

  /**
   * @brief Shutdown all sensors and their event loops.
   */
  void Shutdown();

  /**
   * @brief Create ROS 2 publisher controllers for all available sensors.
   * @param ros ROS interface for creating publishers
   * @return Number of controllers created
   */
  size_t CreateControllers(RosInterface& ros);

  /**
   * @brief Get all sensor controllers.
   * @return Vector of sensor data providers (controllers)
   */
  const std::vector<std::unique_ptr<SensorDataProvider>>& GetControllers() const;

  /**
   * @brief Enable a sensor publisher by unique ID.
   * @param unique_id Unique identifier for the sensor
   * @return true if sensor was found and enabled
   */
  bool EnableSensor(const std::string& unique_id);

  /**
   * @brief Disable a sensor publisher by unique ID.
   * @param unique_id Unique identifier for the sensor
   * @return true if sensor was found and disabled
   */
  bool DisableSensor(const std::string& unique_id);

  /**
   * @brief Disable all sensor publishers and clear controllers.
   */
  void ClearControllers();

 private:
  std::string package_name_;
  Sensors sensors_;
  std::vector<std::unique_ptr<SensorDataProvider>> controllers_;
};

}  // namespace ros2_android
