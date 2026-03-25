package com.github.mowerick.ros2.android

import android.content.Context
import android.util.Log
import java.util.concurrent.TimeUnit

/**
 * Manages root access verification and TTY device permission setup for rooted Android devices.
 *
 * This class provides utilities to:
 * - Check if the device has root access (su binary available)
 * - Execute commands with root privileges via `su`
 * - Set TTY device permissions (chmod 666) for LIDAR access
 *
 * Requirements:
 * - Rooted Android device
 * - Root management app (Magisk, SuperSU, etc.)
 * - User approval of root access for this app
 *
 * Usage:
 * ```
 * val rootManager = RootPermissionManager(context)
 * if (rootManager.hasRootAccess()) {
 *     rootManager.setTtyPermissions("/dev/ttyUSB0")
 * }
 * ```
 */
class RootPermissionManager(private val context: Context) {

    /**
     * Check if device has root access
     *
     * This method executes `su -c 'id -u'` to verify root access.
     * The root user always has UID 0.
     *
     * On first call, the root management app (Magisk/SuperSU) will prompt
     * the user to grant root access to this app.
     *
     * @return true if root access is available and granted, false otherwise
     */
    fun hasRootAccess(): Boolean {
        return try {
            Log.i(TAG, "Checking root access...")

            // Execute su command to check UID
            val process = Runtime.getRuntime().exec(arrayOf("su", "-c", "id -u"))

            // Wait for process to complete (with timeout)
            val completed = process.waitFor(3, TimeUnit.SECONDS)
            if (!completed) {
                Log.w(TAG, "Root check timed out after 3 seconds")
                process.destroy()
                return false
            }

            // Check exit code first
            val exitCode = process.exitValue()
            Log.d(TAG, "Process exit code: $exitCode")

            // Read output
            val output = process.inputStream.bufferedReader().use { it.readText().trim() }
            val errorOutput = process.errorStream.bufferedReader().use { it.readText().trim() }

            Log.d(TAG, "Output: '$output'")
            if (errorOutput.isNotEmpty()) {
                Log.d(TAG, "Error output: '$errorOutput'")
            }

            // Check if we got UID 0 and exit code 0
            val hasRoot = exitCode == 0 && output == "0"
            if (hasRoot) {
                Log.i(TAG, "Root access confirmed (UID=0)")
            } else {
                Log.w(TAG, "Root access denied or not available (UID='$output', exit=$exitCode)")
            }

            hasRoot
        } catch (e: Exception) {
            Log.e(TAG, "Root check failed: ${e.message}", e)
            false
        }
    }

    /**
     * Set TTY device permissions for LIDAR access
     *
     * Executes `chmod 666 <ttyPath>` with root privileges to make the TTY device
     * readable and writable by all users. This is required because Android apps
     * normally cannot access USB serial devices without USB Host API workarounds.
     *
     * @param ttyPath TTY device path (e.g., "/dev/ttyUSB0")
     * @return true if permissions were set successfully, false otherwise
     *
     * Note: This method does NOT prompt for root access. Call [hasRootAccess] first.
     */
    fun setTtyPermissions(ttyPath: String): Boolean {
        return try {
            Log.i(TAG, "Setting permissions on $ttyPath...")

            // Execute chmod command
            val process = Runtime.getRuntime().exec(arrayOf("su", "-c", "chmod 666 $ttyPath"))

            val completed = process.waitFor(3, TimeUnit.SECONDS)
            if (!completed) {
                Log.e(TAG, "chmod command timed out for $ttyPath")
                process.destroy()
                return false
            }

            val exitCode = process.exitValue()
            if (exitCode != 0) {
                val error = process.errorStream.bufferedReader().use { it.readText() }
                Log.e(TAG, "chmod failed for $ttyPath (exit=$exitCode): $error")
                return false
            }

            // Verify permissions were set
            val verifyProcess = Runtime.getRuntime().exec(arrayOf("su", "-c", "ls -l $ttyPath"))
            verifyProcess.waitFor(2, TimeUnit.SECONDS)
            val permissions = verifyProcess.inputStream.bufferedReader().use { it.readText().trim() }
            Log.i(TAG, "TTY permissions set successfully: $permissions")

            true
        } catch (e: Exception) {
            Log.e(TAG, "Failed to set TTY permissions for $ttyPath: ${e.message}", e)
            false
        }
    }

    /**
     * Execute a shell command with root privileges
     *
     * Generic utility for executing commands via `su -c`. Use this for custom
     * root operations beyond TTY permission management.
     *
     * @param command Shell command to execute (will be executed as: su -c '$command')
     * @return Pair of (stdout, stderr) if successful, null otherwise
     */
    fun executeRootCommand(command: String): Pair<String, String>? {
        return try {
            Log.d(TAG, "Executing root command: $command")
            val process = Runtime.getRuntime().exec(arrayOf("su", "-c", command))

            val completed = process.waitFor(5, TimeUnit.SECONDS)
            if (!completed) {
                Log.w(TAG, "Root command timed out: $command")
                process.destroy()
                return null
            }

            val stdout = process.inputStream.bufferedReader().readText()
            val stderr = process.errorStream.bufferedReader().readText()

            if (process.exitValue() != 0) {
                Log.w(TAG, "Root command failed (exit=${process.exitValue()}): $stderr")
            }

            Pair(stdout, stderr)
        } catch (e: Exception) {
            Log.e(TAG, "Root command execution failed: ${e.message}")
            null
        }
    }

    /**
     * Set SELinux to permissive mode (for USB FD access)
     *
     * On rooted devices with SELinux enforcing, USB file descriptor operations
     * may be blocked. Setting SELinux to permissive allows these operations.
     *
     * Warning: This reduces system security. Only use for development/testing.
     *
     * @return true if SELinux mode was changed, false otherwise
     */
    fun setSELinuxPermissive(): Boolean {
        return try {
            Log.i(TAG, "Setting SELinux to permissive mode...")
            val process = Runtime.getRuntime().exec(arrayOf("su", "-c", "setenforce 0"))
            val completed = process.waitFor(2, TimeUnit.SECONDS)

            if (!completed || process.exitValue() != 0) {
                Log.e(TAG, "Failed to set SELinux permissive")
                return false
            }

            // Verify mode
            val checkProcess = Runtime.getRuntime().exec(arrayOf("su", "-c", "getenforce"))
            checkProcess.waitFor(2, TimeUnit.SECONDS)
            val mode = checkProcess.inputStream.bufferedReader().use { it.readText().trim() }

            val success = mode.equals("Permissive", ignoreCase = true)
            if (success) {
                Log.i(TAG, "SELinux set to permissive mode")
            } else {
                Log.w(TAG, "SELinux mode is: $mode")
            }

            success
        } catch (e: Exception) {
            Log.e(TAG, "Failed to set SELinux permissive: ${e.message}", e)
            false
        }
    }

    /**
     * Get current SELinux mode
     *
     * @return "Enforcing", "Permissive", or null if unable to determine
     */
    fun getSELinuxMode(): String? {
        return try {
            val process = Runtime.getRuntime().exec(arrayOf("su", "-c", "getenforce"))
            process.waitFor(2, TimeUnit.SECONDS)
            process.inputStream.bufferedReader().use { it.readText().trim() }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to get SELinux mode: ${e.message}", e)
            null
        }
    }

    companion object {
        private const val TAG = "RootPermissionManager"
    }
}
