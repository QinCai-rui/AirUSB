#!/bin/bash

# AirUSB Installation Script
# Optimized for WiFi 6E networks

set -e

AIRUSB_VERSION="1.0.0"
INSTALL_PREFIX="/usr/local"
SERVICE_DIR="/etc/systemd/system"
UDEV_DIR="/etc/udev/rules.d"
CONFIG_DIR="/etc/airusb"

echo "AirUSB v${AIRUSB_VERSION} Installation Script"
echo "============================================="

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root (use sudo)" 
   exit 1
fi

# Detect system architecture
ARCH=$(uname -m)
echo "Detected architecture: $ARCH"

# Check for WiFi 6E support
check_wifi6e_support() {
    echo "Checking WiFi 6E support..."
    
    if lspci | grep -q "Network controller.*AX2"; then
        echo "✓ Intel AX2xx WiFi 6E adapter detected"
    elif lspci | grep -q "Network controller.*MT792"; then
        echo "✓ MediaTek WiFi 6E adapter detected"  
    elif iw list 2>/dev/null | grep -q "6000 MHz"; then
        echo "✓ WiFi 6E (6GHz) support detected"
    else
        echo "⚠ Warning: WiFi 6E adapter not detected. Performance may be limited."
        read -p "Continue anyway? [y/N]: " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi
}

# Install system dependencies
install_dependencies() {
    echo "Installing system dependencies..."
    
    # Detect package manager
    if command -v apt-get &> /dev/null; then
        apt-get update
        apt-get install -y build-essential cmake libusb-1.0-0-dev zlib1g-dev pkg-config \
                          wireless-tools iw udev systemd
    elif command -v yum &> /dev/null; then
        yum groupinstall -y "Development Tools"
        yum install -y cmake libusb1-devel zlib-devel pkgconfig wireless-tools iw systemd
    elif command -v pacman &> /dev/null; then
        pacman -S --noconfirm base-devel cmake libusb zlib pkg-config wireless_tools iw systemd
    else
        echo "❌ Unsupported package manager. Please install dependencies manually."
        exit 1
    fi
    
    echo "✓ Dependencies installed"
}

# Compile and install AirUSB
compile_and_install() {
    echo "Compiling AirUSB..."
    
    # Create build directory
    if [[ ! -d "build" ]]; then
        mkdir build
    fi
    
    cd build
    
    # Configure with optimization flags
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
        -DCMAKE_CXX_FLAGS="-O3 -march=native -DWIFI6E_OPTIMIZED"
    
    # Build with all available cores
    make -j$(nproc)
    
    # Install binaries
    echo "Installing AirUSB..."
    make install
    
    # Set appropriate permissions
    chmod +x "$INSTALL_PREFIX/bin/airusb-server"
    chmod +x "$INSTALL_PREFIX/bin/airusb-client"
    
    echo "✓ AirUSB compiled and installed"
}

# Install system service
install_service() {
    echo "Installing systemd service..."
    
    # Copy service file
    cp scripts/airusb-server.service "$SERVICE_DIR/"
    
    # Update service file with correct paths
    sed -i "s|/usr/bin/airusb-server|$INSTALL_PREFIX/bin/airusb-server|g" "$SERVICE_DIR/airusb-server.service"
    
    # Create airusb user and group
    if ! getent group airusb > /dev/null 2>&1; then
        groupadd -r airusb
    fi
    
    if ! getent passwd airusb > /dev/null 2>&1; then
        useradd -r -g airusb -d /var/lib/airusb -s /bin/false airusb
    fi
    
    # Create directories
    mkdir -p /var/lib/airusb
    chown airusb:airusb /var/lib/airusb
    
    # Reload systemd
    systemctl daemon-reload
    
    echo "✓ Systemd service installed"
}

# Install udev rules
install_udev_rules() {
    echo "Installing udev rules..."
    
    # Copy udev rules
    cp scripts/99-airusb.rules "$UDEV_DIR/"
    
    # Create USB optimization script
    cat > /usr/local/bin/optimize-usb-for-airusb.sh << 'EOF'
#!/bin/bash
# USB device optimization for AirUSB

USB_DEVICE="$1"
USB_PATH="/sys/bus/usb/devices/$USB_DEVICE"

if [[ -d "$USB_PATH" ]]; then
    # Increase URB buffer size for high-speed devices
    if [[ -f "$USB_PATH/speed" ]]; then
        SPEED=$(cat "$USB_PATH/speed")
        if [[ "$SPEED" -ge 480 ]]; then
            echo 16 > /sys/module/usbcore/parameters/usbfs_memory_mb 2>/dev/null || true
        fi
    fi
    
    # Set device permissions for AirUSB group
    chgrp airusb "$USB_PATH" 2>/dev/null || true
    chmod g+rw "$USB_PATH" 2>/dev/null || true
fi
EOF
    
    chmod +x /usr/local/bin/optimize-usb-for-airusb.sh
    
    # Reload udev rules
    udevadm control --reload-rules
    udevadm trigger
    
    echo "✓ udev rules installed"
}

# Create configuration files
create_config() {
    echo "Creating configuration files..."
    
    mkdir -p "$CONFIG_DIR"
    
    # Server configuration
    cat > "$CONFIG_DIR/server.conf" << EOF
[network]
port = 3240
bind_address = 0.0.0.0
max_clients = 8

[performance]  
compression = lz4
buffer_size = 2097152
tcp_nodelay = true
high_priority_qos = true

[usb]
scan_interval = 5000
auto_attach = false
filter_hubs = true

[logging]
level = info
file = /var/log/airusb-server.log
EOF
    
    # Client configuration
    cat > "$CONFIG_DIR/client.conf" << EOF
[network]
port = 3240
reconnect_interval = 5000

[performance]
compression = lz4
buffer_size = 2097152
batch_urbs = true

[logging]
level = info
file = /var/log/airusb-client.log
EOF
    
    # Set permissions
    chown -R airusb:airusb "$CONFIG_DIR"
    chmod 644 "$CONFIG_DIR"/*.conf
    
    echo "✓ Configuration files created"
}

# Optimize system for WiFi 6E
optimize_system() {
    echo "Optimizing system for WiFi 6E performance..."
    
    # Network buffer optimization
    cat >> /etc/sysctl.conf << EOF

# AirUSB WiFi 6E optimizations
net.core.rmem_max = 67108864
net.core.wmem_max = 67108864
net.core.rmem_default = 262144
net.core.wmem_default = 262144
net.ipv4.tcp_rmem = 4096 87380 67108864
net.ipv4.tcp_wmem = 4096 65536 67108864
net.ipv4.tcp_congestion_control = bbr
net.core.netdev_max_backlog = 5000
EOF
    
    # Apply sysctl settings
    sysctl -p
    
    # USB subsystem optimization
    echo 16 > /sys/module/usbcore/parameters/usbfs_memory_mb 2>/dev/null || true
    
    # Add to modules to load at boot
    echo "usbcore" >> /etc/modules 2>/dev/null || true
    
    echo "✓ System optimized for WiFi 6E"
}

# Enable and start service
enable_service() {
    echo "Enabling AirUSB service..."
    
    systemctl enable airusb-server.service
    
    echo "✓ Service enabled"
    echo ""
    echo "Service commands:"
    echo "  Start:   sudo systemctl start airusb-server"
    echo "  Stop:    sudo systemctl stop airusb-server"  
    echo "  Status:  sudo systemctl status airusb-server"
    echo "  Logs:    sudo journalctl -u airusb-server -f"
}

# Verify installation
verify_installation() {
    echo "Verifying installation..."
    
    # Check binaries
    if [[ ! -x "$INSTALL_PREFIX/bin/airusb-server" ]]; then
        echo "❌ Server binary not found"
        exit 1
    fi
    
    if [[ ! -x "$INSTALL_PREFIX/bin/airusb-client" ]]; then
        echo "❌ Client binary not found"
        exit 1
    fi
    
    # Check service
    if ! systemctl is-enabled airusb-server.service &>/dev/null; then
        echo "❌ Service not enabled"
        exit 1
    fi
    
    # Test server startup (dry run)
    if ! "$INSTALL_PREFIX/bin/airusb-server" --version &>/dev/null; then
        echo "⚠ Warning: Server binary may have issues"
    fi
    
    echo "✓ Installation verified"
}

# Print post-install instructions
print_instructions() {
    echo ""
    echo "🎉 AirUSB installation completed successfully!"
    echo ""
    echo "Quick Start:"
    echo "1. Start the server:    sudo systemctl start airusb-server"
    echo "2. Connect USB devices to this machine"
    echo "3. From client machine: airusb-client <this_ip> -l"
    echo "4. Attach device:       airusb-client <this_ip> -a <device_id>"
    echo ""
    echo "Configuration:"
    echo "  Server config: $CONFIG_DIR/server.conf"
    echo "  Client config: $CONFIG_DIR/client.conf"
    echo ""
    echo "WiFi 6E Setup:"
    echo "  • Ensure 6GHz band is enabled on your router"
    echo "  • Use WPA3 security for best performance"  
    echo "  • Position devices close to router for optimal 6GHz signal"
    echo ""
    echo "Documentation: $INSTALL_PREFIX/share/doc/airusb/README.md"
}

# Main installation flow
main() {
    check_wifi6e_support
    install_dependencies
    compile_and_install
    install_service
    install_udev_rules
    create_config
    optimize_system
    enable_service
    verify_installation
    print_instructions
}

# Run main function
main "$@"