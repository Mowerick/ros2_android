plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.github.mowerick.ros2.android"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.github.mowerick.ros2.android"
        minSdk = 33
        targetSdk = 33
        versionCode = 1
        versionName = "1.0"

        ndk {
            abiFilters += "arm64-v8a"
        }
    }

    buildFeatures {
        compose = true
    }

    composeOptions {
        kotlinCompilerExtensionVersion = "1.5.14"
    }

    // Pre-built native libs from CMake superbuild
    sourceSets["main"].jniLibs.srcDirs("${rootProject.projectDir}/build/jniLibs")

    signingConfigs {
        getByName("debug") {
            storeFile = file("${System.getProperty("user.home")}/.android/debug.keystore")
            storePassword = "android"
            keyAlias = "adb_debug_key"
            keyPassword = "android"
        }
    }

    buildTypes {
        debug {
            isDebuggable = true
            signingConfig = signingConfigs.getByName("debug")
        }
        release {
            isDebuggable = false
            isMinifyEnabled = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }

    kotlinOptions {
        jvmTarget = "21"
    }
}

dependencies {
    implementation("androidx.activity:activity-compose:1.8.2")
    implementation("androidx.compose.ui:ui:1.5.4")
    implementation("androidx.compose.foundation:foundation:1.5.4")
    implementation("androidx.compose.material3:material3:1.1.2")
    implementation("androidx.compose.material:material-icons-extended:1.5.4")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.7.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.7.0")
    implementation("com.google.android.gms:play-services-location:21.0.1")
    // Source - https://stackoverflow.com/a/74613696
    // Posted by TIMBLOCKER, modified by community. See post 'Timeline' for change history
    // Retrieved 2026-03-19, License - CC BY-SA 4.0
    implementation("com.jakewharton:process-phoenix:3.0.0")

    // USB Serial library for CP210x support (YDLIDAR communication)
    implementation("com.github.mik3y:usb-serial-for-android:3.8.0")
}
