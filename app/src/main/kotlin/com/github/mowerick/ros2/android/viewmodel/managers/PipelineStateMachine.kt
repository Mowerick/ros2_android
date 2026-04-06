package com.github.mowerick.ros2.android.viewmodel.managers

import android.content.Context
import com.github.mowerick.ros2.android.util.NativeBridge
import com.github.mowerick.ros2.android.model.NodeRuntimeState
import com.github.mowerick.ros2.android.model.NodeState
import com.github.mowerick.ros2.android.model.PipelineNode
import com.github.mowerick.ros2.android.model.PipelineState
import com.github.mowerick.ros2.android.model.TopicInfo
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Manages the ROS 2 perception & positioning pipeline state machine.
 * Handles node lifecycle, per-node topic probing, and distributed execution tracking.
 *
 * Each pipeline node can independently probe for its topics on the network,
 * enabling distributed deployments where different nodes run on different devices.
 */
class PipelineStateMachine(
    private val applicationContext: Context,
    private val coroutineScope: CoroutineScope,
    private val onError: (String) -> Unit,
    private val onPerceptionStateChange: (Boolean) -> Unit
) {

    private val _pipelineNodes = MutableStateFlow(createDefaultPipelineNodes())
    val pipelineNodes: StateFlow<List<PipelineNode>> = _pipelineNodes

    private val _nodeStates = MutableStateFlow<Map<String, NodeRuntimeState>>(emptyMap())
    val nodeStates: StateFlow<Map<String, NodeRuntimeState>> = _nodeStates

    private val _pipelineState = MutableStateFlow(PipelineState.STOPPED)
    val pipelineState: StateFlow<PipelineState> = _pipelineState

    private val _discoveredTopics = MutableStateFlow<Set<String>>(emptySet())
    val discoveredTopics: StateFlow<Set<String>> = _discoveredTopics

    private var sharedPollingJob: Job? = null

    // -- Query methods --

    fun canStartNodeLocally(nodeId: String): Boolean {
        val node = _pipelineNodes.value.find { it.id == nodeId } ?: return false
        if (node.isExternal) return false
        val state = _nodeStates.value[nodeId]
        if (state?.runningLocally == true || state?.detectedOnNetwork == true) return false
        return state?.upstreamAvailable == true
    }

    fun isNodeRunningLocally(nodeId: String): Boolean {
        return _nodeStates.value[nodeId]?.runningLocally == true
    }

    fun isNodeDetectedOnNetwork(nodeId: String): Boolean {
        return _nodeStates.value[nodeId]?.detectedOnNetwork == true
    }

    fun getNodeDisplayState(nodeId: String): NodeState {
        val state = _nodeStates.value[nodeId]
        return if (state?.isRunning == true) NodeState.Running else NodeState.Stopped
    }

    fun isNodeStartable(nodeId: String): Boolean {
        return canStartNodeLocally(nodeId)
    }

    fun canProbeNode(nodeId: String): Boolean {
        val state = _nodeStates.value[nodeId]
        // Allow stopping an active probe
        if (state?.isProbing == true) return true
        // Can't probe if already running
        if (state?.runningLocally == true || state?.detectedOnNetwork == true) return false

        // Check if the pipeline FSM is in the correct state for this node to probe
        return when (nodeId) {
            "zed_stereo_node" -> _pipelineState.value in listOf(PipelineState.STOPPED, PipelineState.ZED_PROBING)
            "object_detection" -> _pipelineState.value == PipelineState.ZED_AVAILABLE
            "target_manager" -> _pipelineState.value == PipelineState.DETECTION_RUNNING
            "arm_commander" -> _pipelineState.value == PipelineState.TARGET_RUNNING
            "micro_ros_agent" -> _pipelineState.value == PipelineState.ARM_RUNNING
            else -> false
        }
    }

    // -- Node lifecycle --

    fun startNode(nodeId: String) {
        coroutineScope.launch(Dispatchers.IO) {
            try {
                when (nodeId) {
                    "object_detection" -> {
                        val modelsPath = "${applicationContext.filesDir.absolutePath}/models"
                        NativeBridge.enablePerception(modelsPath)
                        withContext(Dispatchers.Main) {
                            onPerceptionStateChange(true)
                        }
                    }
                    "target_manager" -> { /* TODO: Implement target manager start */ }
                    "arm_commander" -> { /* TODO: Implement arm commander start */ }
                    "micro_ros_agent" -> { /* TODO: Implement micro-ROS agent start */ }
                }
                updateNodeState(nodeId) { it.copy(runningLocally = true, isProbing = false) }
                advanceState()
                updatePolling()
            } catch (e: Exception) {
                android.util.Log.e("PipelineStateMachine", "Failed to start node $nodeId", e)
                withContext(Dispatchers.Main) {
                    onError("Failed to start $nodeId: ${e.message}")
                }
            }
        }
    }

    fun stopNode(nodeId: String) {
        coroutineScope.launch(Dispatchers.IO) {
            try {
                when (nodeId) {
                    "object_detection" -> {
                        if (_nodeStates.value["object_detection"]?.runningLocally == true) {
                            NativeBridge.disablePerception()
                        }
                        withContext(Dispatchers.Main) {
                            onPerceptionStateChange(false)
                        }
                    }
                    "target_manager" -> { /* TODO: Implement target manager stop */ }
                    "arm_commander" -> { /* TODO: Implement arm commander stop */ }
                    "micro_ros_agent" -> { /* TODO: Implement micro-ROS agent stop */ }
                }

                stopDownstreamNodes(nodeId)
                removeNodeState(nodeId)
                rollbackState()
                updatePolling()
            } catch (e: Exception) {
                android.util.Log.e("PipelineStateMachine", "Failed to stop node $nodeId", e)
                withContext(Dispatchers.Main) {
                    onError("Failed to stop $nodeId: ${e.message}")
                }
            }
        }
    }

    fun toggleNodeState(nodeId: String) {
        val state = _nodeStates.value[nodeId]

        if (state?.runningLocally == true) {
            stopNode(nodeId)
        } else if (state?.detectedOnNetwork != true) {
            if (canStartNodeLocally(nodeId)) {
                startNode(nodeId)
            }
        }
    }

    // -- Pipeline reset --

    fun resetPipeline() {
        // Stop any locally running nodes
        for ((nodeId, state) in _nodeStates.value) {
            if (state.runningLocally) {
                when (nodeId) {
                    "object_detection" -> NativeBridge.disablePerception()
                    // TODO: Add other node stop calls
                }
            }
        }
        onPerceptionStateChange(false)
        sharedPollingJob?.cancel()
        sharedPollingJob = null
        _nodeStates.value = emptyMap()
        _pipelineState.value = PipelineState.STOPPED
        _discoveredTopics.value = emptySet()
    }

    // -- Per-node topic probing --

    fun toggleNodeProbing(nodeId: String) {
        val current = _nodeStates.value[nodeId] ?: NodeRuntimeState()
        val newProbing = !current.isProbing

        if (newProbing) {
            updateNodeState(nodeId) { it.copy(isProbing = true) }
            // Advance from STOPPED to ZED_PROBING when first probe starts
            if (_pipelineState.value == PipelineState.STOPPED) {
                advanceState()
            }
        } else {
            // Stop probing - keep upstreamAvailable and detectedOnNetwork intact
            // (they reflect actual topic presence, managed by evaluateAllNodes)
            updateNodeState(nodeId) { it.copy(isProbing = false) }
            // Rollback from ZED_PROBING to STOPPED when no nodes are probing
            if (_pipelineState.value == PipelineState.ZED_PROBING &&
                !_nodeStates.value.values.any { it.isProbing }) {
                rollbackState()
            }
        }

        updatePolling()
    }

    // -- State transitions --

    private fun advanceState() {
        PipelineState.nextState(_pipelineState.value)?.let { _pipelineState.value = it }
    }

    private fun rollbackState() {
        PipelineState.previousState(_pipelineState.value)?.let { _pipelineState.value = it }
    }

    // -- Private helpers --

    private fun updateNodeState(nodeId: String, transform: (NodeRuntimeState) -> NodeRuntimeState) {
        val current = _nodeStates.value[nodeId] ?: NodeRuntimeState()
        _nodeStates.value = _nodeStates.value.toMutableMap().apply {
            put(nodeId, transform(current))
        }
    }

    private fun removeNodeState(nodeId: String) {
        _nodeStates.value = _nodeStates.value.toMutableMap().apply {
            remove(nodeId)
        }
    }

    private fun updatePolling() {
        val anyProbing = _nodeStates.value.values.any { it.isProbing }
        if (anyProbing && sharedPollingJob == null) {
            sharedPollingJob = coroutineScope.launch {
                while (true) {
                    try {
                        val topics = NativeBridge.nativeGetDiscoveredTopics().toSet()
                        _discoveredTopics.value = topics
                        android.util.Log.d("PipelineStateMachine", "discovered topics: ${topics.joinToString(", ")}")
                        evaluateAllNodes(topics)
                    } catch (_: UnsatisfiedLinkError) {
                        // Native library not loaded - stop all probing
                        _nodeStates.value = _nodeStates.value.mapValues {
                            it.value.copy(isProbing = false)
                        }
                        _pipelineState.value = PipelineState.STOPPED
                        sharedPollingJob = null
                        return@launch
                    } catch (e: Exception) {
                        android.util.Log.e("PipelineStateMachine", "Failed to probe topics", e)
                    }
                    delay(5000)
                }
            }
        } else if (!anyProbing && sharedPollingJob != null) {
            sharedPollingJob?.cancel()
            sharedPollingJob = null
        }
    }

    private fun evaluateAllNodes(discoveredTopics: Set<String>) {
        for (node in _pipelineNodes.value) {
            val wasUpstreamAvailable = _nodeStates.value[node.id]?.upstreamAvailable == true

            val upstreamAvailable = node.subscribesTo.isEmpty() ||
                node.subscribesTo.all { it.name in discoveredTopics }

            // Only update upstreamAvailable - detectedOnNetwork is managed
            // exclusively by the advancement logic below (based on downstream
            // node's subscribesTo, not this node's publishesTo)
            updateNodeState(node.id) { it.copy(upstreamAvailable = upstreamAvailable) }

            // Advance when a node's subscribesTo topics are newly discovered
            // This means the upstream node is providing what this node needs
            if (upstreamAvailable && !wasUpstreamAvailable && node.subscribesTo.isNotEmpty()) {
                node.upstreamNodeId?.let { upstreamId ->
                    updateNodeState(upstreamId) { it.copy(detectedOnNetwork = true, isProbing = false) }
                }
                android.util.Log.d("PipelineStateMachine", "upstream available for ${node.id}, advancing state")
                advanceState()
            }
        }
    }

    private fun stopDownstreamNodes(nodeId: String) {
        val downstreamOrder = when (nodeId) {
            "object_detection" -> listOf("target_manager", "arm_commander", "micro_ros_agent")
            "target_manager" -> listOf("arm_commander", "micro_ros_agent")
            "arm_commander" -> listOf("micro_ros_agent")
            else -> emptyList()
        }

        for (downstream in downstreamOrder) {
            if (_nodeStates.value[downstream]?.runningLocally == true) {
                when (downstream) {
                    "object_detection" -> NativeBridge.disablePerception()
                    // TODO: Add other node stop calls
                }
                removeNodeState(downstream)
            }
        }
    }

    companion object {
        private fun createDefaultPipelineNodes(): List<PipelineNode> = listOf(
            PipelineNode(
                id = "zed_stereo_node",
                name = "ZED 2i Camera",
                description = "Captures stereo image, depth, point cloud, and IMU data from ZED 2i camera. Runs on external NVIDIA Jetson/PC and streams to Android via DDS.",
                subscribesTo = emptyList(),
                publishesTo = listOf(
                    TopicInfo("/zed/zed_node/rgb/image_rect_color/compressed", "sensor_msgs/msg/CompressedImage"),
                    TopicInfo("/zed/zed_node/depth/depth_registered", "sensor_msgs/msg/Image"),
                    TopicInfo("/zed/zed_node/point_cloud/cloud_registered", "sensor_msgs/msg/PointCloud2"),
                    TopicInfo("/zed/zed_node/imu/data", "sensor_msgs/msg/Imu")
                ),
                upstreamNodeId = null,
                isExternal = true
            ),
            PipelineNode(
                id = "object_detection",
                name = "3D Object Detection",
                description = "Runs 3D Object Detection and Deep SORT to detect and track Colorado Potato Beetle life stages (beetle, larva, eggs) in 3D space using ZED camera data.",
                subscribesTo = listOf(
                    TopicInfo("/zed/zed_node/rgb/image_rect_color/compressed", "sensor_msgs/msg/CompressedImage"),
                    TopicInfo("/zed/zed_node/depth/depth_registered", "sensor_msgs/msg/Image"),
                    TopicInfo("/zed/zed_node/point_cloud/cloud_registered", "sensor_msgs/msg/PointCloud2")
                ),
                publishesTo = listOf(
                    TopicInfo("/cpb_beetle_center", "geometry_msgs/msg/Point"),
                    TopicInfo("/cpb_larva_center", "geometry_msgs/msg/Point"),
                    TopicInfo("/cpb_eggs_center", "geometry_msgs/msg/Point"),
                    TopicInfo("/cpb_beetle", "sensor_msgs/msg/PointCloud2"),
                    TopicInfo("/cpb_larva", "sensor_msgs/msg/PointCloud2"),
                    TopicInfo("/cpb_eggs", "sensor_msgs/msg/PointCloud2")
                ),
                upstreamNodeId = "zed_stereo_node",
                isExternal = false
            ),
            PipelineNode(
                id = "target_manager",
                name = "Target Manager",
                description = "Selects primary targets (CPB eggs) and performs IMU-based orientation calibration for laser positioning. Computes pan/tilt commands with offset correction.",
                subscribesTo = listOf(
                    TopicInfo("/cpb_eggs_center", "geometry_msgs/msg/Point"),
                    TopicInfo("/zed/zed_node/imu/data", "sensor_msgs/msg/Imu")
                ),
                publishesTo = listOf(
                    TopicInfo("/arm_position_goal", "std_msgs/msg/Float32MultiArray")
                ),
                upstreamNodeId = "object_detection",
                isExternal = false
            ),
            PipelineNode(
                id = "arm_commander",
                name = "Arm Commander",
                description = "State machine for pan/tilt arm control with ACK/NACK protocol. Manages command retries, timeouts, and feedback synchronization with microcontroller.",
                subscribesTo = listOf(
                    TopicInfo("/arm_position_goal", "std_msgs/msg/Float32MultiArray"),
                    TopicInfo("/PointNShoot_ACK", "std_msgs/msg/Float32"),
                    TopicInfo("/PointNShoot_DONE", "std_msgs/msg/Float32"),
                    TopicInfo("/PointNShoot_NACK", "std_msgs/msg/Float32")
                ),
                publishesTo = listOf(
                    TopicInfo("/PointNShoot", "std_msgs/msg/Float32MultiArray"),
                    TopicInfo("/arm_position_feedback", "std_msgs/msg/String")
                ),
                upstreamNodeId = "target_manager",
                isExternal = false
            ),
            PipelineNode(
                id = "micro_ros_agent",
                name = "micro-ROS Agent",
                description = "Bridges ROS 2 DDS network to Zephyr microcontroller via USB serial (921600 baud). Forwards /PointNShoot commands to pan/tilt arm MCU. Investigation - may require external PC.",
                subscribesTo = listOf(
                    TopicInfo("/PointNShoot", "std_msgs/msg/Float32MultiArray")
                ),
                publishesTo = emptyList(),
                upstreamNodeId = "arm_commander",
                isExternal = false
            )
        )
    }
}
