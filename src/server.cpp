#include "server.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cstdio>

namespace airusb {

std::atomic<uint32_t> UsbDevice::next_device_id_(1);
std::atomic<uint64_t> UsbDevice::next_urb_id_(1);

// UsbDevice implementation
UsbDevice::UsbDevice(libusb_device* device) 
    : device_(device), handle_(nullptr) {
    device_id_ = next_device_id_++;
    libusb_ref_device(device_);
    
    // Get device descriptor
    libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(device_, &desc) == 0) {
        device_info_.device_id = device_id_;
        device_info_.bus_id = libusb_get_bus_number(device_);
        device_info_.bus_num = libusb_get_bus_number(device_);
        device_info_.device_num = libusb_get_device_address(device_);
        // Get port path for busid
        uint8_t port_numbers[8];
        int path_length = libusb_get_port_numbers(device_, port_numbers, sizeof(port_numbers));
        device_info_.port_number = (path_length > 0) ? port_numbers[0] : device_info_.device_num;
        
        device_info_.vendor_id = desc.idVendor;
        device_info_.product_id = desc.idProduct;
        device_info_.device_class = desc.bDeviceClass;
        device_info_.device_subclass = desc.bDeviceSubClass;
        device_info_.device_protocol = desc.bDeviceProtocol;
        device_info_.configuration_value = desc.bNumConfigurations > 0 ? 1 : 0;
        device_info_.num_interfaces = 0; // Will be set when opened
        device_info_.device_speed = libusb_get_device_speed(device_);
        
        // Create busid string (format: bus-port)
        snprintf(device_info_.busid, sizeof(device_info_.busid), "%d-%d", 
                device_info_.bus_num, device_info_.port_number);
        
        // Clear string fields
        memset(device_info_.manufacturer, 0, sizeof(device_info_.manufacturer));
        memset(device_info_.product, 0, sizeof(device_info_.product));
        memset(device_info_.serial, 0, sizeof(device_info_.serial));
    }
}

UsbDevice::~UsbDevice() {
    close();
    libusb_unref_device(device_);
}

DeviceInfo UsbDevice::get_device_info() const {
    return device_info_;
}

bool UsbDevice::open() {
    if (handle_) return true;
    
    int ret = libusb_open(device_, &handle_);
    if (ret != LIBUSB_SUCCESS) {
        std::cerr << "Failed to open USB device: " << libusb_error_name(ret) << std::endl;
        return false;
    }
    
    // Get string descriptors if available
    libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(device_, &desc) == 0) {
        if (desc.iManufacturer > 0) {
            unsigned char buffer[256];
            if (libusb_get_string_descriptor_ascii(handle_, desc.iManufacturer, 
                                                  buffer, sizeof(buffer)) > 0) {
                strncpy(device_info_.manufacturer, (char*)buffer, 
                       sizeof(device_info_.manufacturer) - 1);
                device_info_.manufacturer[sizeof(device_info_.manufacturer) - 1] = '\0';
            }
        }
        if (desc.iProduct > 0) {
            unsigned char buffer[256];
            if (libusb_get_string_descriptor_ascii(handle_, desc.iProduct, 
                                                  buffer, sizeof(buffer)) > 0) {
                strncpy(device_info_.product, (char*)buffer, 
                       sizeof(device_info_.product) - 1);
                device_info_.product[sizeof(device_info_.product) - 1] = '\0';
            }
        }
        if (desc.iSerialNumber > 0) {
            unsigned char buffer[256];
            if (libusb_get_string_descriptor_ascii(handle_, desc.iSerialNumber, 
                                                  buffer, sizeof(buffer)) > 0) {
                strncpy(device_info_.serial, (char*)buffer, 
                       sizeof(device_info_.serial) - 1);
                device_info_.serial[sizeof(device_info_.serial) - 1] = '\0';
            }
        }
    }
    
    return true;
}

void UsbDevice::close() {
    if (handle_) {
        // Cancel all pending transfers
        std::lock_guard<std::mutex> lock(transfers_mutex_);
        for (auto& pair : pending_transfers_) {
            if (pair.second->transfer) {
                libusb_cancel_transfer(pair.second->transfer);
            }
        }
        
        libusb_close(handle_);
        handle_ = nullptr;
    }
}

int UsbDevice::submit_urb(const UrbHeader& urb_header, const std::vector<uint8_t>& data) {
    if (!handle_) return -1;
    
    auto pending = std::make_unique<PendingTransfer>();
    pending->header = urb_header;
    pending->buffer = data;
    
    // Allocate libusb transfer
    pending->transfer = libusb_alloc_transfer(0);
    if (!pending->transfer) return -1;
    
    // Set up transfer based on type
    switch (urb_header.type) {
        case UrbType::BULK:
            if (urb_header.direction == UrbDirection::OUT) {
                libusb_fill_bulk_transfer(pending->transfer, handle_, urb_header.endpoint,
                                        pending->buffer.data(), pending->buffer.size(),
                                        transfer_callback, pending.get(), 5000);
            } else {
                pending->buffer.resize(urb_header.transfer_length);
                libusb_fill_bulk_transfer(pending->transfer, handle_, urb_header.endpoint,
                                        pending->buffer.data(), pending->buffer.size(),
                                        transfer_callback, pending.get(), 5000);
            }
            break;
            
        case UrbType::INT:
            if (urb_header.direction == UrbDirection::OUT) {
                libusb_fill_interrupt_transfer(pending->transfer, handle_, urb_header.endpoint,
                                             pending->buffer.data(), pending->buffer.size(),
                                             transfer_callback, pending.get(), 5000);
            } else {
                pending->buffer.resize(urb_header.transfer_length);
                libusb_fill_interrupt_transfer(pending->transfer, handle_, urb_header.endpoint,
                                             pending->buffer.data(), pending->buffer.size(),
                                             transfer_callback, pending.get(), 5000);
            }
            break;
            
        case UrbType::CONTROL:
            // Control transfers need special handling
            pending->buffer.resize(LIBUSB_CONTROL_SETUP_SIZE + urb_header.transfer_length);
            libusb_fill_control_transfer(pending->transfer, handle_, 
                                       pending->buffer.data(), transfer_callback, 
                                       pending.get(), 5000);
            break;
            
        default:
            libusb_free_transfer(pending->transfer);
            return -1;
    }
    
    // Submit transfer
    std::lock_guard<std::mutex> lock(transfers_mutex_);
    uint64_t urb_id = urb_header.urb_id;
    pending_transfers_[urb_id] = std::move(pending);
    
    int ret = libusb_submit_transfer(pending_transfers_[urb_id]->transfer);
    if (ret != LIBUSB_SUCCESS) {
        pending_transfers_.erase(urb_id);
        return ret;
    }
    
    return 0;
}

bool UsbDevice::get_completed_urb(UrbHeader& urb_header, std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(transfers_mutex_);
    
    if (completed_transfers_.empty()) {
        return false;
    }
    
    auto completed = std::move(completed_transfers_.front());
    completed_transfers_.pop();
    
    urb_header = completed->header;
    data = std::move(completed->buffer);
    
    return true;
}

void LIBUSB_CALL UsbDevice::transfer_callback(libusb_transfer* transfer) {
    PendingTransfer* pending = static_cast<PendingTransfer*>(transfer->user_data);
    UsbDevice* device = nullptr; // We'll need to find the device from the pending transfer
    
    // Update transfer status
    pending->header.status = transfer->status;
    pending->header.transfer_length = transfer->actual_length;
    pending->completed = true;
    
    // Note: In a real implementation, we'd need a way to get back to the UsbDevice
    // from the transfer callback. This is a simplified version.
}

// UsbServer implementation
UsbServer::UsbServer(uint16_t port) : port_(port), server_socket_(-1), running_(false), usb_context_(nullptr) {
}

UsbServer::~UsbServer() {
    stop();
}

bool UsbServer::start() {
    if (running_) return true;
    
    // Initialize USB
    if (!init_usb()) {
        std::cerr << "Failed to initialize USB" << std::endl;
        return false;
    }
    
    // Initialize socket
    if (!init_socket()) {
        std::cerr << "Failed to initialize socket" << std::endl;
        cleanup_usb();
        return false;
    }
    
    running_ = true;
    
    // Start USB event loop
    usb_event_thread_ = std::thread([this]() { usb_event_loop(); });

    // Scan for devices
    scan_devices();

    // Accept connections (store thread so we can join on stop)
    accept_thread_ = std::thread([this]() { accept_connections(); });
    
    std::cout << "AirUSB server started on port " << port_ << std::endl;
    return true;
}

void UsbServer::stop() {
    if (!running_) return;
    
    running_ = false;
    
    // Stop USB event loop
    if (usb_event_thread_.joinable()) {
        usb_event_thread_.join();
    }

    // Close the listening socket to wake accept()
    cleanup_socket();

    // Join accept thread
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    // Close client connections (try graceful join, then force)
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& client : clients_) {
            client->running = false;
            if (client->socket_fd >= 0) {
                shutdown(client->socket_fd, SHUT_RDWR);
                close(client->socket_fd);
                client->socket_fd = -1;
            }
        }
        // Give threads a moment to exit
        for (auto& client : clients_) {
            if (client->thread.joinable()) {
                // wait up to 200ms for thread to finish
                for (int i = 0; i < 20 && client->thread.joinable(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                if (client->thread.joinable()) {
                    // If still joinable, detach to avoid blocking shutdown
                    client->thread.detach();
                }
            }
        }
        clients_.clear();
    }

        // Close and clear available devices to release libusb references
        {
            std::lock_guard<std::mutex> lock(devices_mutex_);
            for (auto& dev : available_devices_) {
                if (dev) {
                    dev->close();
                }
            }
            available_devices_.clear();
        }

        cleanup_usb();
    
    std::cout << "AirUSB server stopped" << std::endl;
}

bool UsbServer::init_usb() {
    int ret = libusb_init(&usb_context_);
    if (ret != LIBUSB_SUCCESS) {
        std::cerr << "Failed to initialize libusb: " << libusb_error_name(ret) << std::endl;
        return false;
    }
    
    libusb_set_debug(usb_context_, LIBUSB_LOG_LEVEL_WARNING);
    return true;
}

void UsbServer::cleanup_usb() {
    if (usb_context_) {
        libusb_exit(usb_context_);
        usb_context_ = nullptr;
    }
}

bool UsbServer::should_include_device(libusb_device* device) {
    libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(device, &desc) != 0) {
        return false;
    }
    
    // Filter out USB hubs (class 9)
    if (desc.bDeviceClass == LIBUSB_CLASS_HUB) {
        return false;
    }
    
    // Filter out Linux kernel USB/IP virtual controllers
    if (desc.idVendor == 0x1d6b && desc.bDeviceClass == 0) {
        return false;
    }
    
    // Filter out root hubs and other virtual controllers
    if (desc.idVendor == 0x1d6b && 
        (desc.idProduct == 0x0001 || desc.idProduct == 0x0002 || desc.idProduct == 0x0003)) {
        // Check if it's a root hub by examining the device path
        uint8_t port_numbers[8];
        int path_length = libusb_get_port_numbers(device, port_numbers, sizeof(port_numbers));
        if (path_length <= 0) {
            return false; // Root hub
        }
    }
    
    return true;
}

void UsbServer::scan_devices() {
    libusb_device** device_list;
    ssize_t device_count = libusb_get_device_list(usb_context_, &device_list);
    
    if (device_count < 0) {
        std::cerr << "Failed to get USB device list" << std::endl;
        return;
    }
    
    std::lock_guard<std::mutex> lock(devices_mutex_);
    available_devices_.clear();
    
    for (ssize_t i = 0; i < device_count; i++) {
        auto device = std::make_shared<UsbDevice>(device_list[i]);
        
        // Filter out devices like USBIP does
        if (!should_include_device(device_list[i])) {
            continue;
        }
        
        // Try to open device to get string descriptors
        if (device->open()) {
            // Device info is updated in open() method
            device->close(); // Close it for now, will reopen when attached
        }
        
        available_devices_.push_back(device);
    }
    
    libusb_free_device_list(device_list, 1);
    
    std::cout << "Found " << available_devices_.size() << " USB devices" << std::endl;
}

void UsbServer::usb_event_loop() {
    while (running_) {
        struct timeval tv = {0, 100000}; // 100ms timeout
        libusb_handle_events_timeout(usb_context_, &tv);
    }
}

bool UsbServer::init_socket() {
    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_ < 0) {
        perror("socket");
        return false;
    }
    
    // Enable address reuse
    int opt = 1;
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind socket
    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_);
    
    if (bind(server_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_socket_);
        server_socket_ = -1;
        return false;
    }
    
    // Listen for connections
    if (listen(server_socket_, 5) < 0) {
        perror("listen");
        close(server_socket_);
        server_socket_ = -1;
        return false;
    }
    
    return true;
}

void UsbServer::cleanup_socket() {
    if (server_socket_ >= 0) {
        close(server_socket_);
        server_socket_ = -1;
    }
}

void UsbServer::accept_connections() {
    // Ensure the server socket is non-blocking so select can wake up
    int flags = fcntl(server_socket_, F_GETFL, 0);
    if (flags >= 0) fcntl(server_socket_, F_SETFL, flags | O_NONBLOCK);

    while (running_) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_socket_, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000; // 200ms

        int rv = select(server_socket_ + 1, &readfds, NULL, NULL, &tv);
        if (rv < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        } else if (rv == 0) {
            continue; // timeout, check running_ again
        }

        if (!FD_ISSET(server_socket_, &readfds)) continue;

        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_socket = accept(server_socket_, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                continue;
            }
            if (running_) {
                perror("accept");
            }
            continue;
        }
        
        std::cout << "Client connected from " << inet_ntoa(client_addr.sin_addr) << std::endl;
        
        // Create client connection
        auto client = std::make_unique<ClientConnection>();
        client->socket_fd = client_socket;
        client->running = true;
        client->thread = std::thread([this, client = client.get()]() {
            handle_client(client);
        });
        
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.push_back(std::move(client));
    }
}

void UsbServer::handle_client(ClientConnection* client) {
    while (client->running && running_) {
        Message msg;
        if (!receive_message(client->socket_fd, msg)) {
            break;
        }
        
        switch (msg.header.type) {
            case MessageType::DEVICE_LIST_REQUEST:
                handle_device_list_request(client, msg);
                break;
            case MessageType::DEVICE_ATTACH_REQUEST:
                handle_device_attach_request(client, msg);
                break;
            case MessageType::DEVICE_DETACH_REQUEST:
                handle_device_detach_request(client, msg);
                break;
            case MessageType::USB_SUBMIT_URB:
                handle_usb_submit_urb(client, msg);
                break;
            case MessageType::USB_UNLINK_URB:
                handle_usb_unlink_urb(client, msg);
                break;
            default:
                send_error(client->socket_fd, "Unknown message type");
                break;
        }
    }
    
    std::cout << "Client disconnected" << std::endl;
    close(client->socket_fd);
    client->running = false;
}

void UsbServer::handle_device_list_request(ClientConnection* client, const Message& msg) {
    Message response(MessageType::DEVICE_LIST_RESPONSE, msg.header.sequence);
    
    std::lock_guard<std::mutex> lock(devices_mutex_);
    std::cout << "Sending device list with " << available_devices_.size() << " devices" << std::endl;
    
    for (const auto& device : available_devices_) {
        DeviceInfo info = device->get_device_info();
        response.add_payload(info);
        std::cout << "Added device: " << info.manufacturer << " " << info.product << std::endl;
    }
    
    if (!send_message(client->socket_fd, response)) {
        std::cerr << "Failed to send device list response" << std::endl;
    } else {
        std::cout << "Device list response sent successfully" << std::endl;
    }
}

void UsbServer::handle_device_attach_request(ClientConnection* client, const Message& msg) {
    if (msg.payload.size() < sizeof(uint32_t)) {
        send_error(client->socket_fd, "Invalid attach request");
        return;
    }
    
    uint32_t device_id = *reinterpret_cast<const uint32_t*>(msg.payload.data());
    
    // Find device (from cached list)
    std::shared_ptr<UsbDevice> device;
    {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        auto it = std::find_if(available_devices_.begin(), available_devices_.end(),
                              [device_id](const std::shared_ptr<UsbDevice>& d) {
                                  return d->get_device_id() == device_id;
                              });
        if (it != available_devices_.end()) {
            device = *it;
        }
    }
    
    Message response(MessageType::DEVICE_ATTACH_RESPONSE, msg.header.sequence);
    
    bool attach_ok = false;
    if (device) {
        if (device->open()) {
            attach_ok = true;
        } else {
            // Try to re-find the device in the current libusb device list and recreate the UsbDevice
            std::cerr << "Attempting to re-find device " << device_id << " after open() failure" << std::endl;
            libusb_device** device_list = nullptr;
            ssize_t cnt = libusb_get_device_list(usb_context_, &device_list);
            if (cnt > 0) {
                for (ssize_t i = 0; i < cnt; ++i) {
                    libusb_device* d = device_list[i];
                    libusb_device_descriptor desc;
                    if (libusb_get_device_descriptor(d, &desc) != 0) continue;

                    // Match by VID/PID and bus/address (best-effort)
                    if (desc.idVendor == device->get_device_info().vendor_id &&
                        desc.idProduct == device->get_device_info().product_id &&
                        libusb_get_bus_number(d) == device->get_device_info().bus_num &&
                        libusb_get_device_address(d) == device->get_device_info().device_num) {

                        // Create new UsbDevice from fresh libusb_device pointer
                        auto new_dev = std::make_shared<UsbDevice>(d);
                        if (new_dev->open()) {
                            // Replace cached device entry
                            std::lock_guard<std::mutex> lock(devices_mutex_);
                            auto it = std::find_if(available_devices_.begin(), available_devices_.end(),
                                [device_id](const std::shared_ptr<UsbDevice>& x) { return x->get_device_id() == device_id; });
                            if (it != available_devices_.end()) {
                                *it = new_dev;
                            } else {
                                available_devices_.push_back(new_dev);
                            }

                            device = new_dev;
                            attach_ok = true;
                            break;
                        } else {
                            // couldn't open this candidate, continue
                            continue;
                        }
                    }
                }

                libusb_free_device_list(device_list, 1);
            }
        }
    }

    if (attach_ok) {
        client->attached_devices[device_id] = device;
        uint32_t success = 1;
        response.add_payload(success);
    } else {
        uint32_t success = 0;
        response.add_payload(success);
    }
    
    send_message(client->socket_fd, response);
}

void UsbServer::handle_device_detach_request(ClientConnection* client, const Message& msg) {
    if (msg.payload.size() < sizeof(uint32_t)) {
        send_error(client->socket_fd, "Invalid detach request");
        return;
    }
    
    uint32_t device_id = *reinterpret_cast<const uint32_t*>(msg.payload.data());
    
    auto it = client->attached_devices.find(device_id);
    if (it != client->attached_devices.end()) {
        it->second->close();
        client->attached_devices.erase(it);
    }
    
    Message response(MessageType::DEVICE_DETACH_RESPONSE, msg.header.sequence);
    uint32_t success = 1;
    response.add_payload(success);
    
    send_message(client->socket_fd, response);
}

void UsbServer::handle_usb_submit_urb(ClientConnection* client, const Message& msg) {
    if (msg.payload.size() < sizeof(UrbHeader)) {
        send_error(client->socket_fd, "Invalid URB");
        return;
    }
    
    UrbHeader urb_header = *reinterpret_cast<const UrbHeader*>(msg.payload.data());
    
    auto device_it = client->attached_devices.find(urb_header.device_id);
    if (device_it == client->attached_devices.end()) {
        send_error(client->socket_fd, "Device not attached");
        return;
    }
    
    std::vector<uint8_t> urb_data;
    if (msg.payload.size() > sizeof(UrbHeader)) {
        urb_data.assign(msg.payload.begin() + sizeof(UrbHeader), msg.payload.end());
    }
    
    // Submit URB
    int ret = device_it->second->submit_urb(urb_header, urb_data);
    
    // For now, send immediate response (in real implementation, this would be async)
    Message response(MessageType::USB_COMPLETE_URB, msg.header.sequence);
    urb_header.status = ret;
    response.add_payload(urb_header);
    
    send_message(client->socket_fd, response);
}

void UsbServer::handle_usb_unlink_urb(ClientConnection* client, const Message& msg) {
    // Implementation for unlinking URBs
    Message response(MessageType::USB_COMPLETE_URB, msg.header.sequence);
    send_message(client->socket_fd, response);
}

bool UsbServer::send_message(int socket_fd, const Message& msg) {
    std::vector<uint8_t> data = msg.serialize();
    ssize_t sent = send(socket_fd, data.data(), data.size(), MSG_NOSIGNAL);
    return sent == static_cast<ssize_t>(data.size());
}

bool UsbServer::receive_message(int socket_fd, Message& msg) {
    // First, receive header
    MessageHeader header;
    ssize_t received = recv(socket_fd, &header, sizeof(header), MSG_WAITALL);
    if (received != sizeof(header)) {
        return false;
    }
    
    // Validate header
    if (header.magic != AIRUSB_MAGIC || header.version != PROTOCOL_VERSION) {
        return false;
    }
    
    // Receive payload
    std::vector<uint8_t> data(sizeof(header) + header.length);
    std::memcpy(data.data(), &header, sizeof(header));
    
    if (header.length > 0) {
        received = recv(socket_fd, data.data() + sizeof(header), header.length, MSG_WAITALL);
        if (received != static_cast<ssize_t>(header.length)) {
            return false;
        }
    }
    
    return msg.deserialize(data);
}

void UsbServer::send_error(int socket_fd, const std::string& error_msg) {
    Message error_response(MessageType::ERROR);
    error_response.add_payload_data(error_msg.c_str(), error_msg.length() + 1);
    send_message(socket_fd, error_response);
}

} // namespace airusb