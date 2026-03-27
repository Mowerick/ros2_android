#pragma once

#include <android/log.h>

// LOGD is only enabled in Debug builds to reduce logging overhead in Release
#if ROS2_ANDROID_DEBUG
#define LOGD(...) \
  ((void)__android_log_print(ANDROID_LOG_DEBUG, "ros2_android", __VA_ARGS__))
#else
#define LOGD(...) ((void)0)
#endif

#define LOGI(...) \
  ((void)__android_log_print(ANDROID_LOG_INFO, "ros2_android", __VA_ARGS__))
#define LOGW(...) \
  ((void)__android_log_print(ANDROID_LOG_WARN, "ros2_android", __VA_ARGS__))
#define LOGE(...) \
  ((void)__android_log_print(ANDROID_LOG_ERROR, "ros2_android", __VA_ARGS__))