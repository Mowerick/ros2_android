#!/usr/bin/env bash
#
# Setup ROS 2 network connectivity with Android device
#
# Usage: ./setup_ros2_network.sh <android_ip> <domain_id>
#

set -e # Exit on error

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
    exit 1
fi

ANDROID_IP="$1"
DOMAIN_ID="$2"

# Validate IP address format
if ! [[ "$ANDROID_IP" =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$ ]]; then
    error "Invalid IP address format: $ANDROID_IP"
    exit 1
fi

# Validate domain ID (must be 0-101)
if ! [[ "$DOMAIN_ID" =~ ^[0-9]+$ ]] || [ "$DOMAIN_ID" -lt 0 ] || [ "$DOMAIN_ID" -gt 101 ]; then
    error "Invalid domain ID: $DOMAIN_ID"
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
sudo iptables -C INPUT -p igmp -j ACCEPT 2>/dev/null ||
    sudo iptables -I INPUT 1 -p igmp -j ACCEPT

# Allow UDP multicast packets
sudo iptables -C INPUT -p udp -d 224.0.0.0/4 -j ACCEPT 2>/dev/null ||
    sudo iptables -I INPUT 1 -p udp -d 224.0.0.0/4 -j ACCEPT

# Allow UDP packets from Android device
sudo iptables -C INPUT -p udp -s "$ANDROID_IP" -j ACCEPT 2>/dev/null ||
    sudo iptables -I INPUT 1 -p udp -s "$ANDROID_IP" -j ACCEPT

info "iptables rules configured successfully"

# Export locally for the remainder of this script process
export ROS_DOMAIN_ID="$DOMAIN_ID"

# Check if ROS 2 is sourced
if ! command -v ros2 &>/dev/null; then
    error "ROS 2 not found in PATH. Please source your ROS 2 installation first."
    exit 1
fi

# Start ROS 2 daemon
info "Starting ROS 2 daemon on Domain $DOMAIN_ID..."
ros2 daemon stop 2>/dev/null || true 
sleep 1
ros2 daemon start

info "Waiting for daemon to initialize..."
sleep 2

# Verify daemon is running
if ros2 daemon status &>/dev/null; then
    info "ROS 2 daemon is running"
else
    error "Failed to start ROS 2 daemon"
    exit 1
fi

# Print summary
echo ""
info "Setup complete!"
echo ""
echo "Note: The network is configured, but this terminal session"
echo "still needs the domain ID set manually."
echo ""
echo "Run this command now:"
echo -e "  ${YELLOW}export ROS_DOMAIN_ID=$DOMAIN_ID${NC}"
echo ""
echo "Then verify discovery:"
echo "  ros2 topic list"
echo ""