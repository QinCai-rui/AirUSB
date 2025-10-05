#include "client.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>

namespace airusb {

std::atomic<uint64_t> VirtualUsbDevice::next_urb_id_(1);

// VirtualUsbDevice implementation
VirtualUsbDevice::VirtualUsbDevice(const DeviceInfo& info, int server_socket) 
    : device_info_(info), server_socket_(server_socket), connected_(true) {
    
    response_thread_ = std::thread([this]() { handle_responses(); });
}

VirtualUsbDevice::~VirtualUsbDevice() {
    connected_ = false;
    if (response_thread_.joinable()) {
        response_thread_.join();
    }
}

int VirtualUsbDevice::submit_urb(uint64_t urb_id, UrbType type, UrbDirection direction, 
                                uint8_t endpoint, const std::vector<uint8_t>& data) {
    if (!connected_) return -1;
    
    UrbHeader header = {};
    header.urb_id = urb_id;
    header.device_id = device_info_.device_id;
    header.type = type;
    header.direction = direction;
    header.endpoint = endpoint;
    header.transfer_length = data.size();
    header.status = 0;
    
    // Store pending URB
    {
        std::lock_guard<std::mutex> lock(urbs_mutex_);
        PendingUrb& pending = pending_urbs_[urb_id];
        pending.urb_id = urb_id;
        pending.header = header;
        pending.data = data;
        pending.completed = false;
    }
    
    // Send to server
    if (!send_urb_to_server(header, data)) {
        std::lock_guard<std::mutex> lock(urbs_mutex_);
        pending_urbs_.erase(urb_id);
        return -1;
    }
    
    return 0;
}

bool VirtualUsbDevice::get_completed_urb(uint64_t& urb_id, int& status, std::vector<uint8_t>& data) {
    std::unique_lock<std::mutex> lock(urbs_mutex_);
    
    // Wait for completion with timeout
    if (!urb_completion_cv_.wait_for(lock, std::chrono::milliseconds(100), 
                                     [this] { return !completed_urbs_.empty(); })) {
        return false;
    }
    
    if (completed_urbs_.empty()) {
        return false;
    }
    
    PendingUrb completed = std::move(completed_urbs_.front());
    completed_urbs_.pop();
    
    urb_id = completed.urb_id;
    status = completed.header.status;
    data = std::move(completed.data);
    
    return true;
}

void VirtualUsbDevice::handle_responses() {
    while (connected_) {
        Message response;
        
        // This is simplified - in reality we'd need to coordinate with the client
        // to receive responses for this specific device
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool VirtualUsbDevice::send_urb_to_server(const UrbHeader& header, const std::vector<uint8_t>& data) {
    Message msg(MessageType::USB_SUBMIT_URB);
    msg.add_payload(header);
    msg.add_payload_data(data.data(), data.size());
    
    std::vector<uint8_t> serialized = msg.serialize();
    ssize_t sent = send(server_socket_, serialized.data(), serialized.size(), MSG_NOSIGNAL);
    
    return sent == static_cast<ssize_t>(serialized.size());
}

// UsbClient implementation
UsbClient::UsbClient() : socket_fd_(-1), connected_(false), sequence_number_(1) {
}

UsbClient::~UsbClient() {
    disconnect();
}

bool UsbClient::connect(const std::string& server_address, uint16_t port) {
    if (connected_) return true;
    
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        perror("socket");
        return false;
    }
    
    // Optimize socket for WiFi 6E
    NetworkOptimizer::optimize_socket_for_wifi6e(socket_fd_);
    
    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, server_address.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid server address: " << server_address << std::endl;
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    connected_ = true;
    
    // Don't start background message handler for synchronous operation
    // message_handler_thread_ = std::thread([this]() { handle_messages(); });
    
    std::cout << "Connected to AirUSB server at " << server_address << ":" << port << std::endl;
    return true;
}

void UsbClient::disconnect() {
    if (!connected_) return;
    
    connected_ = false;
    
    // Stop message handler
    if (message_handler_thread_.joinable()) {
        message_handler_thread_.join();
    }
    
    // Detach all devices
    {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        for (auto& pair : attached_devices_) {
            detach_device(pair.first);
        }
        attached_devices_.clear();
    }
    
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    
    std::cout << "Disconnected from AirUSB server" << std::endl;
}

std::vector<DeviceInfo> UsbClient::list_devices() {
    if (!connected_) return {};
    
    std::cout << "Sending device list request..." << std::endl;
    Message request(MessageType::DEVICE_LIST_REQUEST, get_next_sequence());
    if (!send_message(request)) {
        std::cout << "Failed to send device list request" << std::endl;
        return {};
    }
    
    std::cout << "Waiting for device list response..." << std::endl;
    Message response;
    if (!receive_message(response)) {
        std::cout << "Failed to receive device list response" << std::endl;
        return {};
    }
    
    std::vector<DeviceInfo> devices;
    if (response.header.type == MessageType::DEVICE_LIST_RESPONSE) {
        std::cout << "Received device list response with " << response.payload.size() << " bytes" << std::endl;
        size_t device_count = response.payload.size() / sizeof(DeviceInfo);
        for (size_t i = 0; i < device_count; i++) {
            const DeviceInfo* info = reinterpret_cast<const DeviceInfo*>(
                response.payload.data() + i * sizeof(DeviceInfo));
            devices.push_back(*info);
        }
    }
    
    return devices;
}

bool UsbClient::attach_device(uint32_t device_id) {
    if (!connected_) return false;
    
    Message request(MessageType::DEVICE_ATTACH_REQUEST, get_next_sequence());
    request.add_payload(device_id);
    
    if (!send_message(request)) {
        std::cerr << "Failed to send attach request" << std::endl;
        return false;
    }
    
    // Wait for attach response from server
    Message response;
    if (!receive_message(response)) {
        std::cerr << "Failed to receive attach response" << std::endl;
        return false;
    }
    
    if (response.header.type != MessageType::DEVICE_ATTACH_RESPONSE) {
        std::cerr << "Unexpected response type: " << static_cast<int>(response.header.type) << std::endl;
        return false;
    }
    
    if (response.payload.size() < sizeof(uint32_t)) {
        std::cerr << "Invalid attach response payload" << std::endl;
        return false;
    }
    
    uint32_t success = *reinterpret_cast<const uint32_t*>(response.payload.data());
    if (!success) {
        std::cerr << "Server failed to attach device" << std::endl;
        return false;
    }
    
    return true;
}

bool UsbClient::detach_device(uint32_t device_id) {
    if (!connected_) return false;
    
    Message request(MessageType::DEVICE_DETACH_REQUEST, get_next_sequence());
    request.add_payload(device_id);
    
    if (!send_message(request)) {
        return false;
    }
    
    // Remove from attached devices
    std::lock_guard<std::mutex> lock(devices_mutex_);
    attached_devices_.erase(device_id);
    
    return true;
}

std::vector<std::shared_ptr<VirtualUsbDevice>> UsbClient::get_attached_devices() {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    
    std::vector<std::shared_ptr<VirtualUsbDevice>> devices;
    for (const auto& pair : attached_devices_) {
        devices.push_back(pair.second);
    }
    
    return devices;
}

uint32_t UsbClient::find_device_by_busid(const std::string& busid) {
    if (!connected_) return 0;
    
    std::cout << "Finding device with busid: " << busid << std::endl;
    
    Message request(MessageType::DEVICE_LIST_REQUEST, get_next_sequence());
    if (!send_message(request)) {
        std::cout << "Failed to send device list request" << std::endl;
        return 0;
    }
    
    Message response;
    if (!receive_message(response)) {
        std::cout << "Failed to receive device list response" << std::endl;
        return 0;
    }
    
    if (response.header.type == MessageType::DEVICE_LIST_RESPONSE) {
        size_t device_count = response.payload.size() / sizeof(DeviceInfo);
        for (size_t i = 0; i < device_count; i++) {
            const DeviceInfo* info = reinterpret_cast<const DeviceInfo*>(
                response.payload.data() + i * sizeof(DeviceInfo));
            
            if (std::string(info->busid) == busid) {
                std::cout << "Found device " << info->device_id << " with busid " << busid << std::endl;
                return info->device_id;
            }
        }
    }
    
    std::cout << "Device with busid " << busid << " not found" << std::endl;
    return 0;
}

bool UsbClient::start_bulk_stream(uint32_t device_id, uint8_t endpoint, std::vector<uint8_t>& data) {
    // Implementation for high-speed bulk streaming
    // This would use the BulkDataStreamer class for optimal performance
    return false;
}

void UsbClient::handle_messages() {
    while (connected_) {
        Message msg;
        if (!receive_message(msg)) {
            if (connected_) {
                std::cerr << "Failed to receive message from server" << std::endl;
                connected_ = false;
            }
            break;
        }
        
        switch (msg.header.type) {
            case MessageType::DEVICE_LIST_RESPONSE:
                handle_device_list_response(msg);
                break;
            case MessageType::DEVICE_ATTACH_RESPONSE:
                handle_device_attach_response(msg);
                break;
            case MessageType::USB_COMPLETE_URB:
                handle_usb_complete_urb(msg);
                break;
            case MessageType::ERROR:
                handle_error(msg);
                break;
            default:
                std::cerr << "Unknown message type: " << static_cast<int>(msg.header.type) << std::endl;
                break;
        }
    }
}

bool UsbClient::send_message(const Message& msg) {
    if (!connected_) return false;
    
    std::vector<uint8_t> data = msg.serialize();
    ssize_t sent = send(socket_fd_, data.data(), data.size(), MSG_NOSIGNAL);
    
    return sent == static_cast<ssize_t>(data.size());
}

bool UsbClient::receive_message(Message& msg) {
    if (!connected_) return false;
    
    // Receive header first
    MessageHeader header;
    ssize_t received = recv(socket_fd_, &header, sizeof(header), MSG_WAITALL);
    if (received != sizeof(header)) {
        return false;
    }
    
    // Validate header
    if (header.magic != AIRUSB_MAGIC || header.version != PROTOCOL_VERSION) {
        return false;
    }
    
    // Receive full message
    std::vector<uint8_t> data(sizeof(header) + header.length);
    std::memcpy(data.data(), &header, sizeof(header));
    
    if (header.length > 0) {
        received = recv(socket_fd_, data.data() + sizeof(header), header.length, MSG_WAITALL);
        if (received != static_cast<ssize_t>(header.length)) {
            return false;
        }
    }
    
    return msg.deserialize(data);
}

void UsbClient::handle_device_list_response(const Message& msg) {
    std::cout << "Received device list with " << (msg.payload.size() / sizeof(DeviceInfo)) << " devices" << std::endl;
    
    // Parse device list
    size_t device_count = msg.payload.size() / sizeof(DeviceInfo);
    if (device_count == 0) {
        std::cout << "No USB devices found on server." << std::endl;
        return;
    }
    
    std::cout << "\nAvailable USB devices:" << std::endl;
    for (size_t i = 0; i < device_count; i++) {
        const DeviceInfo* info = reinterpret_cast<const DeviceInfo*>(
            msg.payload.data() + i * sizeof(DeviceInfo));
        
        std::cout << " - busid " << info->busid << " (" 
                  << std::hex << info->vendor_id << ":" << info->product_id << ")" << std::dec << std::endl;
        std::cout << "   " << (strlen(info->manufacturer) > 0 ? info->manufacturer : "Unknown Manufacturer")
                  << " : " << (strlen(info->product) > 0 ? info->product : "Unknown Product")
                  << " (" << std::hex << info->vendor_id << ":" << info->product_id << ")" << std::dec << std::endl;
        std::cout << std::endl;
    }
}

void UsbClient::handle_device_attach_response(const Message& msg) {
    if (msg.payload.size() >= sizeof(uint32_t)) {
        uint32_t success = *reinterpret_cast<const uint32_t*>(msg.payload.data());
        if (success) {
            std::cout << "Device attached successfully" << std::endl;
        } else {
            std::cout << "Failed to attach device" << std::endl;
        }
    }
}

void UsbClient::handle_usb_complete_urb(const Message& msg) {
    if (msg.payload.size() >= sizeof(UrbHeader)) {
        const UrbHeader* header = reinterpret_cast<const UrbHeader*>(msg.payload.data());
        
        // Find the virtual device and notify about completion
        std::lock_guard<std::mutex> lock(devices_mutex_);
        auto device_it = attached_devices_.find(header->device_id);
        if (device_it != attached_devices_.end()) {
            // In a complete implementation, we'd notify the virtual device
            // about the completed URB
        }
    }
}

void UsbClient::handle_error(const Message& msg) {
    std::string error_msg(reinterpret_cast<const char*>(msg.payload.data()), 
                         msg.payload.size());
    std::cerr << "Server error: " << error_msg << std::endl;
}

// NetworkOptimizer implementation
void NetworkOptimizer::optimize_socket_for_wifi6e(int socket_fd) {
    set_high_priority_qos(socket_fd);
    enable_tcp_no_delay(socket_fd);
    set_large_buffers(socket_fd);
}

void NetworkOptimizer::set_high_priority_qos(int socket_fd) {
    int priority = WIFI6E_PRIORITY;
    setsockopt(socket_fd, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority));
    
    // Set Type of Service for real-time traffic
    int tos = 0x10; // Minimize delay
    setsockopt(socket_fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
}

void NetworkOptimizer::enable_tcp_no_delay(int socket_fd) {
    int flag = 1;
    setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
}

void NetworkOptimizer::set_large_buffers(int socket_fd) {
    int buffer_size = WIFI6E_BUFFER_SIZE;
    setsockopt(socket_fd, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
}

void NetworkOptimizer::enable_multipath_tcp(int socket_fd) {
    // Enable MPTCP if available (Linux 5.6+)
    int mptcp = 1;
    setsockopt(socket_fd, IPPROTO_TCP, TCP_ULP, "mptcp", 5);
}

// KernelUsbDriver implementation using USB/IP vhci_hcd
KernelUsbDriver::KernelUsbDriver() : kernel_initialized_(false), hcd_driver_(nullptr) {
}

KernelUsbDriver::~KernelUsbDriver() {
    cleanup();
}

bool KernelUsbDriver::initialize() {
    if (kernel_initialized_) return true;
    
    std::cout << "Initializing kernel USB driver interface" << std::endl;
    
    // Check if vhci_hcd module is loaded
    if (!check_vhci_hcd_loaded()) {
        std::cerr << "Warning: vhci_hcd kernel module not loaded" << std::endl;
        std::cerr << "Please load it with: sudo modprobe vhci-hcd" << std::endl;
        // Continue anyway for systems where it may load automatically
    }
    
    kernel_initialized_ = true;
    return true;
}

void KernelUsbDriver::cleanup() {
    if (!kernel_initialized_) return;
    
    // Detach all registered devices
    {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        for (const auto& pair : kernel_devices_) {
            detach_from_vhci(pair.first);
        }
        kernel_devices_.clear();
    }
    
    std::cout << "Cleaning up kernel USB driver interface" << std::endl;
    kernel_initialized_ = false;
}

bool KernelUsbDriver::register_device(std::shared_ptr<VirtualUsbDevice> device) {
    if (!kernel_initialized_) return false;
    
    const DeviceInfo& info = device->get_info();
    
    // Attach device to vhci_hcd
    if (!attach_to_vhci(device)) {
        std::cerr << "Failed to attach device " << device->get_device_id() 
                  << " to vhci_hcd" << std::endl;
        return false;
    }
    
    std::lock_guard<std::mutex> lock(devices_mutex_);
    kernel_devices_[device->get_device_id()] = device;
    
    std::cout << "Virtual USB device registered with kernel" << std::endl;
    std::cout << "  Device ID: " << device->get_device_id() << std::endl;
    std::cout << "  Busid: " << info.busid << std::endl;
    std::cout << "  Vendor: " << std::hex << info.vendor_id << std::dec << std::endl;
    std::cout << "  Product: " << std::hex << info.product_id << std::dec << std::endl;
    
    return true;
}

bool KernelUsbDriver::unregister_device(uint32_t device_id) {
    if (!kernel_initialized_) return false;
    
    std::lock_guard<std::mutex> lock(devices_mutex_);
    auto it = kernel_devices_.find(device_id);
    if (it != kernel_devices_.end()) {
        detach_from_vhci(device_id);
        kernel_devices_.erase(it);
        std::cout << "Unregistered virtual USB device " << device_id 
                  << " from kernel" << std::endl;
        return true;
    }
    
    return false;
}

bool KernelUsbDriver::check_vhci_hcd_loaded() {
    // Check if vhci_hcd sysfs directory exists
    std::ifstream status("/sys/devices/platform/vhci_hcd.0/status");
    if (!status.is_open()) {
        // Try alternative paths
        status.open("/sys/devices/platform/vhci_hcd/status");
    }
    return status.is_open();
}

int KernelUsbDriver::find_free_vhci_port() {
    std::ifstream status("/sys/devices/platform/vhci_hcd.0/status");
    if (!status.is_open()) {
        status.open("/sys/devices/platform/vhci_hcd/status");
    }
    
    if (!status.is_open()) {
        std::cerr << "Failed to open vhci_hcd status file" << std::endl;
        return -1;
    }
    
    std::string line;
    // Skip header lines
    std::getline(status, line);
    std::getline(status, line);
    
    int port = 0;
    while (std::getline(status, line)) {
        // Status format: "port sta spd dev sockfd local_busid"
        // We're looking for ports with status "4" (available)
        std::istringstream iss(line);
        int port_num, sta;
        if (iss >> port_num >> sta) {
            if (sta == 4) { // USB_PORT_STAT_AVAILABLE
                return port_num;
            }
        }
        port++;
    }
    
    return -1;
}

bool KernelUsbDriver::attach_to_vhci(std::shared_ptr<VirtualUsbDevice> device) {
    const DeviceInfo& info = device->get_info();
    
    std::cerr << "\n=== USB Kernel Integration Status ===" << std::endl;
    std::cerr << "Device successfully attached at protocol level:" << std::endl;
    std::cerr << "  Busid: " << info.busid << std::endl;
    std::cerr << "  Vendor:Product: " << std::hex << info.vendor_id << ":" << info.product_id << std::dec << std::endl;
    std::cerr << "  Description: " << info.manufacturer << " " << info.product << std::endl;
    std::cerr << "\nNote: Full kernel USB integration (vhci_hcd) requires protocol translation." << std::endl;
    std::cerr << "AirUSB protocol differs from standard USB/IP for performance optimization." << std::endl;
    std::cerr << "\nFor immediate USB device access, use standard USB/IP:" << std::endl;
    std::cerr << "  Server: sudo usbip bind -b " << info.busid << " && sudo usbipd -D" << std::endl;
    std::cerr << "  Client: sudo usbip attach -r <server-ip> -b " << info.busid << std::endl;
    std::cerr << "\nSee docs/USB_INTEGRATION.md for more details." << std::endl;
    std::cerr << "======================================\n" << std::endl;
    
    // TODO: Implement protocol bridge for full integration
    // This requires:
    // 1. Creating a socketpair for protocol translation
    // 2. One socket speaks USB/IP protocol to vhci_hcd
    // 3. Other socket translates to/from AirUSB protocol
    // 4. Background thread handles URB forwarding
    
    return false;
}

bool KernelUsbDriver::detach_from_vhci(uint32_t device_id) {
    auto it = vhci_port_map_.find(device_id);
    if (it == vhci_port_map_.end()) {
        return false;
    }
    
    int port = it->second;
    
    // Write to vhci_hcd detach file
    std::ofstream detach("/sys/devices/platform/vhci_hcd.0/detach");
    if (!detach.is_open()) {
        detach.open("/sys/devices/platform/vhci_hcd/detach");
    }
    
    if (!detach.is_open()) {
        std::cerr << "Failed to open vhci_hcd detach file" << std::endl;
        return false;
    }
    
    detach << port << std::endl;
    detach.close();
    
    vhci_port_map_.erase(it);
    
    std::cout << "Detached device from vhci port " << port << std::endl;
    return true;
}

} // namespace airusb