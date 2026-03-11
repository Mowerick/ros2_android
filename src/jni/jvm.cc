#include "jni/jvm.h"

static JavaVM* g_java_vm = nullptr;

void ros2_android::SetJavaVM(JavaVM* vm) {
  g_java_vm = vm;
}

JavaVM* ros2_android::GetJavaVM() {
  return g_java_vm;
}
