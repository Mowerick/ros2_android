# ROS 2 Android

An Android app that deploys a ROS 2 Humble Perception & Positioning subsystem on ARM devices (arm64-v8a) using Eclipse Cyclone DDS. Built on top of [sloretz/sensors_for_ros](https://github.com/sloretz/sensors_for_ros) (Loretz, ROSCon 2022) - a CMake superbuild of ~70 ROS 2 Humble packages for Android - restructured from a pure C++ NativeActivity into a Java/Kotlin + Native hybrid with Jetpack Compose UI.

![Android](<https://img.shields.io/badge/Android-13%20(API%2033)-green?logo=android>)
![NDK](https://img.shields.io/badge/NDK-26.3-blue)

## Features

### Implemented

- **Built-in sensor publishers** - accelerometer, barometer, gyroscope, illuminance, magnetometer, GPS location published as ROS 2 topics with frame IDs and timestamps
- **Camera publisher** - front and rear device cameras published as `sensor_msgs/Image` (raw BGR8) and `sensor_msgs/CompressedImage` (JPEG)
- **USB LiDAR** - YDLIDAR SDK integration via JNI serial bridge to Android USB Host API, published as `sensor_msgs/LaserScan`
- **YOLOv9 object detection** - on-device inference via NCNN (YOLOv9-s + Deep SORT tracker) for Colorado Potato Beetle detection (beetle, larva, eggs) with 3D localization from ZED camera point clouds
- **State machine pipeline** - sequential ROS 2 subsystem with dynamic node detection (local vs external execution), automatic dependency progression, and distributed deployment support
- **Wi-Fi multicast / DDS discovery** - `MulticastLock` to enable DDS multicast on Android Wi-Fi
- **DDS domain selection** - configurable `ROS_DOMAIN_ID` and network interface for DDS discovery
- **Event-driven architecture** - JNI callbacks for sensor data and camera frames (zero polling overhead)
- **Notification system** - user notification overlay for alerts and error messages from both native (C++) and Kotlin layers
- **Jetpack Compose UI** - sensor list, live sensor data view, camera preview, pipeline node management with runtime state visualization
- **Testing framework** - Python-based ROS 2 subscriber test suite with matplotlib visualizers for all sensor types
- **Beetle Predator mode** - handheld pest detection using built-in rear camera + GPS. Runs NCNN YOLOv9 + Deep SORT on camera frames, publishes geolocated detections as `vermin_collector_ros_msgs/BeetleDetection` with novelty filtering (only new confirmed tracks). User selects which classes (beetle, larva, eggs) trigger publishing via label filter chips
- **Target manager** - CPB egg selection with IMU-based orientation calibration, subscribes to detection results and ZED IMU, publishes `ESP32_Command` via micro-ROS Agent to ESP32
- **micro-ROS Agent** - XRCE-DDS bridge over USB CDC-ACM serial at 460800 baud, HDLC framing, bridges `ESP32_Command`/`ESP32_Feedback` between ROS 2 DDS network and ESP32-S3 microcontroller

### Known Limitations

- **Large DDS payloads over WiFi** - transporting depth images (~8MB) and PointCloud2 (~33MB) from the external ZED camera over WiFi is partially functional but unreliable. A 33MB PointCloud2 is split into ~25,000 RTPS fragments - at just 0.1% WiFi packet loss, the probability of all fragments arriving is effectively zero. With RELIABLE QoS, retransmission helps but creates congestion; with BEST_EFFORT, any single lost fragment discards the entire message. Compressed RGB images (~50-200KB) work reliably. Requires rooted device for kernel buffer tuning. See [DDS Large Payload Limitations](../docs/DDS_LARGE_PAYLOAD_LIMITATIONS.md) for full analysis.
- **Temporal desynchronization** - RGB, depth, and PointCloud2 topics arrive at vastly different rates over WiFi due to size differences. The perception controller pairs the latest available message from each topic without timestamp synchronization, resulting in mismatched frames for 3D localization.
- **No shared memory** - Android's Bionic libc lacks `shm_open`/`shm_unlink`, restricting DDS to UDP transport only. The ZED camera requires CUDA and must run on an external desktop, forcing all camera data across the WiFi network.

## Architecture

```text
┌─────────────────────────────────────────────────┐
│   Kotlin/Java (Jetpack Compose UI)              │
│   - MainActivity, ViewModels, UI Components     │
│   - GpsManager (FusedLocationProviderClient)    │
└─────┬────────────────────────────────┬──────────┘
      │                                │
      │ GPS location (JNI)             │ JNI Bridge
      │                                │ (NativeBridge.kt ↔ jni_bridge.cc)
      │                                │ - jobject construction
      │                                │ - Event callbacks
┌─────▼────────────────────────────────▼──────────┐
│   C++ Native Layer                              │
│                                                 │
│   ┌───────────────────────────────────────┐     │
│   │ Platform APIs (Android NDK)           │     │
│   │ - ASensorManager  (IMU sensor events) │     │
│   │ - ACameraManager / AImageReader       │     │
│   │ - USB serial I/O  (LiDAR, micro-ROS)  │     │
│   └───────────────┬───────────────────────┘     │
│                   │ raw events / frames         │
│   ┌───────────────▼───────────────────────┐     │
│   │ Sensor Controllers                    │     │
│   │ - IMU (accel, gyro, mag, baro, lux)   │     │
│   │ - GPS (receives via JNI)              │     │
│   │ - Camera (front/back via Camera2 NDK) │     │
│   │ - LiDAR (YDLIDAR via USB serial)      │     │
│   └───────────────┬───────────────────────┘     │
│                   │ publish(msg)                │
│   ┌───────────────▼───────────────────────┐     │
│   │ ROS 2 Interface (rclcpp)              │     │
│   │ - Node lifecycle                      │     │
│   │ - Publisher / Subscriber management   │     │
│   └───────────────────────────────────────┘     │
│                                                 │
│   ┌─────────────────────────────────────────┐   │
│   │ Perception & Positioning Pipeline       │   │
│   │                                         │   │
│   │ ZED (external) ──► Object Detection     │   │
│   │   (sub: rgb, depth,   (NCNN YOLOv9-s   │   │
│   │    point cloud)        + Deep SORT)     │   │
│   │                           │             │   │
│   │                    ┌──────▼──────┐      │   │
│   │                    │Target Mgr   │      │   │
│   │                    │(sub: eggs,  │      │   │
│   │                    │ IMU, fb)    │      │   │
│   │                    └──────┬──────┘      │   │
│   │                    ┌──────▼──────┐      │   │
│   │                    │micro-ROS    │      │   │
│   │                    │Agent (USB   │      │   │
│   │                    │serial/DDS)  │      │   │
│   │                    └─────────────┘      │   │
│   └─────────────────────────────────────────┘   │
│                                                 │
│   ┌─────────────────────────────────────────┐   │
│   │ Beetle Predator (handheld detection)    │   │
│   │ Rear camera ──► NCNN YOLOv9 + Deep SORT │   │
│   │ + GPS location ──► BeetleDetection msg  │   │
│   └─────────────────────────────────────────┘   │
└──────┬──────────────────────────────────────────┘
       │
       │ Cyclone DDS (UDP multicast discovery + unicast data)
       │
┌─────────▼───────────────────────────────────────────────────┐
│   ROS 2 Network (same domain ID)                            │
│   - Other ROS 2 nodes on host machine                       │
│   - Topics: /<device_id>/sensors/*, /<device_id>/camera/*,  │
│     /scan, /cpb_*, /ESP32_Command, /ESP32_Feedback,         │
│     /cpb_predator/detection                   │
└─────────────────────────────────────────────────────────────┘
```

The native layer cross-compiles ~70 ROS 2 Humble packages via a CMake superbuild. The Kotlin layer communicates with C++ through JNI, constructing Java objects directly (SensorInfo, SensorReading, CameraInfo, Bitmap) to avoid string serialization overhead. Event callbacks notify the UI layer when sensor data or camera frames are available.

**Data sources:**

- **IMU sensors** (accelerometer, gyroscope, magnetometer, barometer, illuminance) - acquired in C++ via `ASensorManager` (NDK), event queue forwarded to ROS controllers
- **GPS** - acquired in Kotlin via `FusedLocationProviderClient` (Google Play Services - required on device), location updates passed to C++ GPS controller via JNI
- **Cameras** - acquired in C++ via Camera2 NDK (`ACameraManager`/`AImageReader`), YUV frames converted to BGR8 in native layer and published as ROS 2 topics; RGBA frames sent to Kotlin via JNI callbacks for live UI preview
- **USB LiDAR** - YDLIDAR connected via USB serial (JNI serial bridge: C++ SDK calls routed through JNI to Kotlin `BufferedUsbSerialPort` wrapping `mik3y/usb-serial-for-android`), scan data published by C++ LiDAR controller
- **Beetle Predator** - rear camera frames (RGBA via `GetLastFrame()`) + GPS location (`GetLastLocation()`) combined with NCNN inference, publishes `vermin_collector_ros_msgs/BeetleDetection` for confirmed Deep SORT tracks

## Published ROS 2 Topics

The app publishes the following topics that can be discovered and consumed by other ROS 2 nodes on the same domain:

**Sensor data:**

- `/<device_id>/sensors/accelerometer` - `sensor_msgs/Imu` - 3-axis acceleration (m/s²)
- `/<device_id>/sensors/gyroscope` - `sensor_msgs/Imu` - 3-axis angular velocity (rad/s)
- `/<device_id>/sensors/magnetometer` - `sensor_msgs/MagneticField` - 3-axis magnetic field (µT)
- `/<device_id>/sensors/barometer` - `sensor_msgs/FluidPressure` - atmospheric pressure (hPa)
- `/<device_id>/sensors/illuminance` - `sensor_msgs/Illuminance` - ambient light (lux)
- `/<device_id>/sensors/gps` - `sensor_msgs/NavSatFix` - GPS location (lat/lon/alt)

**Camera image streams:**

- `/<device_id>/camera/front/image_color` - `sensor_msgs/Image` - front camera raw BGR8 (~1-3 MB/frame)
- `/<device_id>/camera/front/image_color/compressed` - `sensor_msgs/CompressedImage` - front camera JPEG (~50-100 KB/frame)
- `/<device_id>/camera/rear/image_color` - `sensor_msgs/Image` - rear camera raw BGR8 (~1-3 MB/frame)
- `/<device_id>/camera/rear/image_color/compressed` - `sensor_msgs/CompressedImage` - rear camera JPEG (~50-100 KB/frame)

**Camera calibration:**

- `/<device_id>/camera/front/camera_info` - `sensor_msgs/CameraInfo` - front camera intrinsics
- `/<device_id>/camera/rear/camera_info` - `sensor_msgs/CameraInfo` - rear camera intrinsics

**LiDAR:**

- `/scan` - `sensor_msgs/LaserScan` - YDLIDAR scan data (range, angle, intensity)

**Pipeline (perception & positioning) - subscribed (inputs):**

- `/zed/zed_node/rgb/image_rect_color/compressed` - `sensor_msgs/CompressedImage` - ZED RGB image (object detection input)
- `/zed/zed_node/depth/depth_registered` - `sensor_msgs/Image` - ZED depth map (3D localization input)
- `/zed/zed_node/point_cloud/cloud_registered` - `sensor_msgs/PointCloud2` - ZED point cloud (3D localization + crop input)
- `/zed/zed_node/imu/data` - `sensor_msgs/Imu` - ZED IMU (target manager orientation calibration)
- `/cpb_eggs_center` - `geometry_msgs/Point` - egg cluster location (target manager input from object detection)
- `/ESP32_Feedback` - `vermin_collector_ros_msgs/Feedback` - motor state feedback from ESP32 via micro-ROS Agent
- `/pan_tilt_fixed_position` - `std_msgs/Float32MultiArray` - manual pan/tilt override (target manager)
- `/scan_limit` - `std_msgs/Float32` - configurable scan angle limit (target manager)

**Pipeline (perception & positioning) - published (outputs):**

- `/cpb_beetle_center` - `geometry_msgs/Point` - 3D beetle detection center
- `/cpb_beetle` - `sensor_msgs/PointCloud2` - cropped beetle point cloud
- `/cpb_larva_center` - `geometry_msgs/Point` - 3D larva detection center
- `/cpb_larva` - `sensor_msgs/PointCloud2` - cropped larva point cloud
- `/cpb_eggs_center` - `geometry_msgs/Point` - 3D egg detection center
- `/cpb_eggs` - `sensor_msgs/PointCloud2` - cropped egg point cloud
- `/ESP32_Command` - `vermin_collector_ros_msgs/Command` - motor commands to ESP32 via micro-ROS Agent

**Beetle Predator (handheld detection):**

- `/cpb_predator/detection` - `vermin_collector_ros_msgs/BeetleDetection` - geolocated pest detection (GPS + 2D bbox + class label + track ID)

> [!NOTE] > `<device_id>` is configurable in the app's ROS Setup screen and defaults to the device's sanitized name (e.g., `pixel_7`, `galaxy_s23`). This namespace allows multiple Android devices to publish on the same ROS 2 network without topic collisions.

All published messages include a `frame_id` field in the header (e.g., `"<device_id>_camera_front"`, `"<device_id>_imu_link"`) and a timestamp indicating when the data was captured. This allows other ROS 2 nodes to transform the data between coordinate frames and temporally synchronize multiple sensors using ROS 2's TF (Transform) system.

## Perception & Positioning Subsystem

The app includes a complete perception and positioning pipeline for Colorado Potato Beetle (CPB) detection and localization. The pipeline operates as a state machine with sequential dependency progression.

### Pipeline Architecture

```
ZED Camera (External) → Object Detection (Android) → Target Manager → micro-ROS Agent → ESP32
```

**State Machine Flow:**

```
STOPPED → ZED_PROBING → ZED_AVAILABLE → DETECTION_RUNNING →
TARGET_RUNNING → AGENT_RUNNING
```

### Object Detection Node

**Input (subscribed topics from external ZED camera):**

- `/zed/zed_node/rgb/image_rect_color/compressed` - JPEG compressed RGB image
- `/zed/zed_node/depth/depth_registered` - Depth map aligned to RGB
- `/zed/zed_node/point_cloud/cloud_registered` - 3D point cloud

**Processing pipeline:**

1. JPEG decompression via libjpeg-turbo (TurboJPEG)
2. YOLOv9-s detection via NCNN (1280×736 letterbox input)
3. Deep SORT multi-object tracking with MARS ReID features
4. 3D localization by querying point cloud at detection center
5. Point cloud cropping to detection bounding box

**Output (published topics):**

- `/cpb_beetle_center` - `geometry_msgs/Point` - 3D center location of beetle detections
- `/cpb_beetle` - `sensor_msgs/PointCloud2` - Cropped point cloud for beetle
- `/cpb_larva_center` - `geometry_msgs/Point` - 3D center location of larva detections
- `/cpb_larva` - `sensor_msgs/PointCloud2` - Cropped point cloud for larva
- `/cpb_eggs_center` - `geometry_msgs/Point` - 3D center location of egg detections
- `/cpb_eggs` - `sensor_msgs/PointCloud2` - Cropped point cloud for eggs

**Detection classes:**

- `cpb_beetle` (class 0) - Adult Colorado Potato Beetle
- `cpb_larva` (class 1) - CPB larvae
- `cpb_eggs` (class 2) - CPB egg clusters

### Target Manager Node

Selects CPB egg targets for laser engagement, compensating for camera-to-laser physical offsets and device orientation via ZED IMU data. Publishes `ESP32_Command` directly to the micro-ROS Agent, which bridges it to the ESP32 microcontroller.

**Input (subscribed topics):**

- `/cpb_eggs_center` - `geometry_msgs/Point` - 3D egg cluster location from object detection
- `/zed/zed_node/imu/data` - `sensor_msgs/Imu` - ZED camera IMU for orientation calibration
- `/ESP32_Feedback` - `vermin_collector_ros_msgs/Feedback` - state feedback from ESP32 via micro-ROS Agent
- `/pan_tilt_fixed_position` - `std_msgs/Float32MultiArray` - manual override position

**Output (published topics):**

- `/ESP32_Command` - `vermin_collector_ros_msgs/Command` - motor commands sent to ESP32 via micro-ROS Agent

### Dynamic Node Detection

The pipeline supports distributed deployment across multiple Android devices or PCs. Nodes can run locally or be detected as running on other devices via topic probing.

**User workflow:**

1. Navigate to "ROS 2 Subsystem" from dashboard
2. Probe topics → discovers ZED camera
3. Start object detection locally → loads NCNN models, publishes detections
4. Click object detection card → view live statistics (total detections, active tracks, queue size)
5. Downstream nodes become startable as upstream dependencies are satisfied

> [!TIP]
> The object detection node can run on Phone A while target manager runs on Phone B. Topic discovery automatically detects which nodes are running where and enables/disables start buttons accordingly.

## How to Build

You do not need ROS 2 installed on your machine to build the app.

> [!NOTE]
> ROS 2 Humble is needed on a companion machine to interact with the published topics. Follow [these instructions to install ROS Humble](https://docs.ros.org/en/humble/Installation.html).

### Computer Setup

Install all required system dependencies:

```bash
sudo apt install -y \
  git unzip cmake build-essential \
  openjdk-21-jdk \
  adb android-sdk-platform-tools-common \
  python3-vcstool python3-catkin-pkg-modules python3-empy python3-lark-parser
```

Download the [Android SDK "Command-line tools only" version](https://developer.android.com/studio#command-tools).

```bash
mkdir ~/android-sdk
cd ~/android-sdk
unzip ~/Downloads/commandlinetools-linux-8512546_latest.zip
```

```bash
./cmdline-tools/bin/sdkmanager --sdk_root=$HOME/android-sdk "build-tools;33.0.2" "build-tools;34.0.0" "platforms;android-33" "platforms;android-34" "ndk;26.3.11579264" "cmake;3.22.1"
```

Create `local.properties` in the repo root pointing to your SDK:

```bash
echo "sdk.dir=$HOME/android-sdk" > local.properties
```

You may need to do additional setup to use adb.
Follow the [Set up a device for development](https://developer.android.com/studio/run/device#setting-up) instructions if you're using Ubuntu.

### Create Debug Keys

```bash
mkdir -p ~/.android
keytool -genkey -v -keystore ~/.android/debug.keystore -alias adb_debug_key -keyalg RSA -keysize 2048 -validity 10000 -storepass android -keypass android
```

### Clone and Initialize

```bash
git clone https://github.com/ros2-bachelor-thesis/ros2_android.git
cd ros2_android
git submodule init
git submodule update
```

### Model Assets Setup

The object detection perception system requires NCNN model files. These are included in the repository under `app/src/main/assets/models/`:

**Required files:**

- `yolov9_s_pobed.ncnn.param` - YOLOv9-s detection model parameters
- `yolov9_s_pobed.ncnn.bin` - YOLOv9-s detection model weights
- `osnet_ain_x1_0.ncnn.param` - MARS ReID model parameters for tracking
- `osnet_ain_x1_0.ncnn.bin` - MARS ReID model weights for tracking

- OSNet-AIN: https://kaiyangzhou.github.io/deep-person-reid/MODEL_ZOO
- POBED-Yolov9: https://phabricator.ict.tuwien.ac.at/source/Vermin_Collector_ROS2_3D_Object_Detection/

  **Model specifications:**

- **Detection model**: YOLOv9-s trained on Colorado Potato Beetle dataset (3 classes: `cpb_beetle`, `cpb_larva`, `cpb_eggs`)
- **Input size**: 1280×736 (letterbox resize with padding)
- **ReID model**: OSNet AIN x1.0 for Deep SORT multi-object tracking
- **Feature dimension**: 128-D appearance features

> [!NOTE]
> The perception pipeline subscribes to external ZED camera topics (`/zed/zed_node/rgb/image_rect_color/compressed`, `/zed/zed_node/depth/depth_registered`, `/zed/zed_node/point_cloud/cloud_registered`) and publishes 3D-localized detections. The ZED camera should be running on a separate machine on the same ROS 2 network.

### Patch files in `android_patches`

They are quite important as they apply changes to dependencies fetched via the vcstool and which are located in ros.repos file. Applying happens on the `make deps` step.

### Download ROS Dependencies

Fetch git submodules and ROS 2 source dependencies:

```bash
make deps
```

This initializes git submodules and uses [vcstool](https://github.com/dirk-thomas/vcstool) to download ~70 ROS 2 packages into `deps/`.

### Build

The build system uses a Makefile with two stages: CMake cross-compiles the native libraries, then Gradle compiles Kotlin and produces the APK.

**Build everything (native + APK):**

```bash
make all
```

**Or build stages separately:**

```bash
# Build native libraries only (CMake cross-compilation)
make native

# Build APK only (requires native libraries already built)
make app
```

**Build variants:**

```bash
# Build with debug symbols (no optimization)
make debug

# Build optimized (no debug symbols)
make release
```

The native libraries are staged to `build/jniLibs/arm64-v8a/` and the APK is produced at `app/build/outputs/apk/debug/app-debug.apk`.

### Install and Run

**Install APK to connected device:**

```bash
make install
```

**Build, install, and launch app:**

```bash
make run
```

## Debug

### Logging

**View app logs (recommended):**

```bash
make logcat
```

This clears the log buffer and tails logs filtered to the app.

**View all device logs:**

```bash
adb logcat -v color
```

**Save logs to file:**

```bash
adb logcat -v time > logcat.txt
```

### Cleaning Builds

**Clean everything:**

```bash
make clean
```

**Clean only app build (keeps native libs):**

```bash
make clean-app
```

**Clean native build (forces full rebuild):**

```bash
make clean-native
```

### Quick ROS 2 Network Setup

Use the provided script to configure your host machine for ROS 2 discovery with the Android device:

```bash
# Get Android device IP
adb shell ip addr show wlan0 | grep "inet "

# Run setup script (configures firewall and ROS 2 daemon)
./scripts/setup_ros2_network.sh <android_ip> <domain_id>

# Export domain ID in current terminal
export ROS_DOMAIN_ID=<domain_id>

# Verify topics are visible
ros2 topic list
```

**Example:**

```bash
./scripts/setup_ros2_network.sh 192.168.0.100 1
export ROS_DOMAIN_ID=1
ros2 topic list
```

> [!NOTE]
> The domain ID must match the setting in the Android app (default: 1). For detailed testing instructions and troubleshooting, see the test READMEs in `scripts/tests/`.

## Testing complete ROS 2 Subsystem locally without external devices

See `Quick ROS2 Network setup` section before starting this.

Start ROS inside the ros2_android app with the same `ROS_DOMAIN_ID` setting you used in the network setup and choose the correct Network Interface so you are in the same network as your computer.

### Step 1: Replay Ros bag with ZED 2i Camera topics

```bash
ros2 bag play .
```

Publish explicit topic from rosbag:

```bash
ros2 bag play . --topic /zed/zed_node/rgb/image_rect_color/compressed
```

#### Step 1.1 (Optional)

Play imu data if not available inside the ros bag.

```bash
ros2 topic pub -r 10 /zed/zed_node/imu/data sensor_msgs/msg/Imu \
  "{header: {frame_id: 'imu'}, orientation: {x: -0.7071068, y: 0.0, z: 0.0, w: 0.7071068}}"
```

### Step 2: Probe Topic to verify topics are being published

### Step 3: Start the Object Detection node

#### Step 3.1: (Optional) Start playing Ros bag or keep it paused

### Step 4: Start Target manager

### Step 5: Micro-ROS Agent steps to cheese target manager and to be able to test the whole pipeline

#### Step 5.1 - Connect ESP32 flashed with [pan_and_tilt_zephyr_app](https://github.com/alex120400/pan_and_tilt_zephyr_app.git)

#### Step 5.2 - Start micro-ROS Agent in the ROS2_ANDROID App (Need to have all other ros2 nodes running aswell to advance to be able to start the agent)

#### Step 5.3 - Send fake imu data

```bash
opros2 topic pub -r 10 /zed/zed_node/imu/data sensor_msgs/msg/Imu \
  "{header: {frame_id: 'imu'}, orientation: {x: -0.7071068, y: 0.0, z: 0.0, w: 0.7071068}}"
```

#### Step 5.4 - cheese the state transition of the esp32

Stop the micro-ROS Agent unplug the esp32 from the phone to wipe its memory, replug it and start the micro-ROS Agent. Now we have the EPS32 and Target Manager in a state where we can send commands to it and test the ROS 2 Subsystem without having any real hardware connecte like motor steppers.

---

### Testing with scripts

The Android app publishes sensor and camera data as ROS 2 topics that can be consumed by nodes running on a host machine (Linux/macOS). This enables visualization, logging, and integration with the full ROS 2 ecosystem.

For complete testing instructions, including:

- Automated network setup script
- Interactive sensor testing framework with visualizers
- Manual testing procedures
- Troubleshooting common issues

See the test READMEs in `scripts/tests/`: **[sensors](scripts/tests/sensors/README.md)**, **[object detection](scripts/tests/object_detection/README.md)**
