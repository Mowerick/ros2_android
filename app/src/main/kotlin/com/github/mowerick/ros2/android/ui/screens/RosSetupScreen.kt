package com.github.mowerick.ros2.android.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Refresh
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
    rosDomainId: Int,
    onBack: () -> Unit,
    onStartRos: (domainId: Int, networkInterface: String) -> Unit,
    onRefreshInterfaces: () -> Unit = {},
    onDomainIdChanged: (Int) -> Unit,
) {
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
                            text = "ROS 2 is running with Domain ID $rosDomainId",
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
                        text = if (rosDomainId < 0) "---" else rosDomainId.toString(),
                        style = MaterialTheme.typography.headlineMedium,
                        modifier = Modifier.align(Alignment.CenterHorizontally)
                    )
                    NumericKeypad(
                        currentValue = rosDomainId,
                        onDigit = { digit ->
                            val newId = if (rosDomainId < 0) digit else rosDomainId * 10 + digit
                            onDomainIdChanged(newId)
                        },
                        onClear = { onDomainIdChanged(-1) }
                    )
                }
            }

            // Network interface section
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(
                    modifier = Modifier.padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text("Network Interface", style = MaterialTheme.typography.titleSmall)
                        IconButton(onClick = onRefreshInterfaces) {
                            Icon(Icons.Filled.Refresh, contentDescription = "Refresh interfaces")
                        }
                    }
                    if (networkInterfaces.isNotEmpty()) {
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
                    } else {
                        Text(
                            text = "No interfaces found. Connect to Wi-Fi and tap refresh.",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }

            // Start button
            if (rosStarted) {
                OutlinedButton(
                    onClick = { onStartRos(rosDomainId, selectedInterface) },
                    enabled = rosDomainId >= 0,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(56.dp)
                ) {
                    Text("Restart ROS", style = MaterialTheme.typography.titleMedium)
                }
            } else {
                Button(
                    onClick = { onStartRos(rosDomainId, selectedInterface) },
                    enabled = rosDomainId >= 0,
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
