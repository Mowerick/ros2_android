package com.github.mowerick.ros2.android.model

data class PipelineNode(
    val id: String,
    val name: String,
    val description: String,
    val state: NodeState = NodeState.Stopped,
    val subscribesTo: List<TopicInfo> = emptyList(),
    val publishesTo: List<TopicInfo> = emptyList(),
    val upstreamNodeId: String? = null,
    val isExternal: Boolean = false
)
