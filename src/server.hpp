#pragma once

#include "protocol.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <queue>
#include <atomic>
#include <memory>
#include <libusb-1.0/libusb.h>

namespace airusb {

class UsbDevice {
public:
    UsbDevice(libusb_device* device);
    ~UsbDevice();
    
    // Device information
    DeviceInfo get_device_info() const;
    uint32_t get_device_id() const { return device_id_; }
    
    // Device operations
    bool open();
    void close();
    bool is_open() const { return handle_ != nullptr; }
    
    // Transfer operations
    int submit_urb(const UrbHeader& urb_header, const std::vector<uint8_t>& data);
    bool get_completed_urb(UrbHeader& urb_header, std::vector<uint8_t>& data);
    
private:
    struct PendingTransfer {
        UrbHeader header;
        libusb_transfer* transfer;
        std::vector<uint8_t> buffer;
        bool completed;
        
        PendingTransfer() : transfer(nullptr), completed(false) {}
    };
    
    libusb_device* device_;
    libusb_device_handle* handle_;
    uint32_t device_id_;
    DeviceInfo device_info_;
    
    std::mutex transfers_mutex_;
    std::unordered_map<uint64_t, std::unique_ptr<PendingTransfer>> pending_transfers_;
    std::queue<std::unique_ptr<PendingTransfer>> completed_transfers_;
    
    static void LIBUSB_CALL transfer_callback(libusb_transfer* transfer);
    void handle_completed_transfer(PendingTransfer* pending);
    
    static std::atomic<uint32_t> next_device_id_;
    static std::atomic<uint64_t> next_urb_id_;
};

class UsbServer {
public:
    UsbServer(uint16_t port = 3240);
    ~UsbServer();
    
    bool start();
    void stop();
    
    // Device management
    std::vector<DeviceInfo> get_available_devices();
    bool attach_device(uint32_t device_id);
    bool detach_device(uint32_t device_id);
    
private:
    struct ClientConnection {
        int socket_fd;
        std::thread thread;
        std::atomic<bool> running;
        std::unordered_map<uint32_t, std::shared_ptr<UsbDevice>> attached_devices;
        
        ClientConnection() : socket_fd(-1), running(false) {}
    };
    
    uint16_t port_;
    int server_socket_;
    std::atomic<bool> running_;
    
    // USB context
    libusb_context* usb_context_;
    std::thread usb_event_thread_;
    std::thread accept_thread_;
    
    // Device management
    std::mutex devices_mutex_;
    std::vector<std::shared_ptr<UsbDevice>> available_devices_;
    
    // Client connections
    std::mutex clients_mutex_;
    std::vector<std::unique_ptr<ClientConnection>> clients_;
    
    // Server operations
    bool init_usb();
    void cleanup_usb();
    void scan_devices();
    bool should_include_device(libusb_device* device);
    void usb_event_loop();
    
    // Network operations
    bool init_socket();
    void cleanup_socket();
    void accept_connections();
    void handle_client(ClientConnection* client);
    
    // Message handling
    void handle_device_list_request(ClientConnection* client, const Message& msg);
    void handle_device_attach_request(ClientConnection* client, const Message& msg);
    void handle_device_detach_request(ClientConnection* client, const Message& msg);
    void handle_usb_submit_urb(ClientConnection* client, const Message& msg);
    void handle_usb_unlink_urb(ClientConnection* client, const Message& msg);
    
    // Utility functions
    bool send_message(int socket_fd, const Message& msg);
    bool receive_message(int socket_fd, Message& msg);
    void send_error(int socket_fd, const std::string& error_msg);
};

// High-performance bulk data transfer for streaming
class BulkDataStreamer {
public:
    BulkDataStreamer(int socket_fd);
    
    // Start streaming data
    bool start_stream(uint64_t stream_id, const std::vector<uint8_t>& data, 
                     Compressor::Algorithm compression = Compressor::LZ4);
    
    // Receive streamed data
    bool receive_stream(uint64_t stream_id, std::vector<uint8_t>& data);
    
private:
    int socket_fd_;
    static constexpr size_t CHUNK_SIZE = 65536; // 64KB chunks
    
    bool send_bulk_chunk(const BulkDataHeader& header, const std::vector<uint8_t>& chunk);
    bool receive_bulk_chunk(BulkDataHeader& header, std::vector<uint8_t>& chunk);
};

} // namespace airusb