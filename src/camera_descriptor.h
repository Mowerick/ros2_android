#pragma once

#include <camera/NdkCameraMetadata.h>

#include <string>

namespace ros2_android {
struct CameraDescriptor {
  std::string GetName() const;

  // An id identifying the camera
  std::string id;

  // Which way the lens if facing (back, external or front).
  acamera_metadata_enum_acamera_lens_facing lens_facing;

  // True if this camera is a logical multi-camera device (fuses multiple
  // physical sensors).  Used to prefer it over standalone physical cameras
  // that share the same facing direction.
  bool is_logical_multi_camera = false;

  // TODO intrinsics, supported resolutions, supported frame rates,
  // distortion parameters, etc.
};
}  // namespace ros2_android
