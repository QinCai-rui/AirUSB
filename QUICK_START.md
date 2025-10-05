# AirUSB Quick Start Guide

## What You Get

After following this guide, USB devices connected to your server will appear as local devices on your client machine. Storage drives will show up in `lsblk`, and you can mount and use them normally!

## Prerequisites

**Both machines need:**
- Linux (Ubuntu 20.04+ recommended)
- Root/sudo access

**Server needs:**
- USB devices physically connected
- USB/IP tools (script will install if missing)

**Client needs:**
- vhci_hcd kernel module support (usually available)

## Setup (5 minutes)

### 1. Build AirUSB

```bash
# Clone the repository (if not already done)
git clone https://github.com/QinCai-rui/AirUSB.git
cd AirUSB

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 2. Server Setup

On the machine with USB devices connected:

```bash
# Run the automated setup script
sudo ./scripts/setup-usbip-server.sh
```

The script will:
1. Install USB/IP if needed
2. Show you all available USB devices
3. Ask which device you want to share (enter the busid, e.g., "5-1")
4. Configure and start the USB/IP daemon

**Example:**
```
Available USB devices:
=========================================
 - busid 5-1 (13fe:6300)
   aigo : U330

 - busid 4-2 (413c:2113)
   Dell Inc. : Dell KB216 Wired Keyboard

Enter the busid of the device to share (e.g., 5-1): 5-1
✓ Device bound successfully
✓ usbipd started successfully
```

### 3. Client Usage

On the machine where you want to use the USB device:

```bash
cd AirUSB/build

# List available devices from server
./airusb-client 192.168.50.1 -l

# Attach a device (replace IP and busid with yours)
sudo ./airusb-client 192.168.50.1 -a 5-1
```

**You'll see:**
```
Connected to AirUSB server at 192.168.50.1:3240
Looking up device with busid 5-1...
Found device 2 with busid 5-1
Attaching device 2 (busid: 5-1)...
Device attached successfully at network level

Attaching device to kernel USB subsystem...
  Busid: 5-1
  Device: aigo U330
Loading vhci_hcd kernel module...
Attaching device via USB/IP...
✓ Device attached successfully!
  Check with: lsusb or lsblk

✓ Success! Device is now available on this system!
  You can now access it with: lsusb, lsblk, or mount

Press Ctrl+C to detach and exit
```

### 4. Use the Device

**For storage devices:**
```bash
# In another terminal on the client
lsblk
# You should see sda (or similar)!

# Mount it
sudo mkdir -p /mnt/usb
sudo mount /dev/sda1 /mnt/usb

# Use it
ls /mnt/usb
cp files /mnt/usb/

# Unmount when done
sudo umount /mnt/usb
```

**For other devices:**
```bash
# Verify with lsusb
lsusb
# You'll see your device listed!

# Use with any application
# The device appears as if it's locally connected
```

## Troubleshooting

### "Failed to attach device via USB/IP"

**Problem:** Server doesn't have usbipd running

**Solution:**
```bash
# On server:
sudo ./scripts/setup-usbip-server.sh
# Make sure it completes successfully
```

### "modprobe: FATAL: Module vhci-hcd not found"

**Problem:** Your kernel doesn't have USB/IP support

**Solution:**
```bash
# Install kernel modules
sudo apt-get install linux-tools-generic

# Or try:
sudo apt-get install linux-tools-$(uname -r)
```

### Device not showing in lsblk

**Check these:**

1. **Is usbipd running on server?**
   ```bash
   # On server:
   ps aux | grep usbipd
   # Should show a running process
   ```

2. **Is device bound on server?**
   ```bash
   # On server:
   usbip list -l
   # Your device should show "usbip-host"
   ```

3. **Is vhci_hcd loaded on client?**
   ```bash
   # On client:
   lsmod | grep vhci
   # Should show vhci_hcd module
   ```

4. **Check usbip port status:**
   ```bash
   # On client (while device is attached):
   sudo usbip port
   # Should show your device attached to a port
   ```

## Tips

1. **Keep the client running** - Press Ctrl+C when done to properly detach
2. **Server can share multiple devices** - Run setup script multiple times for different devices
3. **WiFi 6E recommended** - For best performance, use 6GHz WiFi (but works on any network)
4. **Performance** - Storage reads/writes work at network speed (typically 100-500 MB/s on WiFi 6E)

## What's Happening Behind the Scenes

1. **AirUSB Server** - Discovers USB devices and provides info
2. **usbipd** - Handles actual USB data transfer
3. **AirUSB Client** - Coordinates the attachment
4. **vhci_hcd** - Linux kernel module that makes remote USB appear local

This hybrid approach gives you both performance and compatibility!

## Next Steps

- Try different device types (keyboards, mice, cameras, etc.)
- Experiment with WiFi 6E for maximum performance
- Check `docs/` for advanced configuration options

## Support

If you encounter issues:
1. Check the troubleshooting section above
2. Run `./scripts/check-prereqs.sh` to verify your system
3. Check server and client logs for error messages
4. Open an issue on GitHub with detailed information
