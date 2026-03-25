#include "lidar/impl/ydlidar_device.h"

#include <chrono>
#include <cmath>

#include "core/log.h"
#include "core/notification_queue.h"
#include "src/CYdLidar.h"

namespace ros2_android
{

  YDLidarDevice::YDLidarDevice(const std::string &tty_path, const std::string &unique_id)
      : tty_path_(tty_path),
        unique_id_(unique_id),
        is_scanning_(false),
        shutdown_(false),
        lidar_(std::make_unique<CYdLidar>())
  {
    LOGI("LIDAR device created: id=%s, tty=%s", unique_id_.c_str(), tty_path_.c_str());
  }

  YDLidarDevice::~YDLidarDevice()
  {
    Shutdown();
  }

  bool YDLidarDevice::Initialize()
  {
    LOGI("Initializing LIDAR: %s at %s", unique_id_.c_str(), tty_path_.c_str());

    // Set lidar type (TYPE_TOF for TG series - SDK will auto-detect actual model)
    int lidar_type = TYPE_TOF;
    lidar_->setlidaropt(LidarPropLidarType, &lidar_type, sizeof(int));

    // Set device type to serial
    int device_type = YDLIDAR_TYPE_SERIAL;
    lidar_->setlidaropt(LidarPropDeviceType, &device_type, sizeof(int));

    // Set TTY serial port path
    std::string port_path = tty_path_;
    lidar_->setlidaropt(LidarPropSerialPort, port_path.c_str(), port_path.size());

    // Enable SDK debug logging
    lidar_->setEnableDebug(true);

    // Set baudrate for TG15 (512000 as detected from PC test)
    int baudrate = 512000;
    LOGI("Setting LIDAR baudrate to %d", baudrate);
    lidar_->setlidaropt(LidarPropSerialBaudrate, &baudrate, sizeof(int));

    // Sample rate (20 for TOF bidirectional communication)
    int sample_rate = 20;
    lidar_->setlidaropt(LidarPropSampleRate, &sample_rate, sizeof(int));

    // Abnormal check count
    int abnormal_check = 4;
    lidar_->setlidaropt(LidarPropAbnormalCheckCount, &abnormal_check, sizeof(int));

    // Single channel mode for TG-series TOF LIDARs (no response headers expected)
    bool single_channel = true;
    lidar_->setlidaropt(LidarPropSingleChannel, &single_channel, sizeof(bool));

    // Enable auto-reconnect
    bool auto_reconnect = true;
    lidar_->setlidaropt(LidarPropAutoReconnect, &auto_reconnect, sizeof(bool));

    // Motor DTR control (disabled for TG series)
    bool motor_dtr = false;
    lidar_->setlidaropt(LidarPropSupportMotorDtrCtrl, &motor_dtr, sizeof(bool));

    // Set reasonable angle range (SDK may adjust based on model)
    float min_angle = -180.0f;
    float max_angle = 180.0f;
    lidar_->setlidaropt(LidarPropMinAngle, &min_angle, sizeof(float));
    lidar_->setlidaropt(LidarPropMaxAngle, &max_angle, sizeof(float));

    // Set range limits for TOF (0.05-64m)
    float min_range = 0.05f;
    float max_range = 64.0f;
    lidar_->setlidaropt(LidarPropMinRange, &min_range, sizeof(float));
    lidar_->setlidaropt(LidarPropMaxRange, &max_range, sizeof(float));

    // Set scan frequency (TOF default: 8Hz, range: 3-15.7Hz)
    float scan_freq = 8.0f;
    lidar_->setlidaropt(LidarPropScanFrequency, &scan_freq, sizeof(float));

    // Disable intensity for TOF (not supported)
    bool intensity = false;
    lidar_->setlidaropt(LidarPropIntenstiy, &intensity, sizeof(bool));

    // Initialize SDK with standard serial port path
    LOGI("Initializing YDLIDAR SDK with TTY path: %s", tty_path_.c_str());
    bool init_result = lidar_->initialize();

    if (!init_result)
    {
      const char *error = lidar_->DescribeError();
      LOGE("YDLIDAR SDK initialization failed: %s", error ? error : "unknown error");
      PostNotification(NotificationSeverity::ERROR, std::string("LIDAR init failed: ") + (error ? error : "unknown"));
      return false;
    }

    LOGI("LIDAR SDK initialized successfully");
    PostNotification(NotificationSeverity::WARNING, "LIDAR initialized");
    return true;
  }

  void YDLidarDevice::Shutdown()
  {
    if (shutdown_)
    {
      return;
    }

    LOGI("Shutting down LIDAR: %s", unique_id_.c_str());

    shutdown_ = true;

    // Stop scanning if active
    if (is_scanning_)
    {
      StopScanning();
    }

    // Wait for read thread to finish
    if (read_thread_.joinable())
    {
      read_thread_.join();
    }

    // Disconnect SDK (closes TTY device automatically)
    if (lidar_)
    {
      lidar_->disconnecting();
    }

    LOGI("LIDAR device shut down: %s", unique_id_.c_str());
  }

  bool YDLidarDevice::StartScanning()
  {
    if (is_scanning_)
    {
      LOGW("LIDAR already scanning: %s", unique_id_.c_str());
      return true;
    }

    LOGI("Starting LIDAR scan: %s", unique_id_.c_str());

    // Start SDK scanning (enables motor and starts data acquisition)
    if (!lidar_->turnOn())
    {
      const char *error = lidar_->DescribeError();
      LOGE("Failed to start LIDAR scanning: %s", error ? error : "unknown error");
      PostNotification(NotificationSeverity::ERROR, std::string("LIDAR start failed: ") + (error ? error : "unknown"));
      return false;
    }

    is_scanning_ = true;
    read_thread_ = std::thread(&YDLidarDevice::ReadThread, this);

    PostNotification(NotificationSeverity::WARNING, "LIDAR scanning started");
    LOGI("LIDAR scanning started: %s", unique_id_.c_str());
    return true;
  }

  void YDLidarDevice::StopScanning()
  {
    if (!is_scanning_)
    {
      return;
    }

    LOGI("Stopping LIDAR scan: %s", unique_id_.c_str());

    is_scanning_ = false;

    // Stop SDK scanning (disables motor)
    if (lidar_)
    {
      lidar_->turnOff();
    }

    LOGI("LIDAR scanning stopped: %s", unique_id_.c_str());
  }

  void YDLidarDevice::ReadThread()
  {
    LOGI("LIDAR read thread started: %s", unique_id_.c_str());

    LaserScan sdk_scan;

    while (is_scanning_ && !shutdown_)
    {
      // Get scan from SDK (blocking call with timeout)
      bool success = lidar_->doProcessSimple(sdk_scan);

      if (success && sdk_scan.points.size() > 0)
      {
        // Convert SDK scan to our format
        LaserScanData scan;

        // Timestamp
        scan.timestamp_ns = sdk_scan.stamp;

        // Get config from SDK scan
        scan.angle_min = sdk_scan.config.min_angle;
        scan.angle_max = sdk_scan.config.max_angle;
        scan.angle_increment = (sdk_scan.config.max_angle - sdk_scan.config.min_angle) / sdk_scan.points.size();
        scan.range_min = sdk_scan.config.min_range;
        scan.range_max = sdk_scan.config.max_range;
        scan.scan_time = 1.0f / sdk_scan.scanFreq;
        scan.time_increment = scan.scan_time / sdk_scan.points.size();

        // Convert points
        scan.ranges.reserve(sdk_scan.points.size());
        scan.intensities.reserve(sdk_scan.points.size());

        for (const auto &point : sdk_scan.points)
        {
          scan.ranges.push_back(point.range);
          scan.intensities.push_back(point.intensity);
        }

        // Emit scan data
        Emit(scan);
      }
      else if (!success)
      {
        // Log error but continue (SDK may recover)
        const char *error = lidar_->DescribeError();
        LOGW("LIDAR scan error: %s", error ? error : "unknown");

        // Small delay before retry
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }

    LOGI("LIDAR read thread stopped: %s", unique_id_.c_str());
  }


} // namespace ros2_android
