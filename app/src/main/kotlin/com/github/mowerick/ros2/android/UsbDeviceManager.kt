package com.github.mowerick.ros2.android

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import android.os.Build
import android.util.Log
import com.github.mowerick.ros2.android.model.ExternalDeviceInfo
import com.github.mowerick.ros2.android.model.ExternalDeviceType
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.driver.UsbSerialProber
import com.hoho.android.usbserial.util.SerialInputOutputManager
import java.io.IOException

class UsbDeviceManager(private val context: Context) : SerialInputOutputManager.Listener {

    private val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
    private var permissionCallback: ((Boolean) -> Unit)? = null

    // USB serial infrastructure
    private var serialPort: UsbSerialPort? = null
    private var serialManager: SerialInputOutputManager? = null
    private var currentDeviceId: String? = null

    private val permissionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action == ACTION_USB_PERMISSION) {
                synchronized(this) {
                    val device = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    } else {
                        @Suppress("DEPRECATION")
                        intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
                    }

                    val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)

                    if (device != null) {
                        Log.i(TAG, "USB permission result for ${device.deviceName}: $granted")
                        permissionCallback?.invoke(granted)
                        permissionCallback = null
                    }
                }
            }
        }
    }

    init {
        // Register permission receiver
        val filter = IntentFilter(ACTION_USB_PERMISSION)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.registerReceiver(permissionReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            context.registerReceiver(permissionReceiver, filter)
        }
    }

    /**
     * Detect YDLIDAR devices connected to the system
     * Common YDLIDAR USB-to-Serial chips: CP210x (0x10c4:0xea60), CH340 (0x1a86:0x7523)
     */
    fun detectLidarDevices(): List<UsbDevice> {
        val deviceList = usbManager.deviceList
        return deviceList.values.filter { device ->
            // Check for common USB-to-Serial chips used by YDLIDAR
            (device.vendorId == 0x10c4 && device.productId == 0xea60) ||  // CP210x
            (device.vendorId == 0x1a86 && device.productId == 0x7523)     // CH340
        }
    }

    /**
     * Convert UsbDevice to ExternalDeviceInfo for UI display
     */
    fun deviceToInfo(device: UsbDevice, connected: Boolean = false, enabled: Boolean = false): ExternalDeviceInfo {
        val deviceName = when {
            device.vendorId == 0x10c4 && device.productId == 0xea60 -> "YDLIDAR (CP210x)"
            device.vendorId == 0x1a86 && device.productId == 0x7523 -> "YDLIDAR (CH340)"
            else -> "YDLIDAR"
        }

        return ExternalDeviceInfo(
            uniqueId = "ydlidar_${device.deviceName.replace("/", "_")}",
            name = deviceName,
            deviceType = ExternalDeviceType.LIDAR,
            usbPath = device.deviceName,
            vendorId = device.vendorId,
            productId = device.productId,
            topicName = "/scan",
            topicType = "sensor_msgs/msg/LaserScan",
            connected = connected,
            enabled = enabled
        )
    }

    /**
     * Request USB permission for a device
     * Callback is invoked with permission result
     */
    fun requestPermission(device: UsbDevice, callback: (Boolean) -> Unit) {
        if (usbManager.hasPermission(device)) {
            callback(true)
            return
        }

        permissionCallback = callback

        val permissionIntent = PendingIntent.getBroadcast(
            context,
            0,
            Intent(ACTION_USB_PERMISSION),
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                PendingIntent.FLAG_MUTABLE
            } else {
                0
            }
        )

        usbManager.requestPermission(device, permissionIntent)
        Log.i(TAG, "Requesting USB permission for ${device.deviceName}")
    }

    // SerialInputOutputManager.Listener interface - LIDAR→SDK data path
    override fun onNewData(data: ByteArray) {
        currentDeviceId?.let { deviceId ->
            try {
                nativeWriteToPtyMaster(deviceId, data)
            } catch (e: Exception) {
                Log.e(TAG, "Failed to forward LIDAR data to PTY: ${e.message}")
            }
        }
    }

    override fun onRunError(e: Exception) {
        Log.e(TAG, "Serial port error: ${e.message}")
        closeSerialPort()
    }

    /**
     * Open serial port for LIDAR device (called after USB permission granted)
     * @param device USB device to open
     * @param uniqueId Device identifier for PTY bridge
     * @return true if successful, false otherwise
     */
    fun openSerialPort(device: UsbDevice, uniqueId: String): Boolean {
        try {
            // Close existing connection if any
            closeSerialPort()

            // Find USB serial driver
            val drivers = UsbSerialProber.getDefaultProber().findAllDrivers(usbManager)
            val driver = drivers.firstOrNull { it.device == device }
            if (driver == null) {
                Log.e(TAG, "No USB serial driver found for ${device.deviceName}")
                return false
            }

            // Open connection and get first port
            val connection = usbManager.openDevice(device)
            if (connection == null) {
                Log.e(TAG, "Failed to open USB device ${device.deviceName}")
                return false
            }

            val port = driver.ports[0]
            port.open(connection)

            // Configure serial parameters: 230400 baud, 8N1 (no parity, 1 stop bit)
            port.setParameters(
                230400,  // baud rate
                8,       // data bits
                UsbSerialPort.STOPBITS_1,  // stop bits
                UsbSerialPort.PARITY_NONE  // parity
            )

            // Create SerialInputOutputManager for async I/O
            val manager = SerialInputOutputManager(port, this)
            manager.start()

            // Store state
            serialPort = port
            serialManager = manager
            currentDeviceId = uniqueId

            Log.i(TAG, "Opened serial port for $uniqueId: 230400 baud 8N1")
            return true

        } catch (e: IOException) {
            Log.e(TAG, "Failed to open serial port: ${e.message}")
            closeSerialPort()
            return false
        }
    }

    /**
     * Write data to physical USB LIDAR (called from C++ PtyReadThread via JNI)
     * SDK→LIDAR command path
     */
    fun writeToPhysicalUsb(data: ByteArray) {
        try {
            serialPort?.write(data, 1000)  // 1 second timeout
        } catch (e: IOException) {
            Log.e(TAG, "Failed to write to USB: ${e.message}")
        }
    }

    /**
     * Close serial port and release resources
     */
    fun closeSerialPort() {
        serialManager?.stop()
        serialManager = null

        try {
            serialPort?.close()
        } catch (e: IOException) {
            // Ignore close errors
        }
        serialPort = null
        currentDeviceId = null

        Log.i(TAG, "Serial port closed")
    }

    /**
     * Check if manager has permission for device
     */
    fun hasPermission(device: UsbDevice): Boolean {
        return usbManager.hasPermission(device)
    }

    /**
     * Cleanup receiver and serial port on destroy
     */
    fun destroy() {
        closeSerialPort()
        try {
            context.unregisterReceiver(permissionReceiver)
        } catch (e: Exception) {
            // Already unregistered
        }
    }

    /**
     * JNI bridge to forward LIDAR data to C++ PTY master
     */
    private external fun nativeWriteToPtyMaster(uniqueId: String, data: ByteArray)

    companion object {
        private const val TAG = "UsbDeviceManager"
        const val ACTION_USB_PERMISSION = "com.github.mowerick.ros2.android.USB_PERMISSION"
    }
}
