package com.github.mowerick.ros2.android.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.github.mowerick.ros2.android.model.PipelineNode
import com.github.mowerick.ros2.android.ui.components.CommandBridgeCard
import com.github.mowerick.ros2.android.ui.components.PipelineNodeCard
import com.github.mowerick.ros2.android.ui.components.TopicConnector

private val COMMAND_BRIDGE_NODE_IDS = setOf("arm_commander", "micro_ros_agent")

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SubsystemScreen(
    nodes: List<PipelineNode>,
    onBack: () -> Unit,
    onNodeClick: (PipelineNode) -> Unit,
    onNodeStartStop: (String) -> Unit,
    isNodeStartable: (String) -> Boolean,
    canProbeNode: (String) -> Boolean,
    isNodeProbing: (String) -> Boolean,
    onToggleNodeProbing: (String) -> Unit,
    onReset: () -> Unit,
    onCommandBridgeClick: () -> Unit = {},
    isRunningLocally: (String) -> Boolean = { false },
    isDetectedOnNetwork: (String) -> Boolean = { false },
    isNodeStarting: (String) -> Boolean = { false }
) {
    val regularNodes = nodes.filter { it.id !in COMMAND_BRIDGE_NODE_IDS }
    val armCommander = nodes.find { it.id == "arm_commander" }
    val microRosAgent = nodes.find { it.id == "micro_ros_agent" }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("ROS 2 Subsystem") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Filled.ArrowBack, contentDescription = "Back")
                    }
                },
                actions = {
                    IconButton(onClick = onReset) {
                        Icon(Icons.Filled.Refresh, contentDescription = "Reset Pipeline")
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
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            // Render regular pipeline nodes
            regularNodes.forEachIndexed { index, node ->
                item(key = node.id) {
                    PipelineNodeCard(
                        node = node,
                        canStart = isNodeStartable(node.id),
                        canProbe = canProbeNode(node.id),
                        onStartStop = { onNodeStartStop(node.id) },
                        onClick = { onNodeClick(node) },
                        onProbe = { onToggleNodeProbing(node.id) },
                        isProbing = isNodeProbing(node.id),
                        runningLocally = isRunningLocally(node.id),
                        detectedOnNetwork = isDetectedOnNetwork(node.id),
                        isStarting = isNodeStarting(node.id)
                    )
                }

                // TopicConnector after each regular node (including before Command Bridge)
                if (index < regularNodes.lastIndex || (armCommander != null && microRosAgent != null)) {
                    item(key = "${node.id}_connector") {
                        TopicConnector()
                    }
                }
            }

            // Render Command Bridge group
            if (armCommander != null && microRosAgent != null) {
                item(key = "command_bridge") {
                    CommandBridgeCard(
                        armCommander = armCommander,
                        microRosAgent = microRosAgent,
                        onClick = onCommandBridgeClick,
                        onStartStopArm = { onNodeStartStop("arm_commander") },
                        onStartStopAgent = { onNodeStartStop("micro_ros_agent") },
                        onStartBoth = {
                            onNodeStartStop("arm_commander")
                            onNodeStartStop("micro_ros_agent")
                        },
                        onProbeArmCommander = { onToggleNodeProbing("arm_commander") },
                        onProbeMicroRosAgent = { onToggleNodeProbing("micro_ros_agent") },
                        canProbeArmCommander = canProbeNode("arm_commander"),
                        canProbeMicroRosAgent = canProbeNode("micro_ros_agent"),
                        isProbingArmCommander = isNodeProbing("arm_commander"),
                        isProbingMicroRosAgent = isNodeProbing("micro_ros_agent"),
                        armRunningLocally = isRunningLocally("arm_commander"),
                        armDetectedOnNetwork = isDetectedOnNetwork("arm_commander"),
                        agentRunningLocally = isRunningLocally("micro_ros_agent"),
                        agentDetectedOnNetwork = isDetectedOnNetwork("micro_ros_agent"),
                        canStartArm = isNodeStartable("arm_commander"),
                        canStartAgent = isNodeStartable("micro_ros_agent"),
                        isStartingArm = isNodeStarting("arm_commander"),
                        isStartingAgent = isNodeStarting("micro_ros_agent")
                    )
                }
            }
        }
    }
}
