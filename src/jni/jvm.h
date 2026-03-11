#pragma once

#include <jni.h>

namespace ros2_android {
void SetJavaVM(JavaVM* vm);
JavaVM* GetJavaVM();
}  // namespace ros2_android
