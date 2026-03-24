#include "lidar/impl/ydlidar_device.h"

#include <unistd.h>
#include <chrono>
#include <cmath>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/stat.h>

#include "core/log.h"
#include "core/notification_queue.h"
#include "jni/jvm.h"
#include "src/CYdLidar.h"

namespace ros2_android
{

  YDLidarDevice::YDLidarDevice(const std::string &device_path, const std::string &unique_id, jobject usb_manager)
      : device_path_(device_path),
        unique_id_(unique_id),
        is_scanning_(false),
        shutdown_(false),
        pty_master_fd_(-1),
        pty_slave_path_(""),
        usb_manager_ref_(nullptr),
        write_to_usb_method_(nullptr),
        lidar_(std::make_unique<CYdLidar>())
  {
    LOGI("LIDAR device created: id=%s", unique_id_.c_str());

    // Get JNI environment and create global reference to UsbDeviceManager
    JNIEnv *env = ros2_android::GetJNIEnv();
    if (!env)
    {
      LOGE("Failed to get JNI environment in YDLidarDevice constructor");
      return;
    }

    // Create global reference (will persist across JNI calls)
    usb_manager_ref_ = env->NewGlobalRef(usb_manager);
    if (!usb_manager_ref_)
    {
      LOGE("Failed to create global reference to UsbDeviceManager");
      return;
    }

    // Cache the writeToPhysicalUsb method ID
    jclass usb_manager_class = env->GetObjectClass(usb_manager);
    write_to_usb_method_ = env->GetMethodID(usb_manager_class, "writeToPhysicalUsb", "([B)V");
    env->DeleteLocalRef(usb_manager_class);

    if (!write_to_usb_method_)
    {
      LOGE("Failed to find writeToPhysicalUsb method");
      env->DeleteGlobalRef(usb_manager_ref_);
      usb_manager_ref_ = nullptr;
      return;
    }

    LOGI("JNI references cached successfully");
  }

  YDLidarDevice::~YDLidarDevice()
  {
    Shutdown();
  }

  bool YDLidarDevice::Initialize()
  {
    LOGI("Initializing LIDAR: %s", unique_id_.c_str());

    // Create PTY pair
    pty_master_fd_ = posix_openpt(O_RDWR | O_NOCTTY);
    if (pty_master_fd_ < 0)
    {
      LOGE("Failed to create PTY: %s", strerror(errno));
      PostNotification(NotificationSeverity::ERROR, "Failed to create PTY");
      return false;
    }

    if (grantpt(pty_master_fd_) != 0 || unlockpt(pty_master_fd_) != 0)
    {
      LOGE("Failed to setup PTY: %s", strerror(errno));
      close(pty_master_fd_);
      pty_master_fd_ = -1;
      PostNotification(NotificationSeverity::ERROR, "Failed to setup PTY");
      return false;
    }

    char *slave_name = ptsname(pty_master_fd_);
    if (!slave_name)
    {
      LOGE("Failed to get PTY slave name: %s", strerror(errno));
      close(pty_master_fd_);
      pty_master_fd_ = -1;
      PostNotification(NotificationSeverity::ERROR, "Failed to get PTY slave name");
      return false;
    }
    pty_slave_path_ = slave_name;

    // CRITICAL: Set PTY master to raw mode BEFORE SDK tries to use slave
    struct termios tios;
    if (tcgetattr(pty_master_fd_, &tios) == 0)
    {
      cfmakeraw(&tios); // Disable echo, canonical mode, signals
      tcsetattr(pty_master_fd_, TCSANOW, &tios);
      LOGI("PTY master set to raw mode");
    }
    else
    {
      LOGW("Failed to set PTY master to raw mode: %s", strerror(errno));
    }

    // Also set the slave to raw mode
    int slave_fd = open(pty_slave_path_.c_str(), O_RDWR | O_NOCTTY);
    if (slave_fd >= 0)
    {
      if (tcgetattr(slave_fd, &tios) == 0)
      {
        cfmakeraw(&tios);
        tcsetattr(slave_fd, TCSANOW, &tios);
        LOGI("PTY slave also set to raw mode");
      }
      close(slave_fd);
    }
    else
    {
      LOGW("Could not open PTY slave to set raw mode: %s", strerror(errno));
    }

    LOGI("PTY created: master_fd=%d, slave=%s", pty_master_fd_, pty_slave_path_.c_str());

    // Set lidar type (TYPE_TOF for TG series)
    int lidar_type = TYPE_TOF;
    lidar_->setlidaropt(LidarPropLidarType, &lidar_type, sizeof(int));

    // Set device type to serial
    int device_type = YDLIDAR_TYPE_SERIAL;
    lidar_->setlidaropt(LidarPropDeviceType, &device_type, sizeof(int));

    // Enable SDK debug logging (using direct method call, not property)
    lidar_->setEnableDebug(true);

    // Set common parameters with reasonable defaults
    int baudrate = 230400; // Common default for most YDLIDARs
    lidar_->setlidaropt(LidarPropSerialBaudrate, &baudrate, sizeof(int));

    // Sample rate (20 for TOF bidirectional communication)
    int sample_rate = 20;
    lidar_->setlidaropt(LidarPropSampleRate, &sample_rate, sizeof(int));

    // Abnormal check count
    int abnormal_check = 4;
    lidar_->setlidaropt(LidarPropAbnormalCheckCount, &abnormal_check, sizeof(int));

    // Bidirectional communication (not single channel)
    bool single_channel = false;
    lidar_->setlidaropt(LidarPropSingleChannel, &single_channel, sizeof(bool));

    // Enable auto-reconnect
    bool auto_reconnect = true;
    lidar_->setlidaropt(LidarPropAutoReconnect, &auto_reconnect, sizeof(bool));

    // Motor DTR control
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

    // Configure SDK with PTY slave path
    lidar_->setlidaropt(LidarPropSerialPort, pty_slave_path_.c_str(), pty_slave_path_.size());

    // CRITICAL: Start PTY read thread BEFORE initializing SDK
    // SDK initialize() may send commands immediately to query device info
    LOGI("Starting PTY bridge thread before SDK initialization");
    pty_read_thread_ = std::thread(&YDLidarDevice::PtyReadThread, this);

    // Small delay to ensure PTY thread is ready
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Initialize SDK (will open PTY slave and may communicate with device)
    LOGI("Initializing YDLIDAR SDK on PTY slave: %s", pty_slave_path_.c_str());
    bool init_result = lidar_->initialize();

    if (!init_result)
    {
      const char *error = lidar_->DescribeError();
      LOGE("YDLidar SDK initialize failed: %s", error ? error : "unknown error");
      PostNotification(NotificationSeverity::ERROR, std::string("LIDAR SDK init failed: ") + (error ? error : "unknown"));

      // Clean up PTY thread
      shutdown_ = true;
      if (pty_read_thread_.joinable())
      {
        pty_read_thread_.join();
      }

      close(pty_master_fd_);
      pty_master_fd_ = -1;
      return false;
    }

    LOGI("LIDAR SDK initialized successfully on PTY slave");
    PostNotification(NotificationSeverity::WARNING, "LIDAR PTY bridge initialized");
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

    // Wait for both threads to finish
    if (read_thread_.joinable())
    {
      read_thread_.join();
    }
    if (pty_read_thread_.joinable())
    {
      pty_read_thread_.join();
    }

    // Disconnect SDK
    if (lidar_)
    {
      lidar_->disconnecting();
    }

    // Close PTY master
    if (pty_master_fd_ >= 0)
    {
      close(pty_master_fd_);
      pty_master_fd_ = -1;
      LOGI("PTY master closed");
    }

    // Release JNI global references
    JNIEnv *env = ros2_android::GetJNIEnv();
    if (env && usb_manager_ref_)
    {
      env->DeleteGlobalRef(usb_manager_ref_);
      usb_manager_ref_ = nullptr;
      LOGI("JNI global references released");
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
    // Note: pty_read_thread_ already started in Initialize()

    PostNotification(NotificationSeverity::WARNING, "LIDAR scanning started");
    LOGI("LIDAR scanning started with PTY bridge: %s", unique_id_.c_str());
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

  void YDLidarDevice::PtyReadThread()
  {
    LOGI("PTY read thread started for SDK commands: %s", unique_id_.c_str());

    // Get JNI environment for this thread
    JNIEnv *env = ros2_android::GetJNIEnv();
    if (!env)
    {
      LOGE("Failed to attach JNI environment to PTY read thread");
      return;
    }

    struct pollfd pfd = {pty_master_fd_, POLLIN, 0};
    uint8_t buffer[256];

    int poll_count = 0;
    while (!shutdown_)
    {
      // Poll with 100ms timeout for clean shutdown
      int ret = poll(&pfd, 1, 100);
      poll_count++;

      if (ret < 0)
      {
        LOGE("poll() error on PTY master: %s", strerror(errno));
        break;
      }

      if (ret > 0 && (pfd.revents & POLLIN))
      {
        // Data available from SDK (command to LIDAR)
        ssize_t bytes_read = read(pty_master_fd_, buffer, sizeof(buffer));

        if (bytes_read > 0)
        {
          // Convert to jbyteArray
          jbyteArray jdata = env->NewByteArray(bytes_read);
          if (!jdata)
          {
            LOGE("Failed to allocate jbyteArray");
            continue;
          }

          env->SetByteArrayRegion(jdata, 0, bytes_read, reinterpret_cast<jbyte *>(buffer));

          // Call Kotlin writeToPhysicalUsb()
          env->CallVoidMethod(usb_manager_ref_, write_to_usb_method_, jdata);

          env->DeleteLocalRef(jdata);

          // Check for JNI exceptions
          if (env->ExceptionCheck())
          {
            env->ExceptionDescribe();
            env->ExceptionClear();
            LOGE("Exception calling writeToPhysicalUsb()");
          }

          LOGI("Forwarded %zd bytes from SDK to USB", bytes_read);
        }
        else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
          LOGE("PTY read error: %s", strerror(errno));
          break;
        }
      }
    }

    LOGI("PTY read thread stopped after %d polls: %s", poll_count, unique_id_.c_str());
  }

} // namespace ros2_android
