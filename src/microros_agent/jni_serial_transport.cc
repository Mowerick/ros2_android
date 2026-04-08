#include "microros_agent/jni_serial_transport.h"

#include <android/log.h>
#include <cstring>

#include "core/log.h"

// Reuse the same JNI globals cached in jni_bridge.cc (ydlidar::core::serial namespace)
namespace ydlidar { namespace core { namespace serial {
extern JavaVM* g_javaVM;
extern jclass g_usbSerialBridgeClass;
extern jclass g_bufferedSerialClass;
extern jmethodID g_openDeviceMethod;
extern jmethodID g_closeDeviceMethod;
extern jmethodID g_availableMethod;
extern jmethodID g_readMethod;
extern jmethodID g_writeMethod;
extern jmethodID g_flushMethod;
}}}  // namespace ydlidar::core::serial

// Shorter aliases
using ydlidar::core::serial::g_javaVM;
using ydlidar::core::serial::g_usbSerialBridgeClass;
using ydlidar::core::serial::g_bufferedSerialClass;
using ydlidar::core::serial::g_openDeviceMethod;
using ydlidar::core::serial::g_closeDeviceMethod;
using ydlidar::core::serial::g_readMethod;
using ydlidar::core::serial::g_writeMethod;
using ydlidar::core::serial::g_flushMethod;

namespace ros2_android {

JniSerialTransport::JniSerialTransport(const std::string& device_id,
                                       int baudrate)
    : device_id_(device_id), baudrate_(baudrate) {}

JniSerialTransport::~JniSerialTransport() {
  Fini();
}

JNIEnv* JniSerialTransport::GetJNIEnv() {
  JNIEnv* env = nullptr;
  if (!g_javaVM) {
    LOGE("JniSerialTransport: g_javaVM is null");
    return nullptr;
  }

  int status = g_javaVM->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
  if (status == JNI_EDETACHED) {
    status = g_javaVM->AttachCurrentThread(&env, nullptr);
    if (status != JNI_OK) {
      LOGE("JniSerialTransport: Failed to attach thread: %d", status);
      return nullptr;
    }
  }
  return env;
}

bool JniSerialTransport::Init() {
  JNIEnv* env = GetJNIEnv();
  if (!env || !g_usbSerialBridgeClass || !g_openDeviceMethod) {
    LOGE("JniSerialTransport: JNI not initialized");
    return false;
  }

  // Call UsbSerialBridge.openDevice(deviceId, baudrate, dataBits, stopBits, parity)
  jstring j_device_id = env->NewStringUTF(device_id_.c_str());
  jobject port = env->CallStaticObjectMethod(
      g_usbSerialBridgeClass, g_openDeviceMethod,
      j_device_id, baudrate_, 8, 1, 0);

  env->DeleteLocalRef(j_device_id);

  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    LOGE("JniSerialTransport: Exception opening device %s", device_id_.c_str());
    return false;
  }

  if (!port) {
    LOGE("JniSerialTransport: Failed to open device %s", device_id_.c_str());
    return false;
  }

  java_port_ref_ = env->NewGlobalRef(port);
  env->DeleteLocalRef(port);

  is_open_ = true;
  LOGI("JniSerialTransport: Opened %s at %d baud", device_id_.c_str(),
       baudrate_);
  return true;
}

bool JniSerialTransport::Fini() {
  if (!is_open_) {
    return true;
  }

  JNIEnv* env = GetJNIEnv();
  if (!env) {
    return false;
  }

  // Call UsbSerialBridge.closeDevice(deviceId)
  if (g_closeDeviceMethod) {
    jstring j_device_id = env->NewStringUTF(device_id_.c_str());
    env->CallStaticVoidMethod(g_usbSerialBridgeClass, g_closeDeviceMethod,
                              j_device_id);
    env->DeleteLocalRef(j_device_id);

    if (env->ExceptionCheck()) {
      env->ExceptionDescribe();
      env->ExceptionClear();
    }
  }

  if (java_port_ref_) {
    env->DeleteGlobalRef(java_port_ref_);
    java_port_ref_ = nullptr;
  }

  is_open_ = false;
  LOGI("JniSerialTransport: Closed %s", device_id_.c_str());
  return true;
}

ssize_t JniSerialTransport::RecvMsg(
    eprosima::uxr::CustomEndPoint* /*source_endpoint*/, uint8_t* buffer,
    size_t buffer_length, int timeout,
    eprosima::uxr::TransportRc& transport_rc) {

  if (!is_open_ || !java_port_ref_) {
    transport_rc = eprosima::uxr::TransportRc::connection_error;
    return -1;
  }

  JNIEnv* env = GetJNIEnv();
  if (!env) {
    transport_rc = eprosima::uxr::TransportRc::server_error;
    return -1;
  }

  // Create Java byte array and call BufferedUsbSerialPort.read(byte[], int)
  jbyteArray j_buffer = env->NewByteArray(static_cast<jsize>(buffer_length));
  if (!j_buffer) {
    transport_rc = eprosima::uxr::TransportRc::server_error;
    return -1;
  }

  jint bytes_read = env->CallIntMethod(java_port_ref_, g_readMethod,
                                       j_buffer, timeout);

  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    env->DeleteLocalRef(j_buffer);
    transport_rc = eprosima::uxr::TransportRc::connection_error;
    return -1;
  }

  if (bytes_read <= 0) {
    env->DeleteLocalRef(j_buffer);
    transport_rc = eprosima::uxr::TransportRc::timeout_error;
    return 0;
  }

  // Copy from Java byte array to C++ buffer
  env->GetByteArrayRegion(j_buffer, 0, bytes_read,
                          reinterpret_cast<jbyte*>(buffer));
  env->DeleteLocalRef(j_buffer);

  transport_rc = eprosima::uxr::TransportRc::ok;
  return static_cast<ssize_t>(bytes_read);
}

ssize_t JniSerialTransport::SendMsg(
    const eprosima::uxr::CustomEndPoint* /*destination_endpoint*/,
    uint8_t* buffer, size_t message_length,
    eprosima::uxr::TransportRc& transport_rc) {

  if (!is_open_ || !java_port_ref_) {
    transport_rc = eprosima::uxr::TransportRc::connection_error;
    return -1;
  }

  JNIEnv* env = GetJNIEnv();
  if (!env) {
    transport_rc = eprosima::uxr::TransportRc::server_error;
    return -1;
  }

  // Create Java byte array, fill it, call BufferedUsbSerialPort.write(byte[], int)
  jbyteArray j_buffer = env->NewByteArray(static_cast<jsize>(message_length));
  if (!j_buffer) {
    transport_rc = eprosima::uxr::TransportRc::server_error;
    return -1;
  }

  env->SetByteArrayRegion(j_buffer, 0, static_cast<jsize>(message_length),
                          reinterpret_cast<const jbyte*>(buffer));

  // Write with 1000ms timeout
  env->CallVoidMethod(java_port_ref_, g_writeMethod, j_buffer, 1000);

  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    env->DeleteLocalRef(j_buffer);
    transport_rc = eprosima::uxr::TransportRc::connection_error;
    return -1;
  }

  env->DeleteLocalRef(j_buffer);
  transport_rc = eprosima::uxr::TransportRc::ok;
  return static_cast<ssize_t>(message_length);
}

}  // namespace ros2_android
