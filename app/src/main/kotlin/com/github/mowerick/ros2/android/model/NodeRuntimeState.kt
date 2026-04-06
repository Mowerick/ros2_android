package com.github.mowerick.ros2.android.model

/**
 * Tracks the runtime state of a pipeline node.
 *
 * A node can be running in two ways:
 * - Locally: Started by this Android device
 * - On Network: Detected via topic discovery (running on another device)
 *
 * This separation enables distributed pipeline deployments across multiple devices.
 */
data class NodeRuntimeState(
    /**
     * True if this node was started locally on this Android device.
     * Enables Stop button and local control.
     */
    val runningLocally: Boolean = false,

    /**
     * True if this node's published topics were discovered on the network.
     * Indicates another device is running this node.
     * Disables Start button (can't start what's already running elsewhere).
     */
    val detectedOnNetwork: Boolean = false,

    /**
     * True if this node is actively probing for topics on the network.
     */
    val isProbing: Boolean = false,

    /**
     * True while native initialization is in progress (between start click and native call return).
     */
    val isStarting: Boolean = false
) {
    /**
     * True if node is running either locally or detected externally
     */
    val isRunning: Boolean
        get() = runningLocally || detectedOnNetwork

    /**
     * True if node is fully stopped (not local, not external)
     */
    val isStopped: Boolean
        get() = !isRunning
}
