#!/usr/bin/env bash
#
# Setup ROS 2 network connectivity with Android device
#
# Usage: ./setup_ros2_network.sh <android_ip> <domain_id>
#
# This script:
# 1. Configures iptables firewall rules to allow DDS multicast discovery
# 2. Sets ROS_DOMAIN_ID environment variable
# 3. Starts the ROS 2 daemon for topic discovery
#
# Example:
#   ./setup_ros2_network.sh 192.168.1.100 1
#

set -e  # Exit on error

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Print functions
info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check arguments
if [ "$#" -ne 2 ]; then
    error "Invalid number of arguments"
    echo "Usage: $0 <android_ip> <domain_id>"
    echo ""
    echo "Arguments:"
    echo "  android_ip  - IP address of Android device (e.g., 192.168.1.100)"
    echo "  domain_id   - ROS 2 domain ID integer (0-101, typically 1)"
    echo ""
    echo "Example:"
    echo "  $0 192.168.1.100 1"
    echo ""
    echo "To find your Android device IP:"
    echo "  adb shell ip addr show wlan0 | grep inet"
    exit 1
fi

ANDROID_IP="$1"
DOMAIN_ID="$2"

# Validate IP address format
if ! [[ "$ANDROID_IP" =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$ ]]; then
    error "Invalid IP address format: $ANDROID_IP"
    echo "Expected format: xxx.xxx.xxx.xxx (e.g., 192.168.1.100)"
    exit 1
fi

# Validate domain ID (must be 0-101)
if ! [[ "$DOMAIN_ID" =~ ^[0-9]+$ ]] || [ "$DOMAIN_ID" -lt 0 ] || [ "$DOMAIN_ID" -gt 101 ]; then
    error "Invalid domain ID: $DOMAIN_ID"
    echo "Domain ID must be an integer between 0 and 101"
    exit 1
fi

info "Setting up ROS 2 network for Android device at $ANDROID_IP with domain ID $DOMAIN_ID"

# Check if running as root for iptables
if [ "$EUID" -ne 0 ]; then
    warn "iptables rules require root privileges"
    info "You will be prompted for sudo password"
fi

# Configure iptables rules for DDS multicast discovery
info "Configuring iptables firewall rules..."

# Allow IGMP (multicast group joins)
sudo iptables -C INPUT -p igmp -j ACCEPT 2>/dev/null || \
    sudo iptables -I INPUT 1 -p igmp -j ACCEPT

# Allow UDP multicast packets (224.0.0.0/4 is the multicast range)
sudo iptables -C INPUT -p udp -d 224.0.0.0/4 -j ACCEPT 2>/dev/null || \
    sudo iptables -I INPUT 1 -p udp -d 224.0.0.0/4 -j ACCEPT

# Allow UDP packets from Android device
sudo iptables -C INPUT -p udp -s "$ANDROID_IP" -j ACCEPT 2>/dev/null || \
    sudo iptables -I INPUT 1 -p udp -s "$ANDROID_IP" -j ACCEPT

info "iptables rules configured successfully"

# Show current INPUT chain rules
info "Current iptables INPUT chain (first 10 rules):"
sudo iptables -L INPUT -v -n --line-numbers | head -n 12

# Export ROS_DOMAIN_ID
info "Setting ROS_DOMAIN_ID=$DOMAIN_ID"
export ROS_DOMAIN_ID="$DOMAIN_ID"

# Add to current shell profile for persistence
if [ -n "$BASH_VERSION" ]; then
    SHELL_RC="$HOME/.bashrc"
elif [ -n "$ZSH_VERSION" ]; then
    SHELL_RC="$HOME/.zshrc"
else
    SHELL_RC="$HOME/.profile"
fi

# Check if ROS_DOMAIN_ID already set in shell profile
if grep -q "export ROS_DOMAIN_ID=" "$SHELL_RC" 2>/dev/null; then
    warn "ROS_DOMAIN_ID already set in $SHELL_RC"
    info "Current value: $(grep "export ROS_DOMAIN_ID=" "$SHELL_RC")"
    read -p "Update to ROS_DOMAIN_ID=$DOMAIN_ID? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        sed -i "s/export ROS_DOMAIN_ID=.*/export ROS_DOMAIN_ID=$DOMAIN_ID/" "$SHELL_RC"
        info "Updated ROS_DOMAIN_ID in $SHELL_RC"
    fi
else
    echo "export ROS_DOMAIN_ID=$DOMAIN_ID" >> "$SHELL_RC"
    info "Added ROS_DOMAIN_ID to $SHELL_RC for future sessions"
fi

# Check if ROS 2 is sourced
if ! command -v ros2 &> /dev/null; then
    error "ROS 2 not found in PATH"
    echo ""
    echo "Please source your ROS 2 installation first:"
    echo "  source /opt/ros/humble/setup.bash"
    echo ""
    echo "Or add it to your shell profile:"
    echo "  echo 'source /opt/ros/humble/setup.bash' >> $SHELL_RC"
    exit 1
fi

# Start ROS 2 daemon
info "Starting ROS 2 daemon..."
ros2 daemon stop 2>/dev/null || true  # Stop existing daemon if running
sleep 1
ros2 daemon start

info "Waiting for daemon to initialize..."
sleep 2

# Verify daemon is running
if ros2 daemon status &> /dev/null; then
    info "ROS 2 daemon is running"
else
    error "Failed to start ROS 2 daemon"
    exit 1
fi

# Print summary
echo ""
info "Setup complete!"
echo ""
echo "Configuration:"
echo "  Android IP:     $ANDROID_IP"
echo "  ROS_DOMAIN_ID:  $DOMAIN_ID"
echo ""
echo "Next steps:"
echo "  1. Start the ROS 2 app on your Android device"
echo "  2. Set domain ID to $DOMAIN_ID in the app"
echo "  3. Verify discovery:"
echo "       ros2 topic list"
echo "       ros2 topic echo /sensors/accelerometer"
echo ""
warn "Remember to source ROS 2 in new terminal sessions:"
echo "  source /opt/ros/humble/setup.bash"
echo "  export ROS_DOMAIN_ID=$DOMAIN_ID"
