package com.github.mowerick.ros2.android.model

/**
 * Manages pipeline node dependencies and state transitions.
 * Encapsulates logic for determining node startability, cascading stops,
 * and updating external node states based on topic discovery.
 */
class NodeDependencyGraph(private val nodes: List<PipelineNode>) {

    /**
     * Check if a node can be started.
     * A node is startable if:
     * - It's not external (external nodes are controlled externally)
     * - It has no upstream dependency, OR its upstream node is Running
     */
    fun isNodeStartable(nodeId: String): Boolean {
        val node = nodes.find { it.id == nodeId } ?: return false
        if (node.isExternal) return false
        val upstream = node.upstreamNodeId ?: return true
        val upstreamNode = nodes.find { it.id == upstream } ?: return false
        return upstreamNode.state == NodeState.Running
    }

    /**
     * Find all nodes that should be stopped when a given node stops.
     * Uses breadth-first search to find all downstream dependents.
     */
    fun findDownstreamNodes(nodeId: String): Set<String> {
        val toStop = mutableSetOf<String>()
        var frontier = setOf(nodeId)

        while (frontier.isNotEmpty()) {
            val next = mutableSetOf<String>()
            for (n in nodes) {
                if (n.upstreamNodeId in frontier && n.id !in toStop && !n.isExternal) {
                    toStop.add(n.id)
                    next.add(n.id)
                }
            }
            frontier = next
        }

        return toStop
    }

    /**
     * Update the list with a node toggled on/off.
     * When stopping, cascades to all downstream dependents.
     * When starting, only starts if upstream is ready.
     */
    fun toggleNodeState(nodeId: String): List<PipelineNode> {
        val target = nodes.find { it.id == nodeId } ?: return nodes
        if (target.isExternal) return nodes

        return if (target.state == NodeState.Running) {
            // Stopping: cascade to all downstream nodes
            val toStop = findDownstreamNodes(nodeId)
            nodes.map { node ->
                if (node.id in toStop && node.state == NodeState.Running) {
                    node.copy(state = NodeState.Stopped)
                } else {
                    node
                }
            }
        } else {
            // Starting: only if upstream is running
            if (!isNodeStartable(nodeId)) return nodes
            nodes.map { node ->
                if (node.id == nodeId) node.copy(state = NodeState.Running) else node
            }
        }
    }

    /**
     * Update external node states based on discovered topics.
     * External nodes are Running if all their published topics are discovered.
     * Returns updated node list and whether any changes occurred.
     */
    fun updateExternalNodeStates(discoveredTopics: Set<String>): Pair<List<PipelineNode>, Boolean> {
        var changed = false
        val updated = nodes.map { node ->
            if (!node.isExternal) return@map node

            val allPublished = node.publishesTo.isNotEmpty() &&
                node.publishesTo.all { it.name in discoveredTopics }
            val newState = if (allPublished) NodeState.Running else NodeState.Stopped

            if (newState != node.state) {
                changed = true
                node.copy(state = newState)
            } else {
                node
            }
        }

        return Pair(updated, changed)
    }

    /**
     * Find all nodes that should be stopped when a parent node stops.
     * Similar to findDownstreamNodes but starts from a parent that's already stopped.
     */
    fun cascadeStop(parentId: String): List<PipelineNode> {
        val toStop = mutableSetOf<String>()
        var frontier = setOf(parentId)

        while (frontier.isNotEmpty()) {
            val next = mutableSetOf<String>()
            for (n in nodes) {
                if (n.upstreamNodeId in frontier && n.id !in toStop && !n.isExternal) {
                    toStop.add(n.id)
                    next.add(n.id)
                }
            }
            frontier = next
        }

        return if (toStop.isEmpty()) {
            nodes
        } else {
            nodes.map { node ->
                if (node.id in toStop && node.state == NodeState.Running) {
                    node.copy(state = NodeState.Stopped)
                } else {
                    node
                }
            }
        }
    }
}
