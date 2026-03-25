#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ros2_android
{

  /**
   * @brief Represents a TTY device detected on the system
   *
   * This structure holds information about a TTY device (e.g., /dev/ttyUSB0)
   * that is potentially a YDLIDAR device based on VID/PID matching.
   */
  struct TtyDevice
  {
    std::string path;        ///< Device path (e.g., "/dev/ttyUSB0")
    std::string driver;      ///< Kernel driver name (e.g., "cp210x", "ch341")
    uint16_t vendor_id;      ///< USB vendor ID (e.g., 0x10c4 for CP210x)
    uint16_t product_id;     ///< USB product ID (e.g., 0xea60 for CP210x)
  };

  /**
   * @brief Detect YDLIDAR devices connected via USB-to-serial adapters
   *
   * This function enumerates all ttyUSB* and ttyACM* devices in /dev/ and
   * checks their VID/PID against known YDLIDAR USB-to-serial chips:
   * - CP210x: 0x10c4:0xea60
   * - CH340:  0x1a86:0x7523
   *
   * The implementation reads device information from sysfs:
   * /sys/class/tty/<device>/device/uevent
   *
   * @return Vector of detected YDLIDAR TTY devices (empty if none found)
   *
   * @note This function does NOT require root access for enumeration.
   *       However, accessing the actual /dev/ttyUSB* devices for R/W
   *       requires either root permissions or chmod 666.
   */
  std::vector<TtyDevice> DetectYDLidarDevices();

  /**
   * @brief Test if a TTY device can be opened for reading/writing
   *
   * This function attempts to open the TTY device with O_RDWR | O_NOCTTY
   * flags to verify read/write access. It does NOT perform any I/O operations.
   *
   * @param path TTY device path (e.g., "/dev/ttyUSB0")
   * @return true if device can be opened, false otherwise
   *
   * @note This function is primarily for diagnostics. On non-rooted devices,
   *       this will return false due to permission denied errors.
   *       On rooted devices after chmod 666, this should return true.
   */
  bool CanAccessTtyDevice(const std::string &path);

} // namespace ros2_android
