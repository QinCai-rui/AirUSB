#!/bin/bash
# AirUSB Server Setup Script
# Configures USB/IP server alongside AirUSB for full kernel integration

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

if [ "$EUID" -ne 0 ]; then 
    echo -e "${RED}Please run as root (sudo)${NC}"
    exit 1
fi

echo "========================================="
echo "  AirUSB Server USB/IP Setup"
echo "========================================="
echo ""

# Check if usbip is installed
if ! command -v usbip &> /dev/null; then
    echo -e "${YELLOW}Installing USB/IP tools...${NC}"
    apt-get update
    apt-get install -y linux-tools-generic || apt-get install -y usbip
fi

# Load kernel modules
echo "Loading USB/IP kernel modules..."
modprobe usbip-core || true
modprobe usbip-host || true

# List available USB devices
echo ""
echo "Available USB devices:"
echo "========================================="
usbip list -l

echo ""
echo "========================================="
echo ""

# Ask which device to share
read -p "Enter the busid of the device to share (e.g., 5-1): " BUSID

if [ -z "$BUSID" ]; then
    echo -e "${RED}No busid provided${NC}"
    exit 1
fi

# Bind the device
echo "Binding device $BUSID..."
usbip bind -b "$BUSID"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓${NC} Device bound successfully"
else
    echo -e "${RED}✗${NC} Failed to bind device"
    exit 1
fi

# Check if usbipd is already running
if pgrep usbipd > /dev/null; then
    echo -e "${GREEN}✓${NC} usbipd is already running"
else
    echo "Starting usbipd daemon..."
    usbipd -D
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓${NC} usbipd started successfully"
    else
        echo -e "${RED}✗${NC} Failed to start usbipd"
        exit 1
    fi
fi

echo ""
echo "========================================="
echo "  Setup Complete!"
echo "========================================="
echo ""
echo "The device $BUSID is now shared via USB/IP"
echo ""
echo "On the CLIENT, run:"
echo "  sudo ./airusb-client <this-server-ip> -a $BUSID"
echo ""
echo "To unbind the device later:"
echo "  sudo usbip unbind -b $BUSID"
echo ""
echo "To stop usbipd:"
echo "  sudo pkill usbipd"
echo ""
