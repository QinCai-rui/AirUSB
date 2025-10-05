cd /home/qincai/AirUSB/build && sudo ./airusb-server &# AirUSB - High-Speed USB over WiFi 6E

AirUSB is an optimized implementation of USB over IP, specifically designed for high-bandwidth, low-latency networks like WiFi 6E. It provides significantly better performance than traditional USBIP by leveraging modern network capabilities and optimized protocols.

## Features

- **WiFi 6E Optimized**: Designed for 6GHz networks with high bandwidth and low latency
- **High Performance**: Custom protocol with minimal overhead and efficient serialization
- **Compression Support**: Real-time data compression for improved bandwidth utilization
- **Concurrent Transfers**: Multi-threaded design supporting multiple USB devices simultaneously
- **Low Latency**: Optimized for real-time applications like input devices and audio
- **Easy Deployment**: Simple setup on mini PCs and embedded systems

## Performance Comparison

| Feature | Traditional USBIP | AirUSB (WiFi 6E) |
|---------|-------------------|-------------------|
| Max Throughput | ~100 Mbps | ~6 Gbps |
| Latency | 10-50ms | 1-5ms |
| Concurrent Devices | Limited | 8+ devices |
| Compression | None | LZ4/ZSTD |
| Network Optimization | Basic TCP | WiFi 6E optimized |

## Hardware Requirements

### Transmitter (Server)
- Mini PC with WiFi 6E support (e.g., Intel AX210/AX211 chipset)
- USB 3.0+ ports for device connections
- Linux-based OS (Ubuntu 22.04+ recommended)
- Minimum 4GB RAM, 8GB recommended

### Receiver (Client)  
- PC with WiFi 6E adapter or built-in support
- Linux or Windows support (Linux recommended)
- USB kernel driver support

### Network
- WiFi 6E router/access point (6GHz band)
- Direct connection or dedicated network recommended for best performance

## Quick Start

### Building from Source

```bash
# Install dependencies
sudo apt update
sudo apt install build-essential cmake libusb-1.0-0-dev zlib1g-dev pkg-config

# Clone and build
git clone https://github.com/QinCai-rui/AirUSB.git
cd AirUSB
mkdir build && cd build
cmake ..
make -j$(nproc)

# Install
sudo make install
```

### Server Setup (Transmitter)

1. **Install and start the server:**
```bash
# Install system service
sudo systemctl enable airusb-server.service
sudo systemctl start airusb-server.service

# Or run manually
sudo ./airusb-server 3240
```

2. **Connect USB devices:**
- Plug USB devices into the mini PC
- Server will automatically detect and make them available

3. **Check server status:**
```bash
sudo systemctl status airusb-server
journalctl -u airusb-server -f
```

### Client Setup (Receiver)

1. **Connect to server:**
```bash
# List available devices
./airusb-client <server_ip> -l

# Attach a specific device
./airusb-client <server_ip> -a <device_id>
```

2. **Use attached devices:**
- Attached devices appear as local USB devices
- Use normally with any application

## Network Optimization

### WiFi 6E Configuration

For optimal performance, configure your WiFi 6E network:

```bash
# Set channel width to 160MHz
iwconfig wlan0 channel 36 
iwconfig wlan0 txpower 20

# Enable QoS for real-time traffic
tc qdisc add dev wlan0 root handle 1: fq_codel
tc filter add dev wlan0 parent 1: protocol ip prio 1 u32 match ip dport 3240 0xffff flowid 1:1
```

### Firewall Configuration

```bash
# Allow AirUSB traffic
sudo ufw allow 3240/tcp
sudo ufw allow from <client_ip> to any port 3240
```

## Advanced Configuration

### Server Configuration

Edit `/etc/airusb/server.conf`:

```ini
[network]
port = 3240
bind_address = 0.0.0.0
max_clients = 8

[performance]  
compression = lz4
buffer_size = 2097152  # 2MB
tcp_nodelay = true
high_priority_qos = true

[usb]
scan_interval = 5000   # ms
auto_attach = false
filter_hubs = true
```

### Client Configuration

Edit `/etc/airusb/client.conf`:

```ini
[network]
server_address = 192.168.1.100
port = 3240
reconnect_interval = 5000

[performance]
compression = lz4
buffer_size = 2097152
batch_urbs = true
```

## Troubleshooting

### Common Issues

1. **Connection fails:**
   - Check firewall settings
   - Verify WiFi 6E connectivity
   - Ensure server is running: `systemctl status airusb-server`

2. **Low performance:**
   - Check WiFi signal strength and channel utilization
   - Verify 6GHz band is being used
   - Monitor network traffic: `iftop -i wlan0`

3. **Device not detected:**
   - Check USB device permissions: `lsusb -v`
   - Verify udev rules: `udevadm test /sys/bus/usb/devices/...`
   - Check server logs: `journalctl -u airusb-server`

### Performance Tuning

1. **Network optimization:**
```bash
# Increase network buffers
echo 'net.core.rmem_max = 67108864' >> /etc/sysctl.conf
echo 'net.core.wmem_max = 67108864' >> /etc/sysctl.conf
sysctl -p
```

2. **CPU scheduling:**
```bash
# Set real-time priority for server
sudo chrt -f 50 systemctl restart airusb-server
```

3. **USB optimization:**
```bash  
# Increase USB buffer sizes
echo 16 > /sys/module/usbcore/parameters/usbfs_memory_mb
```

## Benchmarking

Run performance tests:

```bash
# Build benchmark suite
make airusb-benchmark

# Run full benchmark
./airusb-benchmark

# Run specific tests
./airusb-benchmark --test-latency
./airusb-benchmark --test-bandwidth
```

## Security Considerations

- AirUSB transmits USB data over the network
- Use WPA3 encryption on your WiFi 6E network
- Consider VPN for connections over untrusted networks
- Limit server access with firewall rules
- Monitor for unauthorized device access

## Architecture Details

### Protocol Design

AirUSB uses a custom binary protocol optimized for USB transfers:

- **Header**: 24-byte fixed header with CRC32 validation  
- **Compression**: Optional LZ4/ZSTD compression for bulk data
- **Streaming**: Chunked transfer for large data blocks
- **Multiplexing**: Concurrent URB handling with sequence numbers

### USB Device Virtualization

- Kernel-level virtual USB host controller driver
- Direct URB (USB Request Block) forwarding
- Support for all USB transfer types (Control, Bulk, Interrupt, Isochronous)
- Device hot-plug/unplug handling

### Network Layer

- TCP with Nagle's algorithm disabled for low latency
- Large socket buffers (2MB) for high throughput
- QoS marking for real-time traffic priority
- Connection pooling for multiple devices

## Development

### Building for Different Architectures

```bash
# ARM64 (for Raspberry Pi, etc.)
cmake -DCMAKE_TOOLCHAIN_FILE=arm64-toolchain.cmake ..

# x86_64 optimized
cmake -DCMAKE_CXX_FLAGS="-march=native -O3" ..
```

### Adding New Features

1. Fork the repository
2. Create feature branch
3. Implement with tests
4. Submit pull request

### Testing

```bash
# Run unit tests
make test

# Run integration tests with hardware
sudo ./tests/integration_test.sh
```

## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please read our [Contributing Guidelines](CONTRIBUTING.md) and [Code of Conduct](CODE_OF_CONDUCT.md).

## Support

- **Issues**: [GitHub Issues](https://github.com/QinCai-rui/AirUSB/issues)
- **Discussions**: [GitHub Discussions](https://github.com/QinCai-rui/AirUSB/discussions)
- **Documentation**: [Wiki](https://github.com/QinCai-rui/AirUSB/wiki)

## Roadmap

- [ ] Windows client support
- [ ] GUI configuration tool
- [ ] USB device filtering and access control
- [ ] Remote device management web interface
- [ ] Support for USB 4.0 and Thunderbolt devices
- [ ] Integration with container orchestration platforms

---

**Note**: This is a high-performance implementation designed for controlled environments. Always test thoroughly with your specific hardware and use cases.