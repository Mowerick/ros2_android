package com.github.mowerick.ros2.android.model

enum class Severity { WARNING, ERROR }

data class NativeNotification(
    val id: Long,
    val message: String,
    val severity: Severity,
    val timestampMs: Long
)
