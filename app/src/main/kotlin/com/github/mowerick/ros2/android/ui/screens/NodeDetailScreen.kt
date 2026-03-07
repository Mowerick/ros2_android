package com.github.mowerick.ros2.android.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
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
fun NodeDetailScreen(
    node: PipelineNode,
    canStart: Boolean,
    onBack: () -> Unit,
    onStartStop: () -> Unit
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(node.name) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Filled.ArrowBack, contentDescription = "Back")
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // State and start/stop
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                NodeStateChip(state = node.state)
                if (!node.isExternal) {
                    if (node.state == NodeState.Running) {
                        OutlinedButton(
                            onClick = onStartStop,
                            modifier = Modifier.height(40.dp)
                        ) {
                            Text("Stop Node")
                        }
                    } else {
                        Button(
                            onClick = onStartStop,
                            enabled = canStart,
                            modifier = Modifier.height(40.dp)
                        ) {
                            Text(if (canStart) "Start Node" else "Waiting for upstream")
                        }
                    }
                }
            }

            // External badge
            if (node.isExternal) {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("External Node", style = MaterialTheme.typography.titleSmall)
                        Text(
                            text = "This node runs on an external device (Jetson/PC) and is not managed by this app.",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(top = 4.dp)
                        )
                    }
                }
            }

            // Description
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("Description", style = MaterialTheme.typography.titleSmall)
                    Text(
                        text = node.description,
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                }
            }

            // Subscribes to
            if (node.subscribesTo.isNotEmpty()) {
                Text("Subscribes to", style = MaterialTheme.typography.titleSmall)
                node.subscribesTo.forEach { topic ->
                    TopicInfoCard(
                        label = "SUB",
                        topicName = topic.name,
                        topicType = topic.type
                    )
                }
            }

            // Publishes to
            if (node.publishesTo.isNotEmpty()) {
                Text("Publishes to", style = MaterialTheme.typography.titleSmall)
                node.publishesTo.forEach { topic ->
                    TopicInfoCard(
                        label = "PUB",
                        topicName = topic.name,
                        topicType = topic.type
                    )
                }
            }
        }
    }
}
