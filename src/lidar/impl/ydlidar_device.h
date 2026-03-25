#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "lidar/base/lidar_device.h"

// Forward declare YDLIDAR SDK class to avoid header pollution
class CYdLidar;

namespace ros2_android
{

  /**
   * YDLIDAR device implementation using YDLIDAR SDK (rooted device approach)
   * Model-agnostic: SDK auto-detects LIDAR model and configures parameters
   *
   * Supports all YDLIDAR models through SDK:
   * - TG series (TG15, TG30, TG50) - TOF lidars
   * - Triangle series (X2, X4, G4, G6, etc.)
   * - Other series (GS, T, SDM, etc.)
   *
   * Requirements:
   * - Rooted Android device
   * - Root permissions set on TTY device (chmod 666 /dev/ttyUSB0)
   * - Direct TTY path access (e.g., /dev/ttyUSB0)
   */
  class YDLidarDevice : public LidarDevice
  {
  public:
    /**
     * Create YDLIDAR device with TTY path for rooted devices
     * @param tty_path TTY device path (e.g., "/dev/ttyUSB0")
     * @param unique_id Unique identifier for this device
     */
    YDLidarDevice(const std::string &tty_path, const std::string &unique_id);
    virtual ~YDLidarDevice();

    // LidarDevice interface
    bool Initialize() override;
    void Shutdown() override;
    bool StartScanning() override;
    void StopScanning() override;
    bool IsScanning() const override { return is_scanning_; }
    const std::string &GetUniqueId() const override { return unique_id_; }
    const std::string &GetDevicePath() const override { return tty_path_; }

  private:
    /**
     * Read thread - polls SDK for scan data and emits LaserScanData
     */
    void ReadThread();

    /**
     * Convert SDK LaserScan to our LaserScanData format
     */
    void ConvertScan(const void *sdk_scan);

    std::string tty_path_;   // TTY device path (e.g., "/dev/ttyUSB0")
    std::string unique_id_;

    std::atomic<bool> is_scanning_;
    std::atomic<bool> shutdown_;
    std::thread read_thread_;

    // YDLIDAR SDK instance
    std::unique_ptr<CYdLidar> lidar_;
  };

} // namespace ros2_android
