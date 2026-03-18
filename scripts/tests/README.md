# ROS 2 Android Sensor Testing Framework

Interactive Python tool for visualizing and testing sensor data published by the ros2_android application.

## Features

- **Auto-discovery**: Automatically finds available sensor topics from the Android app
- **Interactive menu**: Select which sensor to test from a user-friendly CLI
- **Browser-based visualization**: Real-time plots render in your web browser (WebAgg backend)
- **Custom visualizations**: Tailored visualization for each sensor type
- **Validation**: Built-in validation checks for expected sensor ranges
- **Automatic cleanup**: Properly manages visualization state between sensor tests

## Supported Sensors

### IMU Sensors
- **Accelerometer** (`/sensors/accelerometer`) - 3D acceleration with gravity validation
- **Gyroscope** (`/sensors/gyroscope`) - Angular velocity with rotation detection
- **Magnetometer** (`/sensors/magnetometer`) - Compass with magnetic field visualization

### Environmental Sensors
- **Barometer** (`/sensors/barometer`) - Pressure and altitude estimation
- **Illuminance** (`/sensors/illuminance`) - Light sensor with animated brightness circle

### Positioning
- **GPS** (`/sensors/gps`) - Interactive Folium map with accuracy circle

### Camera
- **Camera Image** (`camera/<id>/image_color`) - Raw/compressed image via rqt_image_view
- **Camera Info** (`camera/<id>/camera_info`) - Calibration data display

## Prerequisites

### 1. ROS 2 Humble

Make sure ROS 2 Humble is installed:

```bash
# Check if ROS 2 is installed
ros2 --version

# If not installed, install ROS 2 Humble Desktop
sudo apt update
sudo apt install ros-humble-desktop
```

### 2. Source ROS 2 Environment

```bash
source /opt/ros/humble/setup.bash
```

Add this to your `~/.bashrc` to make it permanent:

```bash
echo "source /opt/ros/humble/setup.bash" >> ~/.bashrc
```

### 3. Install rqt_image_view (for camera visualization)

```bash
sudo apt install ros-humble-rqt-image-view
```

## Installation

### 1. Navigate to the test scripts directory

```bash
cd scripts/tests
```

### 2. Install Python dependencies

```bash
pip install -r requirements.txt
```

Or using a virtual environment (recommended):

```bash
# Create virtual environment
python3 -m venv venv

# Activate it
source venv/bin/activate

# Install dependencies
pip install -r requirements.txt
```

> [!NOTE]
> If using a virtual environment, you need to activate it every time before running the script.

### 3. Make the script executable

```bash
chmod +x sensor_test.py
```

## Usage

### 1. Start the ros2_android Application

Make sure your Android device is running the ros2_android app and is connected to the same network as your testing machine. The app should be publishing sensor data.

### 2. Verify ROS 2 connectivity

Check that you can see the sensor topics:

```bash
ros2 topic list
```

You should see topics like `/sensors/accelerometer`, `/sensors/gps`, etc.

### 3. Run the Testing Framework

```bash
./sensor_test.py
```

Or:

```bash
python3 sensor_test.py
```

### 4. Interactive Menu

The script will:
1. Auto-discover available sensor topics
2. Display an interactive menu
3. Allow you to select which sensor to visualize
4. Launch the appropriate visualization tool
5. Return to the menu when you close the visualization

### 5. Navigation

- Use **arrow keys** to navigate the menu
- Press **Enter** to select
- Press **Ctrl+C** to exit at any time
- Close visualization windows to return to the menu

> [!NOTE]
> Most visualizations open in your default web browser (WebAgg backend). Close the browser tab to return to the menu. The camera visualization uses a separate rqt_image_view window.

## Sensor-Specific Test Procedures

### Accelerometer
1. Place phone flat on table → Z-axis reads ~9.8 m/s²
2. Rotate phone → gravity shifts between axes
3. Shake phone → magnitude spikes above 9.8 m/s²

**Expected**: Magnitude √(x² + y² + z²) ≈ 9.8 m/s² when stationary

---

### Gyroscope
1. Keep phone stationary → all axes ~0 rad/s
2. Rotate around X-axis (roll) → red line spikes
3. Rotate around Y-axis (pitch) → green line spikes
4. Rotate around Z-axis (yaw) → blue line spikes

**Expected**: Near-zero when stationary, spikes during rotation

---

### Magnetometer
1. Place phone flat → compass arrow points north
2. Rotate phone horizontally → heading changes 0-360°
3. Verify magnitude is 25-65 µT (Earth's magnetic field)
4. Move near metal/magnets → field changes

**Expected**: Heading tracks north, magnitude 25-65 µT

---

### Barometer
1. Note baseline pressure at current height
2. Move phone up/down stairs → pressure changes ~12 Pa/meter
3. Verify altitude estimation is reasonable

**Expected**: 950-1050 hPa at sea level, decreases with altitude

---

### Illuminance (Light Sensor)
1. Cover sensor with hand → circle darkens (1-10 lx)
2. Normal indoor lighting → medium brightness (100-500 lx)
3. Point at bright light → circle brightens (1000+ lx)
4. Go outside → maximum brightness (10,000-100,000 lx)

**Expected**: Smooth logarithmic transitions, no negative values

---

### GPS
1. Go outdoors for better GPS fix
2. Wait for accuracy < 20m (green marker)
3. Walk around → marker updates on map
4. Check accuracy circle matches position uncertainty

**Expected**: Accuracy < 20m outdoors, position updates as you move

---

### Camera
1. Image topics → Opens rqt_image_view window
2. Verify image quality and frame rate
3. Camera info → Displays calibration matrix

**Expected**: Clear images at 15-30 Hz

## Troubleshooting

### "No sensor topics found!"

**Problem**: The script cannot find any sensor topics.

**Solutions**:
- Ensure ros2_android app is running on the device
- Check network connectivity between device and test machine
- Verify topics exist: `ros2 topic list`
- Check DDS domain ID matches (default is 0)

---

### "rqt_image_view not found"

**Problem**: Camera visualization requires rqt_image_view.

**Solution**:
```bash
sudo apt install ros-humble-rqt-image-view
```

---

### GPS shows "No GPS fix yet"

**Problem**: GPS sensor has no satellite lock.

**Solutions**:
- Move device outdoors
- Wait 30-60 seconds for initial fix
- Ensure location permissions are granted in Android app
- Check that GPS is enabled in Android settings

---

### Import errors for rclpy

**Problem**: ROS 2 Python libraries not found.

**Solutions**:
```bash
# Source ROS 2 environment
source /opt/ros/humble/setup.bash

# If using virtual environment, install with system site packages
python3 -m venv --system-site-packages venv
```

---

### Visualization windows freeze or don't update

**Problem**: matplotlib or ROS 2 event loop issues.

**Solutions**:
- Close and restart the visualization
- Try a different sensor
- Check CPU usage - may be too slow for real-time plotting
- Reduce plot update frequency in the visualizer code

---

### Browser doesn't open or shows "Connection refused"

**Problem**: WebAgg server failed to start or port is already in use.

**Solutions**:
- Check if port 8988 is available: `netstat -tuln | grep 8988`
- Kill any process using the port: `lsof -ti:8988 | xargs kill -9`
- Manually open the URL shown in the terminal (usually http://127.0.0.1:8988/)
- Check your default browser is set correctly
- Try a different browser if the default one has issues

---

### "IOLoop is already running" error

**Problem**: Previous visualization didn't clean up properly.

**Solution**:
- Exit the script completely (Ctrl+C)
- Restart the script - cleanup runs automatically on startup
- If issue persists, restart your terminal session

---

### Permission denied when running script

**Solution**:
```bash
chmod +x sensor_test.py
```

## Architecture

```
scripts/tests/
├── sensor_test.py              # Main interactive script
├── visualizers/                # Visualization modules
│   ├── __init__.py
│   ├── matplotlib_config.py    # Shared matplotlib WebAgg configuration and cleanup
│   ├── accelerometer.py        # 3D acceleration plot
│   ├── gyroscope.py            # Angular velocity plot
│   ├── magnetometer.py         # Compass + field plot
│   ├── barometer.py            # Pressure/altitude plot
│   ├── illuminance.py          # Brightness circle animation
│   ├── gps.py                  # Folium interactive map
│   └── camera.py               # rqt_image_view launcher
├── requirements.txt            # Python dependencies
└── README.md                   # This file
```

### Visualization Backend

All matplotlib-based visualizers (accelerometer, gyroscope, magnetometer, barometer, illuminance) use the **WebAgg** backend, which renders plots in your default web browser. This provides:

- **Cross-platform compatibility**: Works on Linux, macOS, and Windows
- **Better interactivity**: Smooth animations and responsive UI
- **No X11 required**: No need for X server or display configuration
- **Automatic cleanup**: Proper server shutdown between visualizations

The framework automatically:
1. Configures WebAgg with a clean toolbar-free interface
2. Opens the visualization in your default browser
3. Cleans up the WebAgg server when you close the browser tab
4. Resets all state before launching the next visualization

## Development

### Adding a New Sensor Visualizer

1. Create a new file in `visualizers/<sensor_name>.py`
2. Implement a class following this pattern:

```python
from visualizers.matplotlib_config import configure_matplotlib
import matplotlib.pyplot as plt
import rclpy
from rclpy.node import Node

class MySensorVisualizer:
    def __init__(self, topic_name):
        rclpy.init()
        self.node = Node('my_sensor_visualizer')
        # Set up subscriber, data buffers, etc.

    def run(self):
        # Configure matplotlib BEFORE creating figures
        configure_matplotlib()

        # Create figure and subplots
        fig, ax = plt.subplots()

        # Set up animation
        from matplotlib.animation import FuncAnimation
        ani = FuncAnimation(fig, self.update_plot, interval=50)

        # Show plot (blocks until window closed)
        plt.show()

        # Cleanup
        self.node.destroy_node()
        rclpy.shutdown()

    def update_plot(self, frame):
        # Update plot with new data
        rclpy.spin_once(self.node, timeout_sec=0)
        # ... plotting code ...
```

3. Add import to `visualizers/__init__.py`
4. Update `sensor_test.py`:
   - Add category to `_categorize_topic()`
   - Add label to `format_topic_choice()`
   - Add case to `launch_visualizer()`

> [!IMPORTANT]
> Always call `configure_matplotlib()` AFTER importing pyplot but BEFORE creating any figures. The cleanup is handled automatically by the framework.

### Customizing Visualizations

Each visualizer is self-contained and can be customized:
- Adjust plot update rates via `FuncAnimation(..., interval=X)` (default: 50ms)
- Modify plot styles, colors, and layouts
- Change validation thresholds for your specific use case
- Adjust WebAgg port in `matplotlib_config.py` if needed (default: 8988)

### Cleanup Mechanism

The framework uses a centralized cleanup system (`matplotlib_config.cleanup_visualization()`):

1. **Automatic cleanup**: Runs before each new visualization in `sensor_test.py:135`
2. **Manual cleanup**: Called in the `finally` block if visualization fails
3. **Cleanup order**:
   - Destroys all figure managers (closes WebSocket connections)
   - Stops the tornado IOLoop if running
   - Resets WebAggApplication state
   - Clears IOLoop instance
   - Closes any remaining figures

This ensures each visualization starts with a clean WebAgg server state, preventing port conflicts and "IOLoop already running" errors.

## Credits

Part of the Bachelor Thesis: "Deploying ROS 2 Humble Perception & Positioning subsystem on Android ARM devices"

## License

See project root LICENSE file.
