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
import androidx.compose.material.icons.filled.SwapVert
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedCard
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
fun CommandBridgeCard(
    armCommander: PipelineNode,
    microRosAgent: PipelineNode,
    onClick: () -> Unit,
    onStartStopArm: () -> Unit,
    onStartStopAgent: () -> Unit,
    onStartBoth: () -> Unit,
    onProbeArmCommander: () -> Unit,
    onProbeMicroRosAgent: () -> Unit,
    canProbeArmCommander: Boolean,
    canProbeMicroRosAgent: Boolean,
    isProbingArmCommander: Boolean,
    isProbingMicroRosAgent: Boolean,
    armRunningLocally: Boolean,
    armDetectedOnNetwork: Boolean,
    agentRunningLocally: Boolean,
    agentDetectedOnNetwork: Boolean,
    canStartArm: Boolean,
    canStartAgent: Boolean,
    isStartingArm: Boolean,
    isStartingAgent: Boolean
) {
    val anyRunningLocally = armRunningLocally || agentRunningLocally
    val anyRunning = anyRunningLocally || armDetectedOnNetwork || agentDetectedOnNetwork
    val bothOnNetwork = armDetectedOnNetwork && agentDetectedOnNetwork
    val combinedState = if (anyRunning) NodeState.Running else NodeState.Stopped

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick),
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            // Group header
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "Command Bridge",
                    style = MaterialTheme.typography.titleMedium
                )
                NodeStateChip(state = combinedState)
            }

            // Arm Commander inner card
            InnerNodeCard(
                node = armCommander,
                runningLocally = armRunningLocally,
                detectedOnNetwork = armDetectedOnNetwork,
                canProbe = canProbeArmCommander,
                isProbing = isProbingArmCommander,
                onProbe = onProbeArmCommander,
                canStart = canStartArm,
                isStarting = isStartingArm,
                onStartStop = onStartStopArm
            )

            // Bidirectional arrow
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.Center
            ) {
                Icon(
                    Icons.Filled.SwapVert,
                    contentDescription = "Bidirectional data flow",
                    modifier = Modifier.size(24.dp),
                    tint = MaterialTheme.colorScheme.outline
                )
            }

            // micro-ROS Agent inner card
            InnerNodeCard(
                node = microRosAgent,
                runningLocally = agentRunningLocally,
                detectedOnNetwork = agentDetectedOnNetwork,
                canProbe = canProbeMicroRosAgent,
                isProbing = isProbingMicroRosAgent,
                onProbe = onProbeMicroRosAgent,
                canStart = canStartAgent,
                isStarting = isStartingAgent,
                onStartStop = onStartStopAgent
            )

            // Shared Start Both / Stop Both button (always visible, disabled when not applicable)
            val bothRunningLocally = armRunningLocally && agentRunningLocally
            val neitherRunning = !armRunningLocally && !armDetectedOnNetwork && !agentRunningLocally && !agentDetectedOnNetwork
            val canStartEither = canStartArm || canStartAgent
            val neitherStarting = !isStartingArm && !isStartingAgent

            if (bothRunningLocally) {
                OutlinedButton(onClick = {
                    onStartStopArm()
                    onStartStopAgent()
                }) {
                    Text("Stop Both")
                }
            } else {
                Button(
                    onClick = onStartBoth,
                    enabled = neitherRunning && canStartEither && neitherStarting
                ) {
                    Text(
                        if (canStartEither) "Start Both Locally"
                        else "Waiting for upstream"
                    )
                }
            }
        }
    }
}

@Composable
private fun InnerNodeCard(
    node: PipelineNode,
    runningLocally: Boolean,
    detectedOnNetwork: Boolean,
    canProbe: Boolean,
    isProbing: Boolean,
    onProbe: () -> Unit,
    canStart: Boolean,
    isStarting: Boolean,
    onStartStop: () -> Unit
) {
    OutlinedCard(
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            // Node name + status
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    if (runningLocally) {
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
                        style = MaterialTheme.typography.titleSmall
                    )
                }
                val displayState = if (runningLocally || detectedOnNetwork) NodeState.Running else NodeState.Stopped
                NodeStateChip(state = displayState)
            }

            // Topics
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

            // Individual Start/Stop button
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
                        enabled = canStart && !isStarting
                    ) {
                        Text(if (canStart) "Start" else "Waiting for upstream")
                    }
                }
            }

            // Individual Probe button
                OutlinedButton(onClick = onProbe, enabled = canProbe && !runningLocally) {
                    if (isProbing) {
                        val transition = rememberInfiniteTransition(label = "probe_${node.id}")
                        val rotation by transition.animateFloat(
                            initialValue = 0f,
                            targetValue = 360f,
                            animationSpec = infiniteRepeatable(
                                animation = tween(1500, easing = LinearEasing),
                                repeatMode = RepeatMode.Restart
                            ),
                            label = "radar_rotation_${node.id}"
                        )
                        val pulse by transition.animateFloat(
                            initialValue = 0.5f,
                            targetValue = 1f,
                            animationSpec = infiniteRepeatable(
                                animation = tween(750, easing = LinearEasing),
                                repeatMode = RepeatMode.Reverse
                            ),
                            label = "radar_pulse_${node.id}"
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
    }
}
