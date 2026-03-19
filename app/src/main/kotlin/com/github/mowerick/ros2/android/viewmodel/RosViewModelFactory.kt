package com.github.mowerick.ros2.android.viewmodel

import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import com.github.mowerick.ros2.android.interfaces.NetworkInterfaceProvider
import com.github.mowerick.ros2.android.interfaces.PermissionHandler

class RosViewModelFactory(
    private val applicationContext: Context,
    private val permissionHandler: PermissionHandler,
    private val networkProvider: NetworkInterfaceProvider,
    private val initialScreen: Screen = Screen.Dashboard
) : ViewModelProvider.Factory {
    @Suppress("UNCHECKED_CAST")
    override fun <T : ViewModel> create(modelClass: Class<T>): T {
        if (modelClass.isAssignableFrom(RosViewModel::class.java)) {
            return RosViewModel(
                applicationContext,
                permissionHandler,
                networkProvider,
                initialScreen
            ) as T
        }
        throw IllegalArgumentException("Unknown ViewModel class")
    }
}
