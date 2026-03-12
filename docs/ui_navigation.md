# UI Navigation & Component Architecture

Documentation of the app's screen hierarchy, reusable components, and interaction patterns for the Jetpack Compose UI.

### Dashboard Hub Navigation

**Why**: The original app had a flat 4-screen flow (DomainIdScreen -> SensorListScreen -> SensorDetailScreen/CameraDetailScreen) that mixed all functionality into a single undifferentiated sensor list. The thesis requires distinct sections for (a) built-in sensors (from the original sensors_for_ros), (b) the ROS 2 Perception & Positioning pipeline with node-by-node control, and (c) future configuration (DDS-Security, etc.). A flat list cannot represent these different concerns.
**Approach**: Restructured navigation into a Dashboard hub pattern. After ROS initialization, the user lands on a `DashboardScreen` with section cards. Each section card navigates into its own sub-flow. The `Screen` sealed class in `RosViewModel` models the full navigation hierarchy, and `navigateBack()` handles the correct back-navigation for each level.
**Alternatives**: Considered a bottom navigation bar with tabs, but rejected because the sections have very different interaction patterns (sensor list vs. pipeline tree) and bottom nav implies peer screens, not a hierarchy. Also considered keeping DomainIdScreen as a mandatory first screen, but moved it behind settings because domain ID selection is a one-time setup action that shouldn't block dashboard access on subsequent launches.
**Limitations**: Navigation state lives entirely in the ViewModel's `Screen` sealed class - there is no `NavHost` or `Navigation` component. This means deep linking and system back button handling depend on the ViewModel's `navigateBack()` logic. The approach is simpler than Jetpack Navigation but would need rework if the navigation graph grows significantly.
**Issues encountered**: Title deduplication - the initial Dashboard showed "ROS 2 Android" in both the TopAppBar and a title card, resolved by changing the TopAppBar title to "Dashboard (ID: xx)". The `ExposedDropdownMenu` for network interface selection required `menuAnchor()` modifier on the `TextField` to position correctly.
**References**: Material Design 3 navigation patterns, Jetpack Compose `Scaffold` documentation.

## Navigation Hierarchy

```text
DashboardScreen (main hub, shows ROS Domain ID in title)
    |
    +-- [gear icon] --> RosSetupScreen (Domain ID + network interface)
    |
    +-- "Built-in Sensors" --> BuiltInSensorsScreen
    |       +-- sensor row --> SensorDetailScreen
    |       +-- camera row --> CameraDetailScreen
    |
    +-- "ROS 2 Subsystem" --> SubsystemScreen (pipeline tree)
            +-- node card --> NodeDetailScreen
```

Back navigation (`RosViewModel.navigateBack()`):

- SensorDetailScreen / CameraDetailScreen -> BuiltInSensorsScreen
- BuiltInSensorsScreen / SubsystemScreen / RosSetupScreen -> DashboardScreen
- NodeDetailScreen -> SubsystemScreen

## Screens

### DashboardScreen

- **File**: `ui/screens/DashboardScreen.kt`
- **Role**: Main landing screen after app launch.
- **TopAppBar**: Title shows "Dashboard (ID: xx)" when ROS is running, "Dashboard (ID: --)" otherwise. A settings gear icon navigates to `RosSetupScreen`.
- **Setup banner**: When ROS is not configured, an error-colored (`errorContainer`) card with a warning icon prompts "ROS not configured" and offers an "Open Settings" button.
- **Section cards**: Two `SectionCard` components for "Built-in Sensors" (icon: Sensors) and "ROS 2 Subsystem" (icon: AccountTree). Both are disabled (greyed out) until ROS is started.
- **State**: `rosStarted`, `rosDomainId`, `sensorCount`, `cameraCount`.

### RosSetupScreen

- **File**: `ui/screens/RosSetupScreen.kt`
- **Role**: Replaces the old `DomainIdScreen`. One-time ROS configuration.
- **TopAppBar**: "ROS Settings" with back arrow.
- **Content**: Status card (when ROS already running), Domain ID card with `NumericKeypad`, Network Interface card with `ExposedDropdownMenu` (auto-selects first `wlan*` interface), and a Start/Restart ROS button.
- **State**: Local `domainId` and `selectedInterface` state. Calls `vm.startRos(domainId, networkInterface)` which initializes the native ROS 2 layer via `NativeBridge.nativeStartRos()`.

### BuiltInSensorsScreen

- **File**: `ui/screens/BuiltInSensorsScreen.kt`
- **Role**: Lists device IMU sensors and cameras with toggle switches. Renamed from `SensorListScreen`.
- **TopAppBar**: "Built-in Sensors" with back arrow (added because it is no longer the root screen).
- **Content**: `LazyColumn` with two sections - "Sensors" (each row shows `prettyName`, `topicName`, and a `Switch` to enable/disable publishing) and "Cameras" (each row shows `name`, enabled status, and a `Switch`). Tapping a row navigates to the respective detail screen.

### SubsystemScreen

- **File**: `ui/screens/SubsystemScreen.kt`
- **Role**: Visualizes the ROS 2 Perception & Positioning pipeline as a vertical top-to-bottom tree.
- **TopAppBar**: "ROS 2 Subsystem" with back arrow.
- **Content**: `LazyColumn` of `PipelineNodeCard` components separated by `TopicConnector` arrows. Each connector shows the topic name flowing between nodes. Tapping a node card navigates to `NodeDetailScreen`.
- **State**: `nodes` (list of `PipelineNode`), `isProbing` (whether DDS topic discovery is active), `isNodeStartable` callback, `onToggleProbing` callback.

### NodeDetailScreen

- **File**: `ui/screens/NodeDetailScreen.kt`
- **Role**: Detail view for a single pipeline node.
- **TopAppBar**: Node name with back arrow.
- **Content**: State chip + Start/Stop button (disabled if upstream not running), external node badge (for nodes running on Jetson/PC), description card, and SUB/PUB `TopicInfoCard` sections.
- **State**: `node` (`PipelineNode`), `canStart` (from `isNodeStartable`).

### SensorDetailScreen

- **File**: `ui/screens/SensorDetailScreen.kt`
- **Role**: Shows live sensor readings polled at 10 Hz via `NativeBridge.nativeGetSensorData()`.
- **Unchanged** from original sensors_for_ros implementation.

### CameraDetailScreen

- **File**: `ui/screens/CameraDetailScreen.kt`
- **Role**: Shows camera info and enable/disable controls.
- **Unchanged** from original sensors_for_ros implementation.

## Permission & Settings Flows

### GPS Location Permission and Settings Flow

**Why**: The GPS sensor (which publishes to `/gps` as `sensor_msgs/msg/NavSatFix`) requires both Android location permission (`ACCESS_FINE_LOCATION`) and location services to be enabled in system settings. Unlike other sensors, GPS cannot simply be enabled directly - it requires user interaction with system permission dialogs and settings screens. The UI must handle the complete flow including user acceptance or rejection at each step.

**Approach**: Implemented a multi-step permission/settings flow with proper state tracking:

1. **User enables GPS sensor** in `BuiltInSensorsScreen` by toggling the switch
2. **Native callback fires**: `GpsController::Enable()` calls JNI callback `NativeBridge.onGpsEnable()`
3. **ViewModel tracks request**: `RosViewModel` stores `pendingGpsSensorId = "gps_location_provider"` to track which sensor initiated the request
4. **GpsManager orchestrates checks**: `GpsManager.startWithChecks()` runs permission and settings checks sequentially:
   - **Permission check**: If not granted, triggers `MainActivity.requestLocationPermissionLauncher`
   - **Settings check**: If granted but settings disabled, triggers `MainActivity.locationSettingsLauncher` with Google Play Services `ResolvableApiException`
5. **User makes decision**: User sees system dialog(s) and can accept or decline
6. **Result handling**:
   - **Permission granted**: `MainActivity` calls `RosViewModel.onLocationPermissionGranted()` which proceeds to settings check
   - **Permission denied**: `MainActivity` calls `RosViewModel.onLocationPermissionDenied()` which:
     * Shows warning notification: "GPS: Location permission required"
     * Disables the GPS sensor via `NativeBridge.nativeDisableSensor(pendingGpsSensorId)`
     * Refreshes sensor list so UI reflects disabled state
     * Clears `pendingGpsSensorId`
   - **Settings enabled**: `MainActivity` calls `RosViewModel.onLocationSettingsEnabled()` which starts GPS updates via `GpsManager.startWithChecks()`
   - **Settings declined**: `MainActivity` calls `RosViewModel.onLocationSettingsCancelled()` which:
     * Shows warning notification: "GPS: Location services required"
     * Disables the GPS sensor via `NativeBridge.nativeDisableSensor(pendingGpsSensorId)`
     * Refreshes sensor list so UI reflects disabled state
     * Clears `pendingGpsSensorId`

**Key Implementation Details**:
- `pendingGpsSensorId` field in `RosViewModel` (line 89) tracks the GPS sensor ID between enable callback and permission/settings result
- Set when `onGpsEnable` callback fires (line 119)
- Cleared when settings enabled (line 195), GPS manually disabled (line 127), or user declines (lines 208, 359)
- Both denial paths call `NativeBridge.nativeDisableSensor(pendingGpsSensorId)` to ensure UI consistency

**Alternatives**:
- **Optimistic enablement**: Could leave sensor enabled and show error notifications when GPS fails to start. Rejected because it creates UI inconsistency - sensor appears enabled but doesn't publish data.
- **Automatic retry**: Could automatically re-prompt user if they decline. Rejected to avoid annoying permission spam - user's decision is respected immediately.
- **Global GPS state flag**: Could use boolean flag instead of tracking sensor ID. Rejected because future implementations might have multiple GPS-dependent features requiring independent tracking.

**Limitations**:
- Currently assumes only one GPS sensor (`"gps_location_provider"`) - if multiple GPS sources are added, would need set/map structure for `pendingGpsSensorId`
- If user enables GPS, declines permission, then re-enables GPS within milliseconds, race condition possible (unlikely in practice)
- No "Don't ask again" handling - if user permanently denies permission, they must manually enable in system settings

**User Experience**:
- GPS sensor toggle in `BuiltInSensorsScreen` provides immediate feedback:
  * Toggle switches ON → permission/settings dialogs appear
  * User declines → toggle automatically switches back OFF with warning notification
  * User accepts → toggle stays ON and GPS starts publishing
- Prevents confusing state where sensor appears enabled but silently fails to publish
- Warning notifications explain why GPS didn't enable ("permission required" vs "location services required")

**Issues Encountered**:
- **Initial bug**: `NativeBridge.nativeEnableSensor()` was called before permission checks, but sensor wasn't disabled when user declined dialogs. This caused GPS sensor to appear enabled in UI even though it couldn't publish data.
- **Root cause**: Permission/settings dialog launchers are asynchronous - `launcher.launch()` returns immediately without waiting for user decision. The result arrives later via `MainActivity` activity result callbacks.
- **Solution**: Track pending enable request with `pendingGpsSensorId` and disable sensor in both decline paths (`onLocationPermissionDenied` and `onLocationSettingsCancelled`).

**Files Modified**:
- `ros2_android/app/src/main/kotlin/com/github/mowerick/ros2/android/viewmodel/RosViewModel.kt` - Added `pendingGpsSensorId` tracking and denial handlers (lines 89, 119, 127, 195, 202-208, 350-361)
- `ros2_android/app/src/main/kotlin/com/github/mowerick/ros2/android/MainActivity.kt` - Added `onLocationPermissionDenied()` callback (line 51)
- `ros2_android/app/src/main/kotlin/com/github/mowerick/ros2/android/GpsManager.kt` - Already implements `startWithChecks()` with permission/settings validation

**References**:
- Android location permission flow: https://developer.android.com/training/location/permissions
- Google Play Services `LocationSettingsRequest`: https://developers.google.com/android/reference/com/google/android/gms/location/LocationSettingsRequest
- ActivityResultContracts documentation: https://developer.android.com/reference/androidx/activity/result/contract/ActivityResultContracts

## Reusable Components

### SectionCard

- **File**: `ui/components/SectionCard.kt`
- **Props**: `icon: ImageVector`, `title: String`, `subtitle: String`, `enabled: Boolean`, `onClick: () -> Unit`
- **Role**: Large clickable card for the Dashboard. Row layout with icon on the left, title and subtitle on the right. Disabled state reduces alpha and removes click handler.

### PipelineNodeCard

- **File**: `ui/components/PipelineNodeCard.kt`
- **Props**: `node: PipelineNode`, `canStart: Boolean`, `onStartStop: () -> Unit`, `onClick: () -> Unit`, `onProbe: (() -> Unit)?`, `isProbing: Boolean`
- **Role**: Represents a pipeline node in the SubsystemScreen tree. Shows node name, `NodeStateChip`, PUB/SUB topic labels (primary color for SUB, tertiary for PUB). External nodes use `surfaceVariant` background with an "External (Jetson/PC)" label and a "Probe Topic" / "Stop Probing" toggle button. Internal nodes show a Start button (disabled with "Waiting for upstream" text if upstream not running) or Stop button depending on state.

### NodeStateChip

- **File**: `ui/components/NodeStateChip.kt`
- **Props**: `state: NodeState`
- **Role**: Colored chip reflecting node lifecycle state. Grey for Stopped, amber for Starting, green for Running, red for Error. Uses filled circle icon with tinted background.

### TopicConnector

- **File**: `ui/components/TopicConnector.kt`
- **Props**: `topicName: String`
- **Role**: Visual connector between pipeline nodes in the SubsystemScreen. Renders a vertical line (Canvas), a centered topic name label, and a downward arrow icon (`KeyboardArrowDown`).

### TopicInfoCard

- **File**: `ui/components/TopicInfoCard.kt`
- **Props**: `label: String`, `topicName: String`, `topicType: String`
- **Role**: Displays a ROS 2 topic's name and message type in NodeDetailScreen. Label is "SUB" or "PUB".

### NumericKeypad

- **File**: `ui/components/NumericKeypad.kt`
- **Role**: Grid of digit buttons (0-9) with a clear button for Domain ID entry. Used in RosSetupScreen.

## Data Models

### Screen (sealed class)

- **File**: `viewmodel/RosViewModel.kt`
- Variants: `Dashboard`, `RosSetup`, `BuiltInSensors`, `Subsystem`, `SensorDetail(sensor)`, `CameraDetail(camera)`, `NodeDetail(node)`

### PipelineNode

- **File**: `model/PipelineNode.kt`
- Fields: `id`, `name`, `description`, `state: NodeState`, `subscribesTo: List<TopicInfo>`, `publishesTo: List<TopicInfo>`, `upstreamNodeId: String?`, `isExternal: Boolean`

### NodeState

- **File**: `model/NodeState.kt`
- Enum: `Stopped`, `Starting`, `Running`, `Error`

### TopicInfo

- **File**: `model/TopicInfo.kt`
- Fields: `name: String`, `type: String`

## Pipeline Dependency Enforcement

**Why**: The 4-node Perception & Positioning pipeline has strict sequential dependencies: YOLO needs stereo image data from the ZED node, laser positioning needs object coordinates from YOLO, and the micro-ROS Agent needs stepper commands from laser positioning. Starting a downstream node without its upstream running would be meaningless.
**Approach**: Each `PipelineNode` has an `upstreamNodeId` field identifying its dependency. `isNodeStartable(nodeId)` checks that the upstream node's state is `Running` before enabling the Start button. When stopping a node, `toggleNodeState()` cascades the stop to all downstream dependents by iteratively finding nodes whose `upstreamNodeId` is in the set of nodes being stopped.

The ZED stereo node is marked `isExternal = true` because it runs on an NVIDIA Jetson/PC, not on Android. External nodes cannot be started/stopped from the app. Instead, they offer a "Probe Topic" toggle button that starts/stops DDS topic discovery polling (every 2 seconds via `rclcpp::Node::get_topic_names_and_types()`). When the external node's published topics appear on the DDS network, its state automatically changes to `Running`, which unblocks downstream nodes. When topics disappear, it cascades a stop to all dependents.

Pipeline node definitions (hardcoded in `RosViewModel.createDefaultPipelineNodes()`):

1. `zed_stereo_node` (external) - PUB: `/stereo_image_data` (sensor_msgs/msg/Image)
2. `yolo_obj_detect` - SUB: `/stereo_image_data`, PUB: `/object_xyz_pos` (geometry_msgs/msg/PointStamped)
3. `laser_positioning` - SUB: `/object_xyz_pos`, PUB: `/stepper_steps` (std_msgs/msg/Int32MultiArray)
4. `micro_ros_agent` - SUB: `/stepper_steps`

**Alternatives**: Considered automatic probing (start topic discovery when entering the SubsystemScreen), but manual control via a toggle button avoids unnecessary network traffic.
**Limitations**: Node start/stop currently only toggles UI state - no actual ROS 2 node lifecycle management happens yet. The nodes are placeholders for future implementation. The dependency chain is linear (single upstream per node); a DAG would require a more complex dependency resolution algorithm.
**Issues encountered**: `nativeGetDiscoveredTopics()` JNI function caused `UnsatisfiedLinkError` when the native library hadn't been rebuilt. The catch block needed to catch `UnsatisfiedLinkError` (which extends `Error`, not `Exception`) to prevent the app from crashing. The catch also stops the polling loop since there is no point retrying without a library rebuild.
**References**: ROS 2 topic discovery API (`rclcpp::Node::get_topic_names_and_types()`), JNI `UnsatisfiedLinkError` documentation.
