# AirUSB
Connect USB devices wirelessly using ad-hoc WiFi 6E/7. High-performance USB over IP with custom protocol optimizations.

## Quick Start

**👉 See [QUICK_START.md](QUICK_START.md) for a complete step-by-step guide!**

Or check prerequisites first:
```bash
./scripts/check-prereqs.sh
```

See [docs/README.md](docs/README.md) for detailed documentation.

### Basic Usage

**Server Setup (one-time):**
```bash
# Run the automated setup script
sudo ./scripts/setup-usbip-server.sh

# This will:
# 1. Install USB/IP if needed
# 2. Load kernel modules
# 3. Let you select which device to share
# 4. Start usbipd daemon
```

**Server (running):**
```bash
# AirUSB server for device discovery (optional but recommended)
sudo ./airusb-server

# usbipd should be running from setup script
# Check with: ps aux | grep usbipd
```

**Client (list devices):**
```bash
./airusb-client <server-ip> -l
```

**Client (attach device):**
```bash
sudo ./airusb-client <server-ip> -a <busid>
# Device will appear in lsusb and lsblk!
```

**Client (use the device):**
```bash
# For storage devices
lsblk  # See the device (e.g., sda)
sudo mount /dev/sda1 /mnt  # Mount it

# For other devices
lsusb  # Verify it's there
# Use normally with any application
```

## Current Status

✅ **Fully Working:**
- High-performance network protocol (WiFi 6E optimized)
- Server: USB device discovery and management
- Client: Device listing and attachment
- **Full kernel USB integration** - devices appear in lsusb/lsblk!
- Server-client communication and synchronization

## How It Works

AirUSB uses a hybrid approach:
1. **AirUSB protocol** for device discovery and management (fast, optimized)
2. **USB/IP protocol** for kernel integration (standard, compatible)

This gives you the best of both worlds: AirUSB's performance optimizations with standard kernel compatibility.

## Building

```bash
# Install dependencies
sudo apt-get install build-essential cmake libusb-1.0-0-dev zlib1g-dev

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Contributing

We welcome contributions, especially for:
- USB/IP protocol bridge implementation
- Kernel module for native AirUSB protocol support
- Testing with various USB device types

See [docs/USB_INTEGRATION.md](docs/USB_INTEGRATION.md) for technical details.
