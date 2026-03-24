#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <poll.h>
#include <jni.h>

#include "lidar/base/lidar_device.h"

// Forward declare YDLIDAR SDK class to avoid header pollution
class CYdLidar;

namespace ros2_android
{

  /**
   * YDLIDAR device implementation using YDLIDAR SDK
   * Model-agnostic: SDK auto-detects LIDAR model and configures parameters
   *
   * Supports all YDLIDAR models through SDK:
   * - TG series (TG15, TG30, TG50) - TOF lidars
   * - Triangle series (X2, X4, G4, G6, etc.)
   * - Other series (GS, T, SDM, etc.)
   */
  class YDLidarDevice : public LidarDevice
  {
  public:
    /**
     * Create YDLIDAR device with PTY bridge
     * @param device_path Device path (unused, PTY slave will be auto-generated)
     * @param unique_id Unique identifier for this device
     * @param usb_manager JNI reference to UsbDeviceManager instance
     */
    YDLidarDevice(const std::string &device_path, const std::string &unique_id, jobject usb_manager);
    virtual ~YDLidarDevice();

    // LidarDevice interface
    bool Initialize() override;
    void Shutdown() override;
    bool StartScanning() override;
    void StopScanning() override;
    bool IsScanning() const override { return is_scanning_; }
    const std::string &GetUniqueId() const override { return unique_id_; }
    const std::string &GetDevicePath() const override { return device_path_; }

    /**
     * Get PTY master file descriptor (for JNI write operations)
     */
    int GetPtyMasterFd() const { return pty_master_fd_; }

  private:
    /**
     * Read thread - polls SDK for scan data and emits LaserScanData
     */
    void ReadThread();

    /**
     * Convert SDK LaserScan to our LaserScanData format
     */
    void ConvertScan(const void *sdk_scan);

    /**
     * PTY read thread - shuttles SDK commands to physical USB
     */
    void PtyReadThread();

    std::string device_path_;
    std::string unique_id_;

    std::atomic<bool> is_scanning_;
    std::atomic<bool> shutdown_;
    std::thread read_thread_;

    // PTY bridge infrastructure
    int pty_master_fd_;               // PTY master file descriptor
    std::string pty_slave_path_;      // PTY slave path (e.g., /dev/pts/1)
    std::thread pty_read_thread_;     // Thread: SDK → LIDAR commands
    jobject usb_manager_ref_;         // Global ref to UsbDeviceManager instance
    jmethodID write_to_usb_method_;   // Cached writeToPhysicalUsb method ID

    // YDLIDAR SDK instance
    std::unique_ptr<CYdLidar> lidar_;
  };

} // namespace ros2_android
