# YDLIDAR Integration on Android

## Overview

This document summarizes the investigation and implementation attempts to integrate YDLIDAR TG15 (TOF LIDAR) with the ROS2 Android application. The YDLIDAR SDK is designed for desktop Linux systems with direct TTY device access (`/dev/ttyUSB0`), which creates fundamental challenges on non-rooted Android devices.

**Device tested**: YDLIDAR TG15 (TOF LIDAR, 512000 baud, single-channel mode)

**Conclusion**: Direct YDLIDAR SDK integration requires **rooted Android devices** due to SELinux restrictions on USB device access.

---

## YDLIDAR SDK Architecture

The YDLIDAR SDK (version 1.2.19) provides unified support for all YDLIDAR models through a layered architecture:

```
Application Layer
    ↓
CYdLidar (High-level API)
    ↓
YDlidarDriver (Protocol layer)
    ↓
Serial (I/O abstraction)
    ↓
unix_serial.cpp (POSIX implementation)
    ↓
/dev/ttyUSB* (Linux TTY device)
```

### Key SDK Characteristics

- **Direct TTY access**: Expects standard Linux serial port paths (`/dev/ttyUSB0`, `/dev/ttyACM0`)
- **POSIX I/O**: Uses `open()`, `read()`, `write()`, `ioctl()`, `pselect()` system calls
- **Model detection**: Auto-detects LIDAR model via device info queries (bidirectional communication)
- **Threading model**: Creates internal read thread for asynchronous data acquisition
- **Configuration**: Baudrate, sample rate, angle range, scan frequency set via `setlidaropt()`

### TG15-Specific Parameters

```cpp
lidar_type = TYPE_TOF              // Time-of-Flight LIDAR
device_type = YDLIDAR_TYPE_SERIAL  // Serial communication
baudrate = 512000                  // TG15 uses 512000 (not 230400!)
sample_rate = 20                   // TOF bidirectional communication
single_channel = true              // No response headers (TG-series)
scan_frequency = 8.0               // 8Hz scan rate (range: 3-15.7Hz)
min_range = 0.05, max_range = 64.0 // TOF range limits (meters)
```

---

## Android USB Constraints

### Non-Rooted Android Limitations

1. **No direct TTY access**: `/dev/ttyUSB*` devices either don't exist or are inaccessible due to permissions
2. **SELinux enforcement**: Mandatory Access Control (MAC) blocks direct USB device access
3. **USB Host API requirement**: Android enforces Java `UsbManager` / `UsbDeviceConnection` API
4. **File descriptor restrictions**: FDs from `UsbDeviceConnection.getFileDescriptor()` cannot be used with POSIX I/O in native code

### Rooted Android

- Direct access to `/dev/ttyUSB0` possible with permission changes
- SELinux policies can be modified or disabled
- Standard Linux serial port workflow applies

---

## Approach 1: PTY Bridge (Failed)

### Concept

Create a pseudo-terminal (PTY) bridge to forward USB data from Java to the YDLIDAR SDK:

```
USB Device (Java API)
    ↓ read via bulkTransfer()
Java USB Reader Thread
    ↓ write to master FD
/dev/pts/X (PTY slave)
    ↓ SDK reads from slave path
YDLIDAR SDK
```

### Implementation

**Native layer** (`src/lidar/impl/ydlidar_device.cc`):
- Created PTY using `posix_openpt()`, `grantpt()`, `unlockpt()`, `ptsname()`
- Spawned `PtyReadThread()` to read from Java via JNI callback and write to PTY master FD
- Passed PTY slave path (`/dev/pts/X`) to SDK via `CYdLidar::initialize()`

**Java layer** (`UsbDeviceManager.kt`):
- Used `usb-serial-for-android` library for serial port abstraction
- Configured `UsbSerialPort` with 512000 baud, 8N1
- Created `SerialInputOutputManager` to read USB data
- Forwarded data to native via `nativeWriteToPty(byte[])`

### Failure Reason

**SELinux denies PTY access on non-rooted Android:**

```
adb logcat | grep avc
avc: denied { read write } for path="/dev/pts/1" dev="devpts"
    scontext=u:r:untrusted_app:s0:c168,c259,c512,c768
    tcontext=u:object_r:devpts:s0 tclass=chr_file permissive=0
```

- SELinux policy `untrusted_app` domain cannot access `devpts` device type
- PTY slave path (`/dev/pts/X`) is inaccessible to the app
- SDK's `Serial::open("/dev/pts/X")` fails with `errno=13 (EACCES)`

**Alternative**: Attempted `/data/local/tmp/pty` symlink - also blocked by SELinux.

### Limitations

- Cannot modify SELinux policies without root
- Android Bionic libc provides PTY APIs but SELinux enforcement prevents usage
- PTY approach is fundamentally incompatible with Android security model for non-rooted devices

---

## Approach 2: Direct FD Passing (Failed)

### Concept

Pass the Java USB file descriptor directly to the YDLIDAR SDK, bypassing TTY paths:

```
USB Device (Java API)
    ↓ UsbDeviceConnection.open()
File Descriptor (FD)
    ↓ pass via JNI
YDLIDAR SDK (use FD directly)
```

### Implementation

**SDK modifications** (`deps/ydlidar_sdk/`):

1. **unix_serial.cpp**: Added `openWithFileDescriptor(int fd, bool skip_config)`
   - Stores FD directly: `fd_ = fd; is_open_ = true;`
   - Skips `::open()` and `tcsetattr()` calls (USB already configured by Java)
   - Port name set to `"fd:117"` for identification

2. **YDlidarDriver.cpp**: Added `connectWithFd(int fd, uint32_t baudrate, bool skip_config)`
   - Creates `Serial` instance and calls `openWithFileDescriptor()`
   - Bypasses standard `connect(const char *port_path, ...)`

3. **CYdLidar.cpp**: Added `initializeWithFileDescriptor(int fd, bool skip_config)`
   - Calls `connectWithFd()` instead of `initialize(const char *port_path, ...)`
   - Skips health/info checks for TOF LIDARs (timeout optimization)

**Parameter configuration** (`src/lidar/impl/ydlidar_device.cc`):
- All `setlidaropt()` calls moved to `SetUsbFileDescriptor()` (after SDK initialization)
- Critical: TG15 requires `baudrate=512000` and `single_channel=true`

**Auto-reconnect handling**:
- Modified `checkAutoConnecting()` to detect FD-based ports (`port.find("fd:") == 0`)
- Disabled auto-reconnect for FD-based connections (cannot reopen externally-owned FD)
- Modified `Serial::close()` to preserve `fd_` and `is_open_` state for FD-based ports

### Failure Reason

**SELinux blocks POSIX I/O operations on Java USB file descriptors:**

Initialization succeeds:
```
I YDLidar_SDK: Successfully opened with FD 6, fd_=6, is_open_=1
I YDLidar_SDK: Successed to start scan mode
```

But read thread immediately times out:
```
E YDLidar_SDK: Timeout count: 1
E YDLidar_SDK: Timeout count: 2
E YDLidar_SDK: Timeout count: 3
E YDLidar_SDK: Failed to turn on the Lidar [Operation timed out]
```

**Root cause**: Even though the FD is valid, native calls fail:
- `read(fd, buffer, size)` - blocked by SELinux
- `ioctl(fd, TIOCINQ, &count)` - blocked by SELinux
- `pselect(fd + 1, ...)` - blocked by SELinux

The FD from `UsbDeviceConnection.getFileDescriptor()` is only usable through **Java USB API methods** (`bulkTransfer()`, `controlTransfer()`), not through POSIX system calls in native code.

### Code Modifications Summary

**Files modified**:
- `deps/ydlidar_sdk/core/serial/impl/unix/unix_serial.cpp` (188 lines changed)
- `deps/ydlidar_sdk/src/YDlidarDriver.cpp` (130 lines changed)
- `deps/ydlidar_sdk/src/CYdLidar.cpp` (45 lines changed)
- `src/lidar/impl/ydlidar_device.cc` (parameter configuration timing)
- `src/lidar/impl/ydlidar_device.h` (added `SetUsbFileDescriptor()` method)
- `app/src/main/kotlin/.../UsbDeviceManager.kt` (FD extraction and JNI bridge)

**Key insights**:
- FD-based ports must ignore `close()` calls (cannot reopen externally-owned FD)
- Auto-reconnect must be disabled for FD-based connections
- TG15 uses 512000 baud (confirmed via PC test), not 230400
- TOF LIDARs need `single_channel=true` (no response headers)
- Health/info checks timeout on TOF LIDARs (4+ second delay if not skipped)

### Limitations

- SELinux restrictions prevent native POSIX I/O on USB file descriptors
- Alternative (data forwarding) would require complete SDK I/O layer rewrite
- Timing-critical TOF communication (512000 baud) makes Java-to-native forwarding fragile
- SDK updates would break custom I/O implementation

---

## Working Solution: Rooted Android Devices

### Requirements

- Rooted Android device (Samsung SM-G770F with Magisk tested)
- Modified SELinux policies or permissive mode
- USB serial driver loaded (CP210x or CH340 for YDLIDAR)

### Implementation

**Direct TTY access**:
```cpp
// No modifications needed - standard SDK usage
CYdLidar lidar;
lidar.initialize("/dev/ttyUSB0");
lidar.turnOn();
```

**Automatic device detection**:
```cpp
// Use libudev for automatic YDLIDAR detection
// Detect CP210x (0x10c4:0xea60) or CH340 (0x1a86:0x7523)
std::vector<std::string> ports = YDlidarDriver::lidarPortList();
```

### Benefits

- **Zero SDK modifications**: Use upstream YDLIDAR SDK as-is
- **Full model support**: All YDLIDAR series (TG, Triangle, GS, etc.) work identically
- **Automatic detection**: Standard Linux device enumeration via `libudev`
- **Stable performance**: Direct hardware access, no Java/native overhead
- **Maintainability**: SDK updates can be integrated without code changes
- **Multi-device support**: Multiple LIDARs can be connected simultaneously

### Academic Justification

Rooted device requirements are common and acceptable in robotics research:

- **ROSCon 2022** (Loretz, "ROS2 on Android"): Acknowledged USB sensor integration as "challenging"
- **Industry precedent**: Turtlebot control apps, robotics research platforms use rooted tablets
- **Thesis scope**: Focus is ROS2 Perception & Positioning deployment, not Android security model
- **Transparent documentation**: Failed non-rooted approaches documented as "Hardships" (thesis requirement)

---

## Attempted Approaches Summary

| Approach | Status | Blocker | Code Complexity |
|----------|--------|---------|-----------------|
| **PTY Bridge** | Failed | SELinux denies `/dev/pts/*` access | Medium (PTY + threading) |
| **Direct FD Passing** | Failed | SELinux blocks POSIX I/O on USB FDs | High (SDK I/O layer changes) |
| **Data Forwarding** | Not attempted | Requires complete SDK I/O rewrite | Very High + fragile timing |
| **Rooted Device** | **Working** | None (standard Linux workflow) | None (upstream SDK) |

---

## Recommendations

### For Production (Non-Rooted Required)

If non-rooted Android support is mandatory, the only viable path is:

1. **Complete SDK I/O layer rewrite**: Replace POSIX calls with callback-based architecture
2. **Java-side data acquisition**: Use `UsbDeviceConnection.bulkTransfer()` to read USB data
3. **JNI data injection**: Feed raw bytes to rewritten SDK via JNI callback
4. **Protocol re-implementation**: Handle YDLIDAR communication protocol in custom code
5. **Extensive testing**: Validate timing constraints for TOF communication (512000 baud)

**Estimated effort**: 3-4 weeks of development + testing
**Risk**: High - timing-sensitive protocol, difficult to maintain across SDK versions

### For Research/Thesis (Recommended)

Use rooted Android devices:

- **Setup time**: 1-2 hours (root device, install drivers)
- **Code changes**: None (standard SDK usage)
- **Reliability**: High (proven desktop Linux workflow)
- **Documentation**: Transparent reporting of non-rooted limitations in thesis

---

## Technical Details for Thesis Documentation

### Motivation

YDLIDAR integration enables laser-based obstacle detection and mapping for the ROS2 Perception subsystem. The TG15 TOF LIDAR provides 360° scanning with 0.05-64m range, critical for outdoor navigation.

### Approach Chosen

Direct TTY device access on rooted Android devices using unmodified YDLIDAR SDK.

### Alternatives Considered

1. **PTY Bridge**: Rejected due to SELinux `/dev/pts/*` access denial
2. **Direct FD Passing**: Rejected due to SELinux blocking POSIX I/O on USB file descriptors
3. **Data Forwarding**: Rejected due to complexity, timing constraints, and maintainability concerns

### Hardships and Workarounds

**PTY Bridge failure**:
- **Problem**: SELinux policy `untrusted_app` cannot access `devpts` device type
- **Attempted workaround**: Symlink PTY to `/data/local/tmp/` - still blocked by SELinux
- **Resolution**: Approach abandoned, not viable without root

**FD Passing failure**:
- **Problem**: Java USB FDs only work with Java API methods, not POSIX calls
- **Investigation**: Added extensive logging, traced SDK I/O flow, confirmed SELinux blocking
- **Attempted fixes**: Modified `close()` to preserve FD state, disabled auto-reconnect
- **Resolution**: Fundamental Android limitation, requires data forwarding architecture

**Configuration issues**:
- **Problem**: SDK showed 230400 baud despite code changes
- **Root cause**: `Initialize()` called after `SetUsbFileDescriptor()` but skipped due to flag
- **Resolution**: Moved all parameter configuration to `SetUsbFileDescriptor()`

**TG15-specific issues**:
- **Problem**: Timeout waiting for response headers
- **Root cause**: TG-series uses single-channel mode (no bidirectional headers)
- **Resolution**: Set `single_channel=true`, discovered via SDK code analysis

**Baudrate detection**:
- **Problem**: Unclear if TG15 uses 230400 or 512000 baud
- **Resolution**: PC test confirmed 512000 baud via YDLIDAR sample code

### Limitations

- **Rooted device requirement**: Not suitable for consumer applications
- **Device-specific setup**: Root method varies by manufacturer (Magisk, TWRP, etc.)
- **SELinux permissive mode**: May reduce overall system security
- **USB driver dependencies**: CP210x or CH340 kernel modules must be available

### Platform-Specific Issues

**Android Bionic vs. glibc**:
- Bionic provides PTY APIs (`posix_openpt()`, etc.) but SELinux enforcement blocks usage
- No `shm_open()` support (irrelevant for LIDAR, but noted for thesis completeness)

**USB Host API design**:
- Java-centric design assumes all USB I/O happens through `UsbDeviceConnection` methods
- File descriptors are exposed but intentionally neutered for native use (security by design)

**SELinux enforcement**:
- Cannot be disabled without root (even `setenforce 0` requires root shell)
- Policies are compiled into system image, not runtime-modifiable

### Dependencies and Versions

- **YDLIDAR SDK**: 1.2.19 (from official repository)
- **Android NDK**: 25.1.8937393
- **Android API Level**: 33 (Android 13)
- **Kernel modules**: `cp210x` or `ch341` (for USB-to-serial conversion)
- **USB serial library**: `usb-serial-for-android` 3.7.3 (for PTY approach only)

### References

- [sensors_for_ros](https://github.com/sloretz/sensors_for_ros) - Shane Loretz, ROSCon 2022
- [YDLIDAR SDK Documentation](https://github.com/YDLIDAR/YDLidar-SDK)
- Android USB Host API: https://developer.android.com/guide/topics/connectivity/usb/host
- SELinux for Android: https://source.android.com/docs/security/features/selinux

---

## Code Locations

### Native Layer

- **LIDAR device wrapper**: `src/lidar/impl/ydlidar_device.{cc,h}`
- **LIDAR controller**: `src/lidar/controllers/lidar_controller.{cc,h}`
- **YDLIDAR SDK**: `deps/ydlidar_sdk/` (submodule)
- **JNI bridge**: `src/jni/jni_bridge.cc` (USB manager bindings)

### Kotlin Layer

- **USB device manager**: `app/src/main/kotlin/.../UsbDeviceManager.kt`
- **External devices UI**: `app/src/main/kotlin/.../ui/screens/ExternalDevicesScreen.kt`
- **ROS ViewModel**: `app/src/main/kotlin/.../viewmodels/RosViewModel.kt`

### Modified SDK Files (FD Approach)

- `deps/ydlidar_sdk/core/serial/impl/unix/unix_serial.cpp`
- `deps/ydlidar_sdk/src/YDlidarDriver.cpp`
- `deps/ydlidar_sdk/src/CYdLidar.cpp`

---

## Build Instructions

### Standard Build (Rooted Devices)

```bash
cd ros2_android/
make deps      # Fetch dependencies
make native    # Cross-compile ROS2 + YDLIDAR SDK
make app       # Build APK
make run       # Install and launch
```

### Testing on Rooted Device

1. **Root device**: Install Magisk via TWRP recovery
2. **Set SELinux permissive** (optional): `adb shell su -c setenforce 0`
3. **Verify TTY device**: `adb shell su -c ls -l /dev/ttyUSB*`
4. **Install APK**: `make run`
5. **Connect LIDAR**: USB OTG adapter → YDLIDAR TG15
6. **Enable publishing**: Tap LIDAR in External Devices screen

### Expected Behavior

- LIDAR auto-detected via `/dev/ttyUSB0`
- Initialization < 1 second (health/info checks skipped for TOF)
- Scan start: 3-4 seconds (motor spin-up + first data packet)
- ROS2 topic: `/scan` (sensor_msgs/LaserScan, 8Hz)

---

## Conclusion

YDLIDAR integration on Android is **feasible only with rooted devices** due to fundamental SELinux restrictions on USB device access. The PTY bridge and direct FD passing approaches both failed despite extensive implementation efforts. For a research thesis, this limitation is acceptable and well-documented in academic literature. For production applications requiring non-rooted support, a complete SDK I/O layer rewrite would be necessary.
