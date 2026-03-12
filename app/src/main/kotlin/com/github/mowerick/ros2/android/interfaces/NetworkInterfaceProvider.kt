package com.github.mowerick.ros2.android.interfaces

/**
 * Interface for querying network interfaces.
 * Abstracts network interface discovery from the ViewModel layer.
 */
interface NetworkInterfaceProvider {
    /**
     * Query available network interfaces on the device.
     * Filters out loopback, point-to-point, and virtual interfaces.
     * Only returns interfaces that are up and support multicast.
     *
     * @return array of network interface names (e.g., ["wlan0", "eth0"])
     */
    fun queryNetworkInterfaces(): Array<String>
}
