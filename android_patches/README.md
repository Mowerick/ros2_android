# Android Patches for YDLIDAR

This directory contains patched files for YDLIDAR SDK and ROS 2 driver to enable Android cross-compilation.

## YDLIDAR SDK Patches

### Files Modified

1. **ydlidar_sdk/ydlidar.h** (from `deps/ydlidar_sdk/core/base/ydlidar.h`)
   - **Issue**: Android Bionic uses XSI `strerror_r()` which returns `char*`, not GNU version which returns `int`
   - **Fix**: Added Android-specific `strerror_r()` handling at line 68-74

2. **ydlidar_sdk/thread.h** (from `deps/ydlidar_sdk/core/base/thread.h`)
   - **Issue**: `pthread_testcancel()` not available in Android Bionic
   - **Fix**: Added `ANDROID` guard to disable `pthread_testcancel()` call at line 158

3. **ydlidar_sdk/base_CMakeLists.txt** (from `deps/ydlidar_sdk/core/base/CMakeLists.txt`)
   - **Issue**: Android Bionic includes pthread in libc, not as separate library
   - **Fix**: Changed to `ELSEIF(NOT ANDROID)` to exclude pthread library on Android at line 9

4. **ydlidar_sdk/network_CMakeLists.txt** (from `deps/ydlidar_sdk/core/network/CMakeLists.txt`)
   - **Issue**: Android Bionic includes rt (realtime) in libc, not as separate library
   - **Fix**: Changed to `ELSEIF(NOT ANDROID)` to exclude rt library on Android at line 8

### Additional Build Configuration

The SDK build also requires the following CMake flag in `dependencies.cmake`:
```cmake
-DCMAKE_CXX_FLAGS="-Wno-format-security"
```
This disables format string security warnings that are treated as errors by the Android NDK compiler.

## YDLIDAR ROS 2 Driver Patches

### Files Modified

1. **ydlidar_ros2_driver/CMakeLists.txt** (from `deps/ydlidar_ros2_driver/CMakeLists.txt`)
   - **Issue**: SDK's CMake config doesn't set `YDLIDAR_SDK_LIBRARY_DIRS` correctly for Android
   - **Fix**: Added explicit library directory for Android at line 37-39:
     ```cmake
     if(ANDROID)
       link_directories(${CMAKE_INSTALL_PREFIX}/lib)
     endif()
     ```

## Installation

These patches are currently applied manually to the deps/ directory after `vcs import`.

**Future improvement**: Automate patch application via CMake's ExternalProject PATCH_COMMAND.

## Platform Compatibility

- **Target Platform**: Android NDK 25.1, arm64-v8a, API 33
- **Tested Bionic Version**: Android 13
- **Build System**: CMake 3.13+
- **Compiler**: Clang 14.0.6 (NDK toolchain)

## References

- Android Bionic differences: https://android.googlesource.com/platform/bionic/+/master/docs/status.md
- YDLIDAR SDK: https://github.com/YDLIDAR/YDLidar-SDK
- YDLIDAR ROS2 Driver: https://github.com/YDLIDAR/ydlidar_ros2_driver
