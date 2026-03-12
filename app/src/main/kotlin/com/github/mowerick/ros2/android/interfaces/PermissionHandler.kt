package com.github.mowerick.ros2.android.interfaces

import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.IntentSenderRequest

/**
 * Interface for handling location permission requests and checks.
 * Abstracts permission-related operations from the ViewModel layer.
 */
interface PermissionHandler {
    /**
     * Request location permission from the user.
     * This will trigger the system permission dialog.
     */
    fun requestLocationPermission()

    /**
     * Check if location permission has been granted.
     * @return true if permission is granted, false otherwise
     */
    fun hasLocationPermission(): Boolean

    /**
     * Get the launcher for location settings resolution.
     * @return launcher if available, null if activity is not ready
     */
    fun getLocationSettingsLauncher(): ActivityResultLauncher<IntentSenderRequest>?
}
