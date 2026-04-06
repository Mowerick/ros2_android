package com.github.mowerick.ros2.android.ui.components

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Radar
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.unit.dp
import com.github.mowerick.ros2.android.model.NodeState
import com.github.mowerick.ros2.android.model.PipelineNode

@Composable
fun PipelineNodeCard(
    node: PipelineNode,
    canStart: Boolean,
    canProbe: Boolean,
    onStartStop: () -> Unit,
    onClick: () -> Unit,
    onProbe: (() -> Unit)? = null,
    isProbing: Boolean = false,
    runningLocally: Boolean = false,
    detectedOnNetwork: Boolean = false
) {
    val isDisabled = !canStart && !canProbe && !runningLocally && !detectedOnNetwork

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(enabled = !isDisabled, onClick = onClick)
            .alpha(if (isDisabled) 0.5f else 1f),
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp),
        colors = if (node.isExternal) CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant
        ) else CardDefaults.cardColors()
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            // Header: name + state chip
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    if (node.isExternal) {
                        Text(
                            text = "External Hardware",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    } else if (runningLocally) {
                        Text(
                            text = "Running Locally",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.primary
                        )
                    } else if (detectedOnNetwork) {
                        Text(
                            text = "Running on Network",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.tertiary
                        )
                    }
                    Text(
                        text = node.name,
                        style = MaterialTheme.typography.titleMedium
                    )
                }
                val displayState = if (runningLocally || detectedOnNetwork) NodeState.Running else NodeState.Stopped
                NodeStateChip(state = displayState)
            }

            // Pub/Sub labels
            node.subscribesTo.forEach { topic ->
                Text(
                    text = "SUB: ${topic.name}",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.primary
                )
            }
            node.publishesTo.forEach { topic ->
                Text(
                    text = "PUB: ${topic.name}",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.tertiary
                )
            }

            // Probe button - shown for all nodes, disabled if FSM doesn't allow it
            if (onProbe != null) {
                OutlinedButton(onClick = onProbe, enabled = canProbe) {
                    if (isProbing) {
                        val transition = rememberInfiniteTransition(label = "probe")
                        val rotation by transition.animateFloat(
                            initialValue = 0f,
                            targetValue = 360f,
                            animationSpec = infiniteRepeatable(
                                animation = tween(1500, easing = LinearEasing),
                                repeatMode = RepeatMode.Restart
                            ),
                            label = "radar_rotation"
                        )
                        val pulse by transition.animateFloat(
                            initialValue = 0.5f,
                            targetValue = 1f,
                            animationSpec = infiniteRepeatable(
                                animation = tween(750, easing = LinearEasing),
                                repeatMode = RepeatMode.Reverse
                            ),
                            label = "radar_pulse"
                        )
                        Icon(
                            Icons.Filled.Radar,
                            contentDescription = null,
                            modifier = Modifier
                                .size(18.dp)
                                .graphicsLayer { rotationZ = rotation }
                                .alpha(pulse)
                        )
                    } else {
                        Icon(
                            Icons.Filled.Radar,
                            contentDescription = null,
                            modifier = Modifier.size(18.dp)
                        )
                    }
                    Text(
                        if (isProbing) "  Stop Probing" else "  Probe Topics",
                        style = MaterialTheme.typography.labelMedium
                    )
                }
            }

            // Start/Stop button - non-external nodes only
            if (!node.isExternal) {
                when {
                    detectedOnNetwork -> {
                        Text(
                            text = "Running on another device",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.tertiary
                        )
                    }
                    runningLocally -> {
                        OutlinedButton(onClick = onStartStop) {
                            Text("Stop")
                        }
                    }
                    else -> {
                        Button(
                            onClick = onStartStop,
                            enabled = canStart
                        ) {
                            Text(
                                if (canStart) "Start"
                                else "Waiting for upstream"
                            )
                        }
                    }
                }
            }
        }
    }
}
