# Target Manager Testing

Test the `TargetManagerController` state machine without a physical ESP32 or micro-ROS agent.
The mock ESP32 node simulates firmware responses, allowing the full INIT → READY boot sequence
to be exercised over DDS on a desktop machine.

## Prerequisites

- ROS 2 Humble sourced (`source /opt/ros/humble/setup.bash`)
- `vermin_collector_ros_msgs` installed or on `AMENT_PREFIX_PATH`
- Android device running the ros2_android app on the same DDS domain (same multicast network)
- Target Manager enabled in the app UI

## Topics

| Topic                    | Direction           | Type                                 | Description                     |
| ------------------------ | ------------------- | ------------------------------------ | ------------------------------- |
| `/ESP32_Command`         | Android → mock      | `vermin_collector_ros_msgs/Command`  | Commands sent by Target Manager |
| `/ESP32_Feedback`        | mock → Android      | `vermin_collector_ros_msgs/Feedback` | Simulated ESP32 state           |
| `/zed/zed_node/imu/data` | publisher → Android | `sensor_msgs/Imu`                    | IMU for calibration             |
| `/cpb_eggs_center`       | publisher → Android | `geometry_msgs/Point`                | Detection target                |

## Test Sequence

Open four terminals, each sourced with ROS 2 Humble.

### Terminal 1 - Mock ESP32 firmware

```bash
cd ros2_android/scripts/tests/targeting
python3 mock_esp32_node.py
```

Expected output as Android boots:

```
Received HARD_HOMING: ...
  -> MOVING for 2.0s
  -> READY (steps=[0, 0, 0])
Received SETUP: ...
  -> SETUP echoed: freq=[1000, 1000, 1000] en=[0, 0, 0] res=8
Received SETUP: ...
  -> SETUP echoed: freq=[1000, 1000, 1000] en=[1, 1, 1] res=8
Received TARGET: ...   (calibration iterations)
Received SOFT_HOMING: ...
  -> MOVING for 1.5s
  -> READY (steps=[0, 0, 0])
```

### Terminal 2 - Level IMU (calibration converges immediately)

The quaternion below produces `gravity_in_camera() = (0, -1, 0)` which gives
`tilt_err = 0°` and `roll_err = 0°` - both below the 0.3° deadband.
Calibration completes in a single tick.

```bash
ros2 topic pub -r 10 /zed/zed_node/imu/data sensor_msgs/msg/Imu \
  "{header: {frame_id: 'imu'}, orientation: {x: -0.7071068, y: 0.0, z: 0.0, w: 0.7071068}}"
```

> [!NOTE]
> The identity quaternion `{x:0, y:0, z:0, w:1}` gives `gravity=(0,0,1)` → `tilt_err=90°`.
> `{x:0, y:-0.7071, z:0, w:0.7071}` gives `gravity=(1,0,0)` → `roll_err=90°`.
> Only `{x:-0.7071, y:0, z:0, w:0.7071}` gives `gravity=(0,-1,0)` → both errors=0°.

### Terminal 3 - Monitor commands from Android

```bash
ros2 topic echo /ESP32_Command
```

Verify that:

- `HARD_HOMING` (command_type=4) is sent first
- `SETUP` (command_type=0) with `en_motors=[0,0,0]` follows, then again with `en_motors=[1,1,1]` after 5s
- `TARGET` commands arrive during calibration (incremental step corrections)
- `SOFT_HOMING` (command_type=3) is sent when calibration completes

### Terminal 4 - Send a detection target (after logcat shows READY)

```bash
# Target at 1m forward, 1m left, 1m up - 45° up and left of laser
ros2 topic pub --once /cpb_eggs_center geometry_msgs/msg/Point \
  "{x: 0.0, y: 1.0, z: 1.0}"
```

## Expected Logcat Progression

```bash
make logcat | grep -iE "INIT|HARDHOME|SETUP_PHASE|CALIBRAT|SOFTHOME|READY|TARGET|Sent"
```

```
INIT -> HARDHOME
HARDHOME -> SETUP_PHASE
SETUP_PHASE -> CALIBRATING
CALIBRATING complete (err=0.000°) -> SOFTHOME
SOFTHOME -> READY
Sent TARGET: tilt=45.00° roll=0.00° res=8 steps=(...) star=...
EXECUTING -> READY (return-to-zero pending)
```

## Boot Timing (approximate with mock)

| Phase                     | Duration                                               |
| ------------------------- | ------------------------------------------------------ |
| INIT → HARDHOME           | instant (first Feedback received)                      |
| HARDHOME → SETUP_PHASE    | ~2.0s (mock HARD_HOMING move)                          |
| SETUP_PHASE → CALIBRATING | ~0.5s (motor gate: 5s from Enable; echoed immediately) |
| CALIBRATING → SOFTHOME    | 1 tick (100ms) with level IMU                          |
| SOFTHOME → READY          | ~1.5s (mock SOFT_HOMING move)                          |
| **Total**                 | **~9s** (dominated by motor enable gate)               |

> [!NOTE]
> The motor enable gate (5s safety delay) means SETUP_PHASE may take up to 5 seconds
> before re-issuing SETUP with `en_motors=[1,1,1]` and advancing to CALIBRATING.

## Testing Calibration Convergence

To test the calibration loop with non-zero error (arm needs to physically adjust):

```bash
# ~10° tilt error: gravity=(0, -0.9848, 0.1736)
# Derived by rotating {x:-0.7071,y:0,z:0,w:0.7071} by +10° around the roll axis
ros2 topic pub -r 10 /zed/zed_node/imu/data sensor_msgs/msg/Imu \
  "{header: {frame_id: 'imu'}, orientation: {x: -0.6645, y: 0.0, z: -0.0872, w: 0.7424}}"
```

Logcat should show resolution progressing 8 → 32 → 64 as the error decreases,
and multiple calibration TARGET commands before SOFTHOME is sent.

## Fixed Position Mode

After READY, the fixed position override can be tested:

```bash
# Move to tilt=20°, pan=10° (Float32MultiArray: [tilt, pan])
ros2 topic pub --once /pan_tilt_fixed_position std_msgs/msg/Float32MultiArray \
  "{data: [20.0, 10.0]}"
```
