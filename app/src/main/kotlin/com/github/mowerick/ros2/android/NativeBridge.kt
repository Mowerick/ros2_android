package com.github.mowerick.ros2.android

object NativeBridge {
    init {
        System.loadLibrary("android-ros")
    }

    private var notificationCallback: ((String, String) -> Unit)? = null

    external fun nativeInit(cacheDir: String, packageName: String)
    external fun nativeDestroy()
    external fun nativeSetNetworkInterfaces(interfaces: Array<String>)
    external fun nativeOnPermissionResult(permission: String, granted: Boolean)

    external fun nativeStartRos(domainId: Int, networkInterface: String)
    external fun nativeStopRos()
    external fun nativeGetSensorList(): String
    external fun nativeGetSensorData(uniqueId: String): String
    external fun nativeGetCameraList(): String
    external fun nativeEnableCamera(uniqueId: String)
    external fun nativeDisableCamera(uniqueId: String)
    external fun nativeEnableSensor(uniqueId: String)
    external fun nativeDisableSensor(uniqueId: String)
    external fun nativeGetNetworkInterfaces(): String
    external fun nativeGetDiscoveredTopics(): String
    external fun nativeGetCameraFrame(uniqueId: String): ByteArray?
    external fun nativeGetPendingNotifications(): String
    external fun nativeSetNotificationCallback()

    fun setNotificationCallback(callback: (severity: String, message: String) -> Unit) {
        notificationCallback = callback
        nativeSetNotificationCallback()
    }

    // Called from native code (JNI)
    @Suppress("unused")
    @JvmStatic
    private fun onNotification(severity: String, message: String) {
        notificationCallback?.invoke(severity, message)
    }
}
