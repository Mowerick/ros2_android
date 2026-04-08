#pragma once

#include <jni.h>
#include <uxr/agent/transport/custom/CustomAgent.hpp>

#include <atomic>
#include <string>

namespace ros2_android {

// JNI-based serial transport for Micro-XRCE-DDS Agent.
// Routes I/O through the existing UsbSerialBridge -> BufferedUsbSerialPort
// Java stack, reusing the same pattern as the YDLIDAR SDK integration.
class JniSerialTransport {
 public:
  JniSerialTransport(const std::string& device_id, int baudrate);
  ~JniSerialTransport();

  // CustomAgent callback functions
  bool Init();
  bool Fini();

  ssize_t RecvMsg(eprosima::uxr::CustomEndPoint* source_endpoint,
                  uint8_t* buffer, size_t buffer_length, int timeout,
                  eprosima::uxr::TransportRc& transport_rc);

  ssize_t SendMsg(const eprosima::uxr::CustomEndPoint* destination_endpoint,
                  uint8_t* buffer, size_t message_length,
                  eprosima::uxr::TransportRc& transport_rc);

  bool IsOpen() const { return is_open_.load(); }

 private:
  JNIEnv* GetJNIEnv();

  std::string device_id_;
  int baudrate_;
  std::atomic<bool> is_open_{false};

  // Java object reference to BufferedUsbSerialPort (global ref)
  jobject java_port_ref_ = nullptr;
};

}  // namespace ros2_android
