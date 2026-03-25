package com.github.mowerick.ros2.android

import android.content.Context
import android.util.Log
import com.github.mowerick.ros2.android.model.ExternalDeviceInfo
import com.github.mowerick.ros2.android.model.ExternalDeviceType

/**
 * Manages TTY device detection and permission setup for YDLIDAR devices on rooted Android.
 *
 * This manager:
 * - Detects YDLIDAR devices via native TTY enumeration (sysfs VID/PID matching)
 * - Sets TTY device permissions (chmod 666) via root access
 * - Converts native TTY device info to ExternalDeviceInfo for UI display
 *
 * Requirements:
 * - Rooted Android device
 * - Root access granted to the app
 * - YDLIDAR connected via USB (appears as /dev/ttyUSB* or /dev/ttyACM*)
 *
 * Usage:
 * ```
 * val ttyManager = TtyDeviceManager(context, rootManager)
 * val devices = ttyManager.detectLidarDevices()
 * if (devices.isNotEmpty()) {
 *     ttyManager.prepareTtyDevice(devices[0].usbPath)
 * }
 * ```
 */
class TtyDeviceManager(
    private val context: Context,
    private val rootManager: RootPermissionManager
) {

    /**
     * Detect YDLIDAR devices connected to the system
     *
     * This method calls native code to enumerate /dev/ttyUSB* and /dev/ttyACM* devices
     * and filters them by VID/PID to identify known YDLIDAR USB-to-serial chips:
     * - CP210x: 0x10c4:0xea60
     * - CH340:  0x1a86:0x7523
     *
     * @return List of detected YDLIDAR devices (empty if none found)
     *
     * Note: This method does NOT require root access for enumeration.
     *       However, accessing the devices for R/W requires root (see [prepareTtyDevice]).
     */
    fun detectLidarDevices(): List<ExternalDeviceInfo> {
        return try {
            Log.i(TAG, "Detecting YDLIDAR TTY devices...")
            val nativeDevices = NativeBridge.nativeDetectTtyDevices()

            if (nativeDevices.isEmpty()) {
                Log.w(TAG, "No YDLIDAR devices found")
            } else {
                Log.i(TAG, "Found ${nativeDevices.size} YDLIDAR device(s)")
                nativeDevices.forEach { device ->
                    Log.d(TAG, "  - ${device.name} at ${device.usbPath} (${device.vendorId.toString(16)}:${device.productId.toString(16)})")
                }
            }

            nativeDevices.toList()
        } catch (e: Exception) {
            Log.e(TAG, "Failed to detect YDLIDAR devices: ${e.message}")
            emptyList()
        }
    }

    /**
     * Prepare TTY device for LIDAR SDK access
     *
     * This method:
     * 1. Verifies root access is available
     * 2. Sets TTY device permissions (chmod 666) via root
     * 3. Optionally tests if device is accessible
     *
     * @param ttyPath TTY device path (e.g., "/dev/ttyUSB0")
     * @return true if device is ready for SDK access, false otherwise
     *
     * Note: This method REQUIRES root access. Call [RootPermissionManager.hasRootAccess] first.
     */
    fun prepareTtyDevice(ttyPath: String): Boolean {
        Log.i(TAG, "Preparing TTY device: $ttyPath")

        // Verify root access
        if (!rootManager.hasRootAccess()) {
            Log.e(TAG, "Root access required to prepare TTY devices")
            return false
        }

        // Set permissions
        if (!rootManager.setTtyPermissions(ttyPath)) {
            Log.e(TAG, "Failed to set permissions on $ttyPath")
            return false
        }

        // Test accessibility (optional diagnostic)
        val accessible = NativeBridge.nativeCanAccessTty(ttyPath)
        if (!accessible) {
            Log.w(TAG, "TTY device $ttyPath not accessible after chmod (SELinux may still block)")
            // Note: On some devices, SELinux may still block access even with correct permissions
            // The LIDAR SDK will fail during initialization if this is the case
        } else {
            Log.i(TAG, "TTY device $ttyPath is accessible")
        }

        return true
    }

    /**
     * Check if a TTY device can be accessed for reading/writing
     *
     * This is a diagnostic utility that attempts to open the TTY device
     * with O_RDWR | O_NOCTTY flags. It does NOT perform any I/O operations.
     *
     * @param ttyPath TTY device path to test
     * @return true if device can be opened, false otherwise
     *
     * Note: This method is called automatically by [prepareTtyDevice] for diagnostics.
     */
    fun canAccessTtyDevice(ttyPath: String): Boolean {
        return try {
            NativeBridge.nativeCanAccessTty(ttyPath)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to test TTY access for $ttyPath: ${e.message}")
            false
        }
    }

    companion object {
        private const val TAG = "TtyDeviceManager"
    }
}
