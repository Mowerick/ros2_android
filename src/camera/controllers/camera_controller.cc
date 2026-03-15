#include "camera/controllers/camera_controller.h"

#include <sstream>

#include "core/camera_frame_callback_queue.h"
#include "core/log.h"
#include "core/notification_queue.h"

using ros2_android::CameraController;
using ros2_android::CameraDevice;
using ros2_android::NotificationSeverity;
using ros2_android::PostNotification;
using sensor_msgs::msg::CameraInfo;
using sensor_msgs::msg::Image;

CameraController::CameraController(CameraManager *camera_manager,
                                   const CameraDescriptor &camera_descriptor,
                                   RosInterface &ros)
    : camera_manager_(camera_manager),
      camera_descriptor_(camera_descriptor),
      SensorDataProvider(camera_descriptor.GetName()),
      info_pub_(ros),
      image_pub_(ros)
{
  std::string info_topic = camera_descriptor_.topic_prefix + "camera_info";
  std::string image_topic = camera_descriptor_.topic_prefix + "image_color";

  info_pub_.SetTopic(info_topic.c_str());
  image_pub_.SetTopic(image_topic.c_str());

  // Use BEST_EFFORT for both topics (standard for camera streaming)
  auto qos = rclcpp::QoS(1).best_effort();
  info_pub_.SetQos(qos);
  image_pub_.SetQos(qos);
}

CameraController::~CameraController() {}

void CameraController::EnableCamera()
{
  device_ = camera_manager_->OpenCamera(camera_descriptor_);
  if (!device_)
  {
    LOGW("Failed to enable camera %s - could not open device (already in use?)",
         camera_descriptor_.display_name.c_str());
    PostNotification(
        NotificationSeverity::WARNING,
        "Failed to enable " + camera_descriptor_.display_name +
            " - could not open device (already in use?)");
    return;
  }
  image_pub_.Enable();
  info_pub_.Enable();
  device_->SetListener(
      std::bind(&CameraController::OnImage, this, std::placeholders::_1));
}

void CameraController::DisableCamera()
{
  image_pub_.Disable();
  info_pub_.Disable();
  device_.reset();
  {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    last_frame_.clear();
  }
}

std::string CameraController::PrettyName() const
{
  std::string name{camera_descriptor_.display_name};
  if (!device_)
  {
    name += " [disabled]";
  }
  return name;
}

std::string CameraController::GetLastMeasurementJson()
{
  std::ostringstream ss;
  ss << "{\"enabled\":" << (IsEnabled() ? "true" : "false");
  if (device_)
  {
    auto [width, height] = device_->Resolution();
    ss << ",\"resolution\":{\"width\":" << width << ",\"height\":" << height
       << "}";
  }
  ss << "}";
  return ss.str();
}

bool CameraController::GetLastMeasurement(jni::SensorReadingData &out_data)
{
  // Cameras don't provide sensor readings, only images
  // Return false to indicate no sensor data available
  return false;
}

void CameraController::OnImage(
    const std::pair<CameraInfo::UniquePtr, Image::UniquePtr> &info_image)
{
  // Always publish when camera is enabled (allows rqt_image_view to discover topic)
  if (info_pub_.Enabled() && image_pub_.Enabled())
  {
    info_pub_.Publish(*info_image.first.get());
    image_pub_.Publish(*info_image.second.get());
  }

  // Convert BGR8 to RGBA for UI preview (libyuv outputs BGR, Android needs RGBA)
  {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    const auto &bgr_data = info_image.second->data;
    int num_pixels = info_image.second->width * info_image.second->height;
    last_frame_.resize(num_pixels * 4);

    // Convert BGR -> RGBA (swap B and R, add alpha)
    // TODO: Implement efficient BGR -> RGBA conversion
    for (int i = 0; i < num_pixels; ++i)
    {
      last_frame_[i * 4 + 0] = bgr_data[i * 3 + 2]; // R (from B position)
      last_frame_[i * 4 + 1] = bgr_data[i * 3 + 1]; // G
      last_frame_[i * 4 + 2] = bgr_data[i * 3 + 0]; // B (from R position)
      last_frame_[i * 4 + 3] = 255;                 // A
    }
  }

  // Trigger callback to notify UI of new camera frame (throttled to 10 Hz)
  ros2_android::PostCameraFrameUpdate(std::string(UniqueId()));
}

bool CameraController::GetLastFrame(std::vector<uint8_t> &out_data,
                                    int &out_width, int &out_height)
{
  std::lock_guard<std::mutex> lock(frame_mutex_);
  if (last_frame_.empty())
    return false;
  out_data = last_frame_; // RGBA data
  out_width = 640;
  out_height = 480;
  return true;
}
