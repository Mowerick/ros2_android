package com.github.mowerick.ros2.android.ui.components

import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Circle
import androidx.compose.material.icons.filled.Error
import androidx.compose.material3.AssistChip
import androidx.compose.material3.AssistChipDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.github.mowerick.ros2.android.model.NodeState

@Composable
fun NodeStateChip(state: NodeState) {
    val (label, color) = when (state) {
        NodeState.Stopped -> "Stopped" to Color.Gray
        NodeState.Starting -> "Starting" to Color(0xFFFFA000)
        NodeState.Running -> "Running" to Color(0xFF4CAF50)
        NodeState.Error -> "Error" to MaterialTheme.colorScheme.error
    }

    AssistChip(
        onClick = {},
        label = { Text(label, style = MaterialTheme.typography.labelSmall) },
        leadingIcon = {
            when (state) {
                NodeState.Starting -> CircularProgressIndicator(
                    modifier = Modifier.size(14.dp),
                    strokeWidth = 2.dp,
                    color = color
                )
                NodeState.Error -> Icon(
                    Icons.Filled.Error,
                    contentDescription = "Error",
                    modifier = Modifier.size(14.dp),
                    tint = color
                )
                else -> Icon(
                    Icons.Filled.Circle,
                    contentDescription = label,
                    modifier = Modifier.size(10.dp),
                    tint = color
                )
            }
        },
        colors = AssistChipDefaults.assistChipColors(
            containerColor = color.copy(alpha = 0.12f),
            labelColor = color
        )
    )
}
