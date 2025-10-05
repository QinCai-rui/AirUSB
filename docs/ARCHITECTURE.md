# AirUSB Architecture

## Current Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Server (192.168.50.1)                 │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌───────────────┐         ┌──────────────┐                │
│  │ airusb-server │────────▶│   libusb     │                │
│  └───────┬───────┘         └──────┬───────┘                │
│          │                         │                         │
│          │                         ▼                         │
│          │                  ┌─────────────┐                 │
│          │                  │ USB Device  │                 │
│          │                  │  (e.g. SDA) │                 │
│          │                  └─────────────┘                 │
│          │                                                   │
│          │  Custom AirUSB Protocol                          │
│          │  (WiFi 6E Optimized)                             │
└──────────┼───────────────────────────────────────────────────┘
           │
           │  Network Layer
           │  - Custom message format with CRC32
           │  - Optional compression (LZ4/ZSTD)
           │  - High-speed bulk transfers
           │  - WiFi 6E QoS optimization
           │
           ▼
┌─────────────────────────────────────────────────────────────┐
│                        Client (192.168.50.90)                │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌───────────────┐                                          │
│  │ airusb-client │                                          │
│  └───────┬───────┘                                          │
│          │                                                   │
│          ▼                                                   │
│  ┌────────────────┐       ┌─────────────────┐             │
│  │ Network Comms  │       │ KernelUsbDriver │             │
│  │   ✓ Working    │       │  ⚠ Framework    │             │
│  └────────────────┘       └────────┬────────┘             │
│                                     │                        │
│                                     ▼                        │
│                            ┌─────────────────┐             │
│                            │   vhci_hcd      │             │
│                            │ (USB/IP kernel  │             │
│                            │     module)     │             │
│                            │  ⚠ Incompatible │             │
│                            │    Protocol     │             │
│                            └─────────────────┘             │
│                                     │                        │
│                                     ▼                        │
│                              ❌ No USB Device                │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

## The Problem

**Protocol Mismatch:**
- AirUSB uses custom protocol for performance
- vhci_hcd expects standard USB/IP protocol
- No translation layer between them

**Result:** Network communication works, but devices don't appear in system.

## Solution Options

### Option 1: Protocol Bridge (Recommended)

```
┌─────────────────────────────────────────────────────────────┐
│                          Client                              │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌───────────────┐                                          │
│  │ airusb-client │                                          │
│  └───────┬───────┘                                          │
│          │                                                   │
│          │ AirUSB Protocol                                  │
│          ▼                                                   │
│  ┌─────────────────────────────────────────────┐           │
│  │         Protocol Bridge Daemon              │           │
│  │  (airusb-bridge - TO BE IMPLEMENTED)        │           │
│  │                                              │           │
│  │  ┌──────────────┐      ┌─────────────────┐ │           │
│  │  │ AirUSB Side  │◀────▶│   USB/IP Side   │ │           │
│  │  │   (custom)   │      │   (standard)    │ │           │
│  │  └──────────────┘      └─────────────────┘ │           │
│  │                                              │           │
│  │  • Translates URB submissions               │           │
│  │  • Converts message formats                 │           │
│  │  • Manages device state                     │           │
│  └──────────────────┬───────────────────────────┘           │
│                     │                                        │
│                     │ USB/IP Protocol                        │
│                     ▼                                        │
│            ┌─────────────────┐                              │
│            │    vhci_hcd     │                              │
│            └────────┬────────┘                              │
│                     │                                        │
│                     ▼                                        │
│              ✅ USB Device                                   │
│              (appears in lsusb/lsblk)                       │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

**Advantages:**
- Uses existing vhci_hcd module
- No kernel modifications needed
- Maintains AirUSB performance benefits
- Userspace development (easier)

**Implementation:**
- Create socketpair for protocol translation
- Thread for AirUSB communication
- Thread for USB/IP communication
- URB translation logic

### Option 2: Custom Kernel Module

```
┌─────────────────────────────────────────────────────────────┐
│                          Client                              │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌───────────────┐                                          │
│  │ airusb-client │                                          │
│  └───────┬───────┘                                          │
│          │                                                   │
│          │ AirUSB Protocol                                  │
│          ▼                                                   │
│  ┌─────────────────────────────────────────────┐           │
│  │    airusb_hcd Kernel Module                 │           │
│  │    (TO BE DEVELOPED)                        │           │
│  │                                              │           │
│  │  • Native AirUSB protocol support           │           │
│  │  • Direct URB handling                      │           │
│  │  • Optimal performance                      │           │
│  │  • USB host controller driver               │           │
│  └──────────────────┬───────────────────────────┘           │
│                     │                                        │
│                     ▼                                        │
│              ✅ USB Device                                   │
│              (appears in lsusb/lsblk)                       │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

**Advantages:**
- Maximum performance
- No protocol translation overhead
- Direct kernel integration

**Disadvantages:**
- Requires kernel development expertise
- Harder to debug and maintain
- Module must be updated for new kernels

## Current Workaround

```
┌──────────────────┐          ┌──────────────────┐
│  Server          │          │  Client          │
│  (USB Host)      │          │                  │
│                  │          │                  │
│  ┌────────────┐  │          │  ┌────────────┐  │
│  │   usbipd   │  │          │  │   usbip    │  │
│  └──────┬─────┘  │          │  └──────┬─────┘  │
│         │        │          │         │        │
│         │ USB/IP Protocol   │         │        │
│         └────────┼──────────┼─────────┘        │
│                  │          │                  │
│  ┌────────────┐  │          │  ┌────────────┐  │
│  │   libusb   │  │          │  │ vhci_hcd   │  │
│  └──────┬─────┘  │          │  └──────┬─────┘  │
│         │        │          │         │        │
│         ▼        │          │         ▼        │
│   ┌─────────┐   │          │   ✅ USB Device  │
│   │USB Drive│   │          │   (in lsblk!)   │
│   └─────────┘   │          │                  │
│                  │          │                  │
└──────────────────┘          └──────────────────┘
```

**Usage:**
```bash
# Server
sudo usbip bind -b 5-1
sudo usbipd -D

# Client
sudo modprobe vhci-hcd
sudo usbip attach -r <server-ip> -b 5-1
lsblk  # Device appears!
```

## Message Flow: AirUSB Protocol

### Device List Request/Response
```
Client                          Server
  │                               │
  ├─ DEVICE_LIST_REQUEST ────────▶│
  │    (sequence #1)              │
  │                               │
  │◀─ DEVICE_LIST_RESPONSE ───────┤
  │    (sequence #1)              │
  │    [DeviceInfo array]         │
  │                               │
```

### Device Attach (Current Implementation)
```
Client                          Server
  │                               │
  ├─ DEVICE_ATTACH_REQUEST ──────▶│
  │    (device_id: 2)             │
  │                               │ (Opens USB device)
  │                               │ (Prepares for I/O)
  │                               │
  │◀─ DEVICE_ATTACH_RESPONSE ─────┤
  │    (success: 1)               │
  │                               │
  [Device info stored]            │
  [Network level attached]        │
  ❌ But no kernel USB device     │
  │                               │
```

### URB Transfer (Future Implementation)
```
Client                          Server
  │                               │
  ├─ USB_SUBMIT_URB ─────────────▶│
  │    (urb_id, type, data)       │
  │                               │ (libusb transfer)
  │                               │ (waits for completion)
  │                               │
  │◀─ USB_COMPLETE_URB ────────────┤
  │    (urb_id, status, data)     │
  │                               │
```

## Performance Characteristics

### AirUSB Protocol Features
- **Message Size**: 24-byte header + payload
- **Compression**: Optional LZ4/ZSTD for bulk data
- **CRC32**: Message integrity checking
- **Streaming**: Chunked transfer for large data
- **Optimization**: WiFi 6E QoS, 2MB buffers, TCP_NODELAY

### USB/IP Protocol
- **Standard**: Well-defined protocol
- **Simple**: Basic URB forwarding
- **Compatible**: Works with vhci_hcd
- **Performance**: Good, but not optimized for WiFi 6E

### Protocol Bridge Performance Impact
Expected overhead: 5-15% due to translation layer
Still faster than standard USB/IP on WiFi 6E due to optimizations.

## Development Roadmap

### Phase 1: Current State ✅
- Network communication working
- Device discovery working
- Framework in place

### Phase 2: Protocol Bridge 🚧
- Design bridge architecture
- Implement URB translation
- Test with storage devices
- Performance benchmarking

### Phase 3: Full Integration 📋
- Support all USB device classes
- Handle edge cases
- Optimize performance
- Production testing

### Phase 4: Advanced Features 🔮
- GUI configuration tool
- Multiple device support
- Load balancing
- Security enhancements

## Contributing

Interested in implementing the protocol bridge or kernel module?

1. Read this architecture document
2. Review `docs/USB_INTEGRATION.md` for technical details
3. Check `SOLUTION_SUMMARY.md` for current status
4. Open an issue to discuss your approach
5. Submit a PR with your implementation

## References

- [Linux USB/IP Documentation](https://www.kernel.org/doc/html/latest/usb/usbip_protocol.html)
- [USB/IP Protocol Specification](http://usbip.sourceforge.net/)
- [vhci_hcd Source](https://github.com/torvalds/linux/tree/master/drivers/usb/usbip)
- [libusb Documentation](https://libusb.info/)
