package com.github.mowerick.ros2.android.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.github.mowerick.ros2.android.ui.components.NumericKeypad

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RosSetupScreen(
    rosStarted: Boolean,
    networkInterfaces: List<String>,
    onBack: () -> Unit,
    onStartRos: (domainId: Int, networkInterface: String) -> Unit
) {
    var domainId by remember { mutableIntStateOf(-1) }
    var selectedInterface by remember(networkInterfaces) {
        mutableStateOf(
            networkInterfaces.firstOrNull { it.startsWith("wlan") }
                ?: networkInterfaces.firstOrNull()
                ?: ""
        )
    }
    var dropdownExpanded by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("ROS Settings") },
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
                .padding(16.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // Status card
            if (rosStarted) {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Status", style = MaterialTheme.typography.titleSmall)
                        Text(
                            text = "ROS 2 is running",
                            style = MaterialTheme.typography.bodyLarge,
                            color = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.padding(top = 4.dp)
                        )
                    }
                }
            }

            // Domain ID section
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(
                    modifier = Modifier.padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Text("ROS Domain ID", style = MaterialTheme.typography.titleSmall)
                    Text(
                        text = if (domainId < 0) "---" else domainId.toString(),
                        style = MaterialTheme.typography.headlineMedium,
                        modifier = Modifier.align(Alignment.CenterHorizontally)
                    )
                    NumericKeypad(
                        currentValue = domainId,
                        onDigit = { digit ->
                            domainId = if (domainId < 0) digit else domainId * 10 + digit
                        },
                        onClear = { domainId = -1 }
                    )
                }
            }

            // Network interface section
            if (networkInterfaces.isNotEmpty()) {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(
                        modifier = Modifier.padding(16.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Text("Network Interface", style = MaterialTheme.typography.titleSmall)
                        ExposedDropdownMenuBox(
                            expanded = dropdownExpanded,
                            onExpandedChange = { dropdownExpanded = it }
                        ) {
                            TextField(
                                value = selectedInterface,
                                onValueChange = {},
                                readOnly = true,
                                trailingIcon = {
                                    ExposedDropdownMenuDefaults.TrailingIcon(expanded = dropdownExpanded)
                                },
                                modifier = Modifier.menuAnchor().fillMaxWidth()
                            )
                            ExposedDropdownMenu(
                                expanded = dropdownExpanded,
                                onDismissRequest = { dropdownExpanded = false }
                            ) {
                                networkInterfaces.forEach { iface ->
                                    DropdownMenuItem(
                                        text = { Text(iface) },
                                        onClick = {
                                            selectedInterface = iface
                                            dropdownExpanded = false
                                        }
                                    )
                                }
                            }
                        }
                    }
                }
            }

            // Start button
            if (rosStarted) {
                OutlinedButton(
                    onClick = { onStartRos(domainId, selectedInterface) },
                    enabled = domainId >= 0,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(56.dp)
                ) {
                    Text("Restart ROS", style = MaterialTheme.typography.titleMedium)
                }
            } else {
                Button(
                    onClick = { onStartRos(domainId, selectedInterface) },
                    enabled = domainId >= 0,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(56.dp)
                ) {
                    Text("Start ROS", style = MaterialTheme.typography.titleMedium)
                }
            }
        }
    }
}
