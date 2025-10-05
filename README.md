# AirUSB
Connect USB devices wirelessly using ad-hoc WiFi 6E/7. High-performance USB over IP with custom protocol optimizations.

## Quick Start

See [docs/README.md](docs/README.md) for detailed setup instructions.

### Basic Usage

**Server (USB device host):**
```bash
sudo ./airusb-server
```

**Client (list devices):**
```bash
./airusb-client <server-ip> -l
```

**Client (attach device):**
```bash
sudo ./airusb-client <server-ip> -a <busid>
```

## Current Status

✅ **Working:**
- High-performance network protocol (WiFi 6E optimized)
- Server: USB device discovery and management
- Client: Device listing and network-level attachment
- Server-client communication and synchronization

⚠️ **In Progress:**
- Full kernel USB device integration (vhci_hcd)
- Protocol bridge for USB/IP compatibility

## USB Device Integration

AirUSB uses a custom protocol optimized for WiFi 6E, which differs from standard USB/IP. This provides better performance but requires additional work for kernel integration.

**Current behavior:** When you attach a device, the network communication works, but the device won't appear in `lsusb` or `lsblk` yet.

**Workaround:** Use standard USB/IP for now (see [docs/USB_INTEGRATION.md](docs/USB_INTEGRATION.md))

**Future:** Full integration with protocol bridge (contributions welcome!)

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
