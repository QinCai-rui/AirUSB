#!/bin/bash
# AirUSB Prerequisites Checker
# Checks system configuration and provides setup guidance

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================="
echo "  AirUSB Prerequisites Checker"
echo "========================================="
echo ""

# Check if running as root for certain operations
if [ "$EUID" -ne 0 ]; then 
    echo -e "${YELLOW}Note: Some checks require root. Run with sudo for complete check.${NC}"
    echo ""
fi

# Check 1: libusb
echo -n "Checking libusb-1.0... "
if pkg-config --exists libusb-1.0; then
    VERSION=$(pkg-config --modversion libusb-1.0)
    echo -e "${GREEN}✓${NC} Found version $VERSION"
else
    echo -e "${RED}✗${NC} Not found"
    echo "  Install: sudo apt-get install libusb-1.0-0-dev"
fi

# Check 2: Build tools
echo -n "Checking build tools... "
if command -v cmake &> /dev/null && command -v g++ &> /dev/null; then
    CMAKE_VERSION=$(cmake --version | head -n1 | awk '{print $3}')
    echo -e "${GREEN}✓${NC} cmake $CMAKE_VERSION, g++ found"
else
    echo -e "${RED}✗${NC} Missing"
    echo "  Install: sudo apt-get install build-essential cmake"
fi

# Check 3: zlib
echo -n "Checking zlib... "
if pkg-config --exists zlib; then
    VERSION=$(pkg-config --modversion zlib)
    echo -e "${GREEN}✓${NC} Found version $VERSION"
else
    echo -e "${RED}✗${NC} Not found"
    echo "  Install: sudo apt-get install zlib1g-dev"
fi

echo ""
echo "========================================="
echo "  USB/IP Kernel Support"
echo "========================================="
echo ""

# Check 4: vhci_hcd module
echo -n "Checking vhci_hcd module... "
if lsmod | grep -q vhci_hcd; then
    echo -e "${GREEN}✓${NC} Loaded"
elif modinfo vhci-hcd &> /dev/null; then
    echo -e "${YELLOW}⚠${NC} Available but not loaded"
    echo "  Load with: sudo modprobe vhci-hcd"
else
    echo -e "${RED}✗${NC} Not available"
    echo "  Your kernel may not have USB/IP support compiled in"
    echo "  Check: ls /lib/modules/\$(uname -r)/kernel/drivers/usb/usbip/"
fi

# Check 5: usbip tools
echo -n "Checking usbip tools... "
if command -v usbip &> /dev/null; then
    echo -e "${GREEN}✓${NC} Installed"
    
    # Check if usbip_host is loaded
    echo -n "Checking usbip-host module... "
    if lsmod | grep -q usbip_host; then
        echo -e "${GREEN}✓${NC} Loaded"
    elif modinfo usbip-host &> /dev/null; then
        echo -e "${YELLOW}⚠${NC} Available but not loaded"
        echo "  Load with: sudo modprobe usbip-host"
    else
        echo -e "${YELLOW}⚠${NC} Module not found"
    fi
else
    echo -e "${YELLOW}⚠${NC} Not installed"
    echo "  Install: sudo apt-get install linux-tools-generic"
    echo "  (For standard USB/IP workaround)"
fi

echo ""
echo "========================================="
echo "  Network Configuration"
echo "========================================="
echo ""

# Check 6: Firewall status
echo -n "Checking firewall (port 3240)... "
if command -v ufw &> /dev/null; then
    if ufw status 2>/dev/null | grep -q "3240.*ALLOW"; then
        echo -e "${GREEN}✓${NC} Port 3240 allowed"
    elif ufw status 2>/dev/null | grep -q "Status: active"; then
        echo -e "${YELLOW}⚠${NC} Firewall active, port 3240 not explicitly allowed"
        echo "  Allow with: sudo ufw allow 3240/tcp"
    else
        echo -e "${GREEN}✓${NC} Firewall inactive"
    fi
else
    echo -e "${GREEN}✓${NC} ufw not installed (assuming no firewall)"
fi

# Check 7: Network interfaces
echo -n "Checking WiFi interfaces... "
WIFI_INTERFACES=$(iw dev 2>/dev/null | grep Interface | wc -l || echo "0")
if [ "$WIFI_INTERFACES" -gt 0 ]; then
    echo -e "${GREEN}✓${NC} Found $WIFI_INTERFACES wireless interface(s)"
    
    # Check for WiFi 6E support (6GHz)
    if iw list 2>/dev/null | grep -q "6[0-9][0-9][0-9] MHz"; then
        echo "  WiFi 6E (6GHz) support: ${GREEN}✓${NC}"
    else
        echo "  WiFi 6E (6GHz) support: ${YELLOW}Not detected${NC}"
        echo "    (AirUSB works on any network but optimized for WiFi 6E)"
    fi
else
    echo -e "${YELLOW}⚠${NC} No wireless interfaces detected"
    echo "  (You can still use AirUSB over Ethernet)"
fi

echo ""
echo "========================================="
echo "  Summary & Recommendations"
echo "========================================="
echo ""

if lsmod | grep -q vhci_hcd; then
    echo -e "${GREEN}System Ready:${NC} You can use AirUSB for network-level device communication"
    echo "  Build: mkdir build && cd build && cmake .. && make"
    echo "  Start server: sudo ./build/airusb-server"
    echo "  Connect client: ./build/airusb-client <server-ip> -l"
    echo ""
    echo "  Note: Full USB device integration requires protocol bridge"
    echo "        See docs/USB_INTEGRATION.md for details"
else
    echo -e "${YELLOW}Partial Setup:${NC} USB/IP kernel module not available"
    echo ""
    echo "  Option 1 - Standard USB/IP (Recommended for now):"
    echo "    Install: sudo apt-get install linux-tools-generic"
    echo "    Load modules: sudo modprobe vhci-hcd && sudo modprobe usbip-host"
    echo "    See docs/USB_INTEGRATION.md for complete workflow"
    echo ""
    echo "  Option 2 - AirUSB network protocol (experimental):"
    echo "    Works for device discovery and network communication"
    echo "    Kernel integration pending protocol bridge implementation"
fi

echo ""
echo "For detailed information: docs/USB_INTEGRATION.md"
echo "For usage instructions: docs/README.md"
echo ""
