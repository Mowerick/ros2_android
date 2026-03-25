#include "lidar/tty_device_detector.h"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <sstream>

#include "core/log.h"

namespace ros2_android
{

  namespace
  {
    /**
     * @brief Parse VID/PID from sysfs uevent file
     *
     * Reads /sys/class/tty/<device>/device/uevent and extracts:
     * PRODUCT=<vid>/<pid>/<version>
     *
     * @param device_name TTY device name (e.g., "ttyUSB0")
     * @param vendor_id Output: vendor ID
     * @param product_id Output: product ID
     * @return true if VID/PID found, false otherwise
     */
    bool ParseDeviceIds(const std::string &device_name, uint16_t &vendor_id, uint16_t &product_id)
    {
      // Construct path to uevent file
      std::string uevent_path = "/sys/class/tty/" + device_name + "/device/uevent";

      std::ifstream uevent_file(uevent_path);
      if (!uevent_file.is_open())
      {
        // Device may not have uevent (not a USB device)
        return false;
      }

      std::string line;
      while (std::getline(uevent_file, line))
      {
        // Look for PRODUCT=<vid>/<pid>/<version>
        if (line.compare(0, 8, "PRODUCT=") == 0)
        {
          std::string product_value = line.substr(8);

          // Parse VID/PID (format: "10c4/ea60/100")
          std::istringstream ss(product_value);
          std::string vid_str, pid_str;

          if (std::getline(ss, vid_str, '/') && std::getline(ss, pid_str, '/'))
          {
            try
            {
              vendor_id = static_cast<uint16_t>(std::stoul(vid_str, nullptr, 16));
              product_id = static_cast<uint16_t>(std::stoul(pid_str, nullptr, 16));
              return true;
            }
            catch (const std::exception &e)
            {
              LOGW("Failed to parse VID/PID from %s: %s", uevent_path.c_str(), e.what());
              return false;
            }
          }
        }
      }

      return false;
    }

    /**
     * @brief Get kernel driver name from sysfs
     *
     * Reads /sys/class/tty/<device>/device/driver symlink to determine
     * the kernel driver (e.g., "cp210x", "ch341").
     *
     * @param device_name TTY device name (e.g., "ttyUSB0")
     * @return Driver name, or empty string if not found
     */
    std::string GetDriverName(const std::string &device_name)
    {
      std::string driver_path = "/sys/class/tty/" + device_name + "/device/driver";

      char link_target[256];
      ssize_t len = readlink(driver_path.c_str(), link_target, sizeof(link_target) - 1);
      if (len == -1)
      {
        return "";
      }

      link_target[len] = '\0';

      // Extract driver name from path (e.g., "../../drivers/usb/serial/cp210x" -> "cp210x")
      std::string target_str(link_target);
      size_t last_slash = target_str.rfind('/');
      if (last_slash != std::string::npos)
      {
        return target_str.substr(last_slash + 1);
      }

      return "";
    }

    /**
     * @brief Check if VID/PID matches known YDLIDAR USB-to-serial chips
     *
     * @param vendor_id USB vendor ID
     * @param product_id USB product ID
     * @return true if known YDLIDAR chip
     */
    bool IsYDLidarDevice(uint16_t vendor_id, uint16_t product_id)
    {
      // CP210x (Silicon Labs)
      if (vendor_id == 0x10c4 && product_id == 0xea60)
      {
        return true;
      }

      // CH340 (Jiangsu QinHeng)
      if (vendor_id == 0x1a86 && product_id == 0x7523)
      {
        return true;
      }

      return false;
    }

  } // anonymous namespace

  std::vector<TtyDevice> DetectYDLidarDevices()
  {
    std::vector<TtyDevice> devices;

    // Open /dev directory
    DIR *dev_dir = opendir("/dev");
    if (!dev_dir)
    {
      LOGE("Failed to open /dev directory: %s", strerror(errno));
      return devices;
    }

    struct dirent *entry;
    while ((entry = readdir(dev_dir)) != nullptr)
    {
      std::string dev_name(entry->d_name);

      // Look for ttyUSB* and ttyACM* devices
      if (dev_name.compare(0, 6, "ttyUSB") != 0 && dev_name.compare(0, 6, "ttyACM") != 0)
      {
        continue;
      }

      uint16_t vendor_id = 0;
      uint16_t product_id = 0;

      // Parse VID/PID from sysfs
      if (!ParseDeviceIds(dev_name, vendor_id, product_id))
      {
        LOGD("Skipping %s: no VID/PID found in sysfs", dev_name.c_str());
        continue;
      }

      // Check if it's a known YDLIDAR device
      if (!IsYDLidarDevice(vendor_id, product_id))
      {
        LOGD("Skipping %s: VID/PID %04x:%04x not a known YDLIDAR chip",
             dev_name.c_str(), vendor_id, product_id);
        continue;
      }

      // Get kernel driver name
      std::string driver = GetDriverName(dev_name);

      // Construct device info
      TtyDevice device;
      device.path = "/dev/" + dev_name;
      device.driver = driver;
      device.vendor_id = vendor_id;
      device.product_id = product_id;

      LOGI("Detected YDLIDAR device: %s (VID/PID %04x:%04x, driver: %s)",
           device.path.c_str(), vendor_id, product_id, driver.c_str());

      devices.push_back(device);
    }

    closedir(dev_dir);

    if (devices.empty())
    {
      LOGW("No YDLIDAR devices detected in /dev");
    }
    else
    {
      LOGI("Found %zu YDLIDAR device(s)", devices.size());
    }

    return devices;
  }

  bool CanAccessTtyDevice(const std::string &path)
  {
    // Attempt to open device with read/write access
    int fd = open(path.c_str(), O_RDWR | O_NOCTTY);
    if (fd == -1)
    {
      LOGW("Cannot access %s: %s", path.c_str(), strerror(errno));
      return false;
    }

    // Success - close immediately
    close(fd);
    LOGI("Successfully accessed %s", path.c_str());
    return true;
  }

} // namespace ros2_android
