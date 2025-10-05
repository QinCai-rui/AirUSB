# USB Device Integration

## Current Status

AirUSB implements a custom high-performance protocol optimized for WiFi 6E. However, this creates a challenge for kernel USB device integration, as the Linux kernel's standard USB/IP (vhci_hcd) expects devices to communicate using the standard USB/IP protocol.

## Architecture

### Current Implementation

1. **Server Side**: Discovers and manages USB devices via libusb
2. **Network Protocol**: Custom AirUSB protocol with compression and WiFi 6E optimizations
3. **Client Side**: Receives device information but needs kernel integration

### The Integration Challenge

The Linux kernel's USB/IP implementation (vhci_hcd) expects:
- A socket using the standard USB/IP protocol
- Direct kernel-to-server communication once attached

AirUSB uses a different, optimized protocol, which creates an incompatibility.

## Solutions

### Option 1: Protocol Bridge (Recommended for Production)

Create a userspace daemon that:
1. Communicates with AirUSB server using AirUSB protocol
2. Presents a standard USB/IP interface locally
3. Bridges between the two protocols

This allows using standard `usbip attach` commands while maintaining AirUSB's performance benefits.

### Option 2: Kernel Module (Maximum Performance)

Develop a custom kernel module that:
1. Understands AirUSB protocol directly
2. Registers as a USB host controller
3. Handles URBs natively

This provides maximum performance but requires kernel development.

### Option 3: Hybrid Approach (Current Implementation)

The current implementation attempts to use vhci_hcd but needs additional work:

1. **Prerequisites**:
   ```bash
   # Load vhci_hcd kernel module
   sudo modprobe vhci-hcd
   
   # Verify it loaded
   ls /sys/devices/platform/vhci_hcd.0/
   ```

2. **Known Limitations**:
   - Requires protocol adaptation layer (not yet implemented)
   - Socket management needs improvement
   - URB handling needs to be bridged between protocols

## Current Workaround

For immediate USB device access, you can use standard USB/IP as a temporary solution:

### Server Side (USB Host):
```bash
# Install usbip
sudo apt-get install usbip

# Load kernel modules
sudo modprobe usbip-core
sudo modprobe usbip-host

# List devices
usbip list -l

# Bind device (replace 5-1 with your device busid)
sudo usbip bind -b 5-1

# Start usbip daemon
sudo usbipd -D
```

### Client Side (USB Client):
```bash
# Install usbip
sudo apt-get install usbip

# Load vhci_hcd
sudo modprobe vhci-hcd

# List devices on server
usbip list -r <server-ip>

# Attach device
sudo usbip attach -r <server-ip> -b 5-1

# Verify
lsusb
lsblk  # For storage devices
```

## Next Steps for Full AirUSB Integration

1. **Implement Protocol Bridge**:
   - Create `airusb-bridge` daemon
   - Translates between AirUSB and USB/IP protocols
   - Maintains performance benefits where possible

2. **Improve Client Architecture**:
   - Keep persistent connection to server
   - Handle URB submission/completion
   - Manage device state properly

3. **Testing**:
   - Verify with various device types (storage, input, audio)
   - Performance benchmarking
   - Stability testing with long-running connections

## Technical Details

### USB/IP vhci_hcd Interface

The vhci_hcd expects writes to sysfs in this format:

**Attach**: `/sys/devices/platform/vhci_hcd.0/attach`
```
<port> <sockfd> <devid> <speed>
```
Where:
- `port`: Available vhci port number (0-7 typically)
- `sockfd`: File descriptor of connected socket
- `devid`: (busnum << 16) | devnum
- `speed`: USB speed (1=LOW, 2=FULL, 3=HIGH, 4=SUPER, etc.)

**Challenge**: The `sockfd` must be a socket already speaking USB/IP protocol.

### AirUSB Protocol Differences

AirUSB protocol features not in standard USB/IP:
- Custom message format with CRC32
- Optional compression (LZ4/ZSTD)
- Bulk data streaming optimization
- WiFi 6E specific optimizations (QoS, buffer sizes, etc.)

## Contributing

If you're interested in implementing the protocol bridge or kernel module, please:
1. Review the USB/IP protocol specification
2. Study the AirUSB protocol implementation in `src/protocol.cpp`
3. Open an issue to discuss your approach
4. Submit a pull request with your implementation

## References

- [Linux USB/IP Documentation](https://www.kernel.org/doc/html/latest/usb/usbip_protocol.html)
- [USB/IP Project](http://usbip.sourceforge.net/)
- [vhci_hcd Source Code](https://github.com/torvalds/linux/tree/master/drivers/usb/usbip)
