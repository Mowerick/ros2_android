package com.github.mowerick.ros2.android

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.core.content.ContextCompat
import androidx.lifecycle.viewmodel.compose.viewModel
import com.github.mowerick.ros2.android.ui.screens.BuiltInSensorsScreen
import com.github.mowerick.ros2.android.ui.screens.CameraDetailScreen
import com.github.mowerick.ros2.android.ui.screens.DashboardScreen
import com.github.mowerick.ros2.android.ui.screens.NodeDetailScreen
import com.github.mowerick.ros2.android.ui.screens.RosSetupScreen
import com.github.mowerick.ros2.android.ui.screens.SensorDetailScreen
import com.github.mowerick.ros2.android.ui.screens.SubsystemScreen
import com.github.mowerick.ros2.android.ui.theme.Ros2AndroidTheme
import com.github.mowerick.ros2.android.viewmodel.RosViewModel
import com.github.mowerick.ros2.android.viewmodel.Screen
import java.net.NetworkInterface

class MainActivity : ComponentActivity() {

    private val requestCameraPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            NativeBridge.nativeOnPermissionResult("CAMERA", granted)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        NativeBridge.nativeInit(cacheDir.absolutePath, packageName)
        NativeBridge.nativeSetNetworkInterfaces(queryNetworkInterfaces())

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
            != PackageManager.PERMISSION_GRANTED
        ) {
            requestCameraPermission.launch(Manifest.permission.CAMERA)
        } else {
            NativeBridge.nativeOnPermissionResult("CAMERA", true)
        }

        setContent {
            Ros2AndroidTheme {
                val vm: RosViewModel = viewModel()
                LaunchedEffect(Unit) { vm.loadNetworkInterfaces() }

                val screen by vm.screen.collectAsState()
                val rosStarted by vm.rosStarted.collectAsState()
                val rosDomainId by vm.rosDomainId.collectAsState()
                val sensors by vm.sensors.collectAsState()
                val cameras by vm.cameras.collectAsState()
                val reading by vm.currentReading.collectAsState()
                val networkInterfaces by vm.networkInterfaces.collectAsState()
                val pipelineNodes by vm.pipelineNodes.collectAsState()
                val isProbing by vm.isProbing.collectAsState()

                when (val s = screen) {
                    is Screen.Dashboard -> DashboardScreen(
                        rosStarted = rosStarted,
                        rosDomainId = rosDomainId,
                        sensorCount = sensors.size,
                        cameraCount = cameras.size,
                        onSettingsClick = { vm.navigateToRosSetup() },
                        onBuiltInSensorsClick = { vm.navigateToBuiltInSensors() },
                        onSubsystemClick = { vm.navigateToSubsystem() }
                    )
                    is Screen.RosSetup -> RosSetupScreen(
                        rosStarted = rosStarted,
                        networkInterfaces = networkInterfaces,
                        onBack = { vm.navigateBack() },
                        onStartRos = { domainId, iface -> vm.startRos(domainId, iface) }
                    )
                    is Screen.BuiltInSensors -> BuiltInSensorsScreen(
                        sensors = sensors,
                        cameras = cameras,
                        onBack = { vm.navigateBack() },
                        onSensorClick = { vm.navigateToSensor(it) },
                        onCameraClick = { vm.navigateToCamera(it) },
                        onSensorToggle = { sensor, enable ->
                            if (enable) vm.enableSensor(sensor.uniqueId)
                            else vm.disableSensor(sensor.uniqueId)
                        },
                        onCameraToggle = { camera, enable ->
                            if (enable) vm.enableCamera(camera.uniqueId)
                            else vm.disableCamera(camera.uniqueId)
                        }
                    )
                    is Screen.SensorDetail -> SensorDetailScreen(
                        sensor = s.sensor,
                        reading = reading,
                        onBack = { vm.navigateBack() },
                        onEnable = { vm.enableSensor(s.sensor.uniqueId) },
                        onDisable = { vm.disableSensor(s.sensor.uniqueId) }
                    )
                    is Screen.CameraDetail -> CameraDetailScreen(
                        camera = s.camera,
                        onBack = { vm.navigateBack() },
                        onEnable = { vm.enableCamera(s.camera.uniqueId) },
                        onDisable = { vm.disableCamera(s.camera.uniqueId) }
                    )
                    is Screen.Subsystem -> SubsystemScreen(
                        nodes = pipelineNodes,
                        onBack = { vm.navigateBack() },
                        onNodeClick = { vm.navigateToNode(it) },
                        onNodeStartStop = { vm.toggleNodeState(it) },
                        isNodeStartable = { vm.isNodeStartable(it) },
                        isProbing = isProbing,
                        onToggleProbing = { vm.toggleTopicProbing() }
                    )
                    is Screen.NodeDetail -> NodeDetailScreen(
                        node = s.node,
                        canStart = vm.isNodeStartable(s.node.id),
                        onBack = { vm.navigateBack() },
                        onStartStop = { vm.toggleNodeState(s.node.id) }
                    )
                }
            }
        }
    }

    override fun onDestroy() {
        NativeBridge.nativeDestroy()
        super.onDestroy()
    }

    private fun queryNetworkInterfaces(): Array<String> {
        return try {
            NetworkInterface.getNetworkInterfaces()
                ?.toList()
                ?.map { it.name }
                ?.toTypedArray()
                ?: emptyArray()
        } catch (e: Exception) {
            emptyArray()
        }
    }
}
