#include "sensors/impl/gps_location_sensor.h"

#include <cmath>
#include <time.h>  // Required for clock_gettime, CLOCK_REALTIME, CLOCK_BOOTTIME
#include <cstdint> // Required for int64_t

#include "core/log.h"

namespace ros2_android
{

  namespace
  {
    // Helper function to get the current offset between Epoch time and Boot time.
    // Placed in an anonymous namespace so it's only visible within this translation unit.
    int64_t get_boot_time_offset_ns()
    {
      struct timespec realtime_ts, boottime_ts;

      // Sample both clocks as close together as possible
      if (clock_gettime(CLOCK_REALTIME, &realtime_ts) == 0 &&
          clock_gettime(CLOCK_BOOTTIME, &boottime_ts) == 0)
      {

        int64_t realtime_ns = static_cast<int64_t>(realtime_ts.tv_sec) * 1000000000LL + realtime_ts.tv_nsec;
        int64_t boottime_ns = static_cast<int64_t>(boottime_ts.tv_sec) * 1000000000LL + boottime_ts.tv_nsec;

        return realtime_ns - boottime_ns;
      }

      // Fallback if clock_gettime fails (unlikely on Android, but safe to handle)
      return 0;
    }
  } // namespace

  void GpsLocationProvider::OnLocationUpdate(double latitude, double longitude,
                                             double altitude, float accuracy,
                                             float altitude_accuracy,
                                             int64_t timestamp_ns)
  {
    if (!location_callback_)
    {
      // No subscriber yet, silently drop
      return;
    }

    sensor_msgs::msg::NavSatFix msg;

    // Convert Android hardware timestamp (nanoseconds since boot) to ROS epoch time
    // by calculating the dynamic offset between the two clocks.
    int64_t offset_ns = get_boot_time_offset_ns();
    int64_t ros_epoch_timestamp_ns = timestamp_ns + offset_ns;

    msg.header.stamp.sec = static_cast<int32_t>(ros_epoch_timestamp_ns / 1000000000LL);
    msg.header.stamp.nanosec = static_cast<uint32_t>(ros_epoch_timestamp_ns % 1000000000LL);
    msg.header.frame_id = "gps";

    // Set GPS fix status
    msg.status.status = sensor_msgs::msg::NavSatStatus::STATUS_FIX;
    msg.status.service = sensor_msgs::msg::NavSatStatus::SERVICE_GPS;

    // Set position
    msg.latitude = latitude;
    msg.longitude = longitude;
    msg.altitude = altitude;

    // Set covariance (ENU - East, North, Up)
    // Android provides horizontal and vertical accuracy in meters (1-sigma)
    // Covariance = variance = sigma^2
    double horizontal_variance = accuracy * accuracy;
    double vertical_variance = altitude_accuracy * altitude_accuracy;

    // Initialize covariance matrix to zero
    for (int i = 0; i < 9; i++)
    {
      msg.position_covariance[i] = 0.0;
    }

    // Diagonal: East, North, Up variances
    msg.position_covariance[0] = horizontal_variance; // East
    msg.position_covariance[4] = horizontal_variance; // North
    msg.position_covariance[8] = vertical_variance;   // Up

    msg.position_covariance_type =
        sensor_msgs::msg::NavSatFix::COVARIANCE_TYPE_DIAGONAL_KNOWN;

    LOGI("GPS Update: lat=%.6f, lon=%.6f, alt=%.2f, acc=%.2fm", latitude,
         longitude, altitude, accuracy);

    location_callback_(msg);
  }

} // namespace ros2_android