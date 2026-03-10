package com.github.mowerick.ros2.android.ui.components

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.unit.dp
import com.github.mowerick.ros2.android.model.NativeNotification
import com.github.mowerick.ros2.android.model.Severity
import kotlinx.coroutines.delay

@Composable
fun NotificationOverlay(
    notifications: List<NativeNotification>,
    onDismiss: (Long) -> Unit,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier.fillMaxSize(),
        contentAlignment = Alignment.TopCenter
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 24.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            notifications.takeLast(3).forEach { notification ->
                AnimatedVisibility(
                    visible = true,
                    enter = fadeIn() + slideInVertically { -it },
                    exit = fadeOut() + slideOutVertically { -it }
                ) {
                    FadingNotificationCard(
                        notification = notification,
                        onDismiss = { onDismiss(notification.id) }
                    )
                }
            }
        }
    }
}

@Composable
private fun FadingNotificationCard(
    notification: NativeNotification,
    onDismiss: () -> Unit
) {
    // Fade from 1.0 to 0.0 over 2 seconds, starting 3 seconds after creation
    val fadeStartMs = 3000L
    val fadeDurationMs = 2000L
    val frameInterval = 50L

    var cardAlpha by remember(notification.id) { mutableFloatStateOf(1f) }

    LaunchedEffect(notification.id) {
        val elapsed = System.currentTimeMillis() - notification.timestampMs
        val remaining = fadeStartMs - elapsed
        if (remaining > 0) delay(remaining)

        val fadeStart = System.currentTimeMillis()
        while (cardAlpha > 0f) {
            val fadeElapsed = System.currentTimeMillis() - fadeStart
            cardAlpha = (1f - fadeElapsed.toFloat() / fadeDurationMs).coerceIn(0f, 1f)
            if (cardAlpha > 0f) delay(frameInterval)
        }
    }

    NotificationCard(
        notification = notification,
        onDismiss = onDismiss,
        modifier = Modifier.alpha(cardAlpha)
    )
}

@Composable
private fun NotificationCard(
    notification: NativeNotification,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier
) {
    val containerColor = when (notification.severity) {
        Severity.ERROR -> MaterialTheme.colorScheme.errorContainer
        Severity.WARNING -> MaterialTheme.colorScheme.tertiaryContainer
    }
    val contentColor = when (notification.severity) {
        Severity.ERROR -> MaterialTheme.colorScheme.onErrorContainer
        Severity.WARNING -> MaterialTheme.colorScheme.onTertiaryContainer
    }

    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = containerColor,
            contentColor = contentColor
        )
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Icon(
                imageVector = Icons.Default.Warning,
                contentDescription = notification.severity.name,
                modifier = Modifier.size(20.dp)
            )
            Text(
                text = notification.message,
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.weight(1f)
            )
            IconButton(onClick = onDismiss, modifier = Modifier.size(24.dp)) {
                Icon(
                    imageVector = Icons.Default.Close,
                    contentDescription = "Dismiss",
                    modifier = Modifier.size(16.dp)
                )
            }
        }
    }
}
