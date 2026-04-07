package com.github.mowerick.ros2.android.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.SwapVert
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.github.mowerick.ros2.android.model.NodeState
import com.github.mowerick.ros2.android.model.PipelineNode
import com.github.mowerick.ros2.android.ui.components.NodeStateChip
import com.github.mowerick.ros2.android.ui.components.TopicInfoCard

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CommandBridgeDetailScreen(
    armCommander: PipelineNode,
    microRosAgent: PipelineNode,
    onBack: () -> Unit,
    onStartStopArm: () -> Unit,
    onStartStopAgent: () -> Unit,
    onStartBoth: () -> Unit,
    armRunningLocally: Boolean,
    armDetectedOnNetwork: Boolean,
    agentRunningLocally: Boolean,
    agentDetectedOnNetwork: Boolean,
    canStartArm: Boolean,
    canStartAgent: Boolean,
    isStartingArm: Boolean = false,
    isStartingAgent: Boolean = false
) {
    val anyRunningLocally = armRunningLocally || agentRunningLocally
    val anyRunning = anyRunningLocally || armDetectedOnNetwork || agentDetectedOnNetwork
    val bothOnNetwork = armDetectedOnNetwork && agentDetectedOnNetwork
    val combinedState = if (anyRunning) NodeState.Running else NodeState.Stopped

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Command Bridge") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Filled.ArrowBack, contentDescription = "Back")
                    }
                }
            )
        }
    ) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(horizontal = 16.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // State and start/stop
            item {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    NodeStateChip(state = combinedState)
                    val canStartEither = canStartArm || canStartAgent
                    when {
                        bothOnNetwork -> {
                            Text(
                                text = "Running on Network",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.tertiary
                            )
                        }
                        armRunningLocally && agentRunningLocally -> {
                            OutlinedButton(onClick = {
                                onStartStopArm()
                                onStartStopAgent()
                            }) {
                                Text("Stop Both")
                            }
                        }
                        !armRunningLocally && !armDetectedOnNetwork && !agentRunningLocally && !agentDetectedOnNetwork -> {
                            Button(
                                onClick = onStartBoth,
                                enabled = canStartEither && !isStartingArm && !isStartingAgent
                            ) {
                                Text(if (canStartEither) "Start Both Locally" else "Waiting for upstream")
                            }
                        }
                        else -> {
                            // Mixed state - handled by per-node buttons below
                        }
                    }
                }
            }

            // Arm Commander section
            item {
                NodeSection(
                    node = armCommander,
                    runningLocally = armRunningLocally,
                    detectedOnNetwork = armDetectedOnNetwork,
                    canStart = canStartArm,
                    isStarting = isStartingArm,
                    onStartStop = onStartStopArm
                )
            }

            // Bidirectional arrow
            item {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.Center
                ) {
                    Icon(
                        Icons.Filled.SwapVert,
                        contentDescription = "Bidirectional data flow",
                        modifier = Modifier.size(32.dp),
                        tint = MaterialTheme.colorScheme.outline
                    )
                }
            }

            // micro-ROS Agent section
            item {
                NodeSection(
                    node = microRosAgent,
                    runningLocally = agentRunningLocally,
                    detectedOnNetwork = agentDetectedOnNetwork,
                    canStart = canStartAgent,
                    isStarting = isStartingAgent,
                    onStartStop = onStartStopAgent
                )
            }
        }
    }
}

@Composable
private fun NodeSection(
    node: PipelineNode,
    runningLocally: Boolean,
    detectedOnNetwork: Boolean,
    canStart: Boolean,
    isStarting: Boolean,
    onStartStop: () -> Unit
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            // Header with status
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(node.name, style = MaterialTheme.typography.titleMedium)
                val state = if (runningLocally || detectedOnNetwork) NodeState.Running else NodeState.Stopped
                NodeStateChip(state = state)
            }

            // Status badge
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

            // Description
            Text(
                text = node.description,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            // Per-node Start/Stop
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
        }
    }

    // Topics below the card
    if (node.subscribesTo.isNotEmpty()) {
        Text(
            "Subscribes to",
            style = MaterialTheme.typography.titleSmall,
            modifier = Modifier.padding(top = 8.dp)
        )
        node.subscribesTo.forEach { topic ->
            TopicInfoCard(
                label = "SUB",
                topicName = topic.name,
                topicType = topic.type
            )
        }
    }

    if (node.publishesTo.isNotEmpty()) {
        Text(
            "Publishes to",
            style = MaterialTheme.typography.titleSmall,
            modifier = Modifier.padding(top = 8.dp)
        )
        node.publishesTo.forEach { topic ->
            TopicInfoCard(
                label = "PUB",
                topicName = topic.name,
                topicType = topic.type
            )
        }
    }
}
