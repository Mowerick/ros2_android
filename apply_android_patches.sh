#!/usr/bin/env bash
# Apply Android-specific patches to YDLIDAR SDK and driver

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PATCHES_DIR="$SCRIPT_DIR/android_patches"
DEPS_DIR="$SCRIPT_DIR/deps"

echo "Applying Android patches..."

# YDLIDAR SDK patches
if [ -d "$DEPS_DIR/ydlidar_sdk" ]; then
  echo "  - Patching YDLIDAR SDK..."
  cp "$PATCHES_DIR/ydlidar_sdk/ydlidar.h" "$DEPS_DIR/ydlidar_sdk/core/base/ydlidar.h"
  cp "$PATCHES_DIR/ydlidar_sdk/thread.h" "$DEPS_DIR/ydlidar_sdk/core/base/thread.h"
  cp "$PATCHES_DIR/ydlidar_sdk/base_CMakeLists.txt" "$DEPS_DIR/ydlidar_sdk/core/base/CMakeLists.txt"
  cp "$PATCHES_DIR/ydlidar_sdk/network_CMakeLists.txt" "$DEPS_DIR/ydlidar_sdk/core/network/CMakeLists.txt"
  echo "    ✓ YDLIDAR SDK patched"
else
  echo "    ⚠ YDLIDAR SDK not found, skipping"
fi

echo "Android patches applied successfully!"
