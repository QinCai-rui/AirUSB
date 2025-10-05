#pragma once

#include "protocol.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <atomic>
#include <memory>

namespace airusb {

class VirtualUsbDevice {
public:
    VirtualUsbDevice(const DeviceInfo& info, int server_socket);
    ~VirtualUsbDevice();
    
    const DeviceInfo& get_info() const { return device_info_; }
    uint32_t get_device_id() const { return device_info_.device_id; }
    
    // USB operations (called by kernel driver)
    int submit_urb(uint64_t urb_id, UrbType type, UrbDirection direction, 
                   uint8_t endpoint, const std::vector<uint8_t>& data);
    bool get_completed_urb(uint64_t& urb_id, int& status, std::vector<uint8_t>& data);
    
    bool is_connected() const { return connected_; }
    
private:
    DeviceInfo device_info_;
    int server_socket_;
    std::atomic<bool> connected_;
    
    struct PendingUrb {
        uint64_t urb_id;
        UrbHeader header;
        std::vector<uint8_t> data;
        bool completed;
        
        PendingUrb() : urb_id(0), completed(false) {}
    };
    
    std::mutex urbs_mutex_;
    std::unordered_map<uint64_t, PendingUrb> pending_urbs_;
    std::queue<PendingUrb> completed_urbs_;
    std::condition_variable urb_completion_cv_;
    
    std::thread response_thread_;
    void handle_responses();
    
    bool send_urb_to_server(const UrbHeader& header, const std::vector<uint8_t>& data);
    
    static std::atomic<uint64_t> next_urb_id_;
};

class UsbClient {
public:
    UsbClient();
    ~UsbClient();
    
    // Connection management
    bool connect(const std::string& server_address, uint16_t port = 3240);
    void disconnect();
    bool is_connected() const { return connected_; }
    
    // Device discovery and management
    std::vector<DeviceInfo> list_devices();
    bool attach_device(uint32_t device_id);
    bool detach_device(uint32_t device_id);
    
    // Get attached virtual devices
    std::vector<std::shared_ptr<VirtualUsbDevice>> get_attached_devices();
    
    // Find device by busid and get its ID
    uint32_t find_device_by_busid(const std::string& busid);
    
    // High-speed bulk transfer optimization
    bool start_bulk_stream(uint32_t device_id, uint8_t endpoint, 
                          std::vector<uint8_t>& data);
    
private:
    int socket_fd_;
    std::atomic<bool> connected_;
    std::atomic<uint32_t> sequence_number_;
    
    std::mutex devices_mutex_;
    std::unordered_map<uint32_t, std::shared_ptr<VirtualUsbDevice>> attached_devices_;
    
    std::thread message_handler_thread_;
    void handle_messages();
    
    // Message communication
    bool send_message(const Message& msg);
    bool receive_message(Message& msg);
    
    // Message handlers
    void handle_device_list_response(const Message& msg);
    void handle_device_attach_response(const Message& msg);
    void handle_usb_complete_urb(const Message& msg);
    void handle_error(const Message& msg);
    
    uint32_t get_next_sequence() { return sequence_number_++; }
};

// Linux kernel interface for virtual USB devices
class KernelUsbDriver {
public:
    KernelUsbDriver();
    ~KernelUsbDriver();
    
    bool initialize();
    void cleanup();
    
    // Register/unregister virtual devices with kernel
    bool register_device(std::shared_ptr<VirtualUsbDevice> device);
    bool unregister_device(uint32_t device_id);
    
    // Kernel callback handlers
    static int urb_enqueue_callback(void* hcd_priv, void* urb_priv);
    static int urb_dequeue_callback(void* hcd_priv, void* urb_priv);
    
private:
    bool kernel_initialized_;
    void* hcd_driver_;  // Pointer to kernel HCD driver structure
    
    std::mutex devices_mutex_;
    std::unordered_map<uint32_t, std::shared_ptr<VirtualUsbDevice>> kernel_devices_;
    std::unordered_map<uint32_t, int> vhci_port_map_; // Maps device_id to vhci port
    
    // USB/IP vhci_hcd interface functions
    bool check_vhci_hcd_loaded();
    int find_free_vhci_port();
    bool attach_to_vhci(std::shared_ptr<VirtualUsbDevice> device);
    bool detach_from_vhci(uint32_t device_id);
};

// WiFi 6E optimization features
class NetworkOptimizer {
public:
    static void optimize_socket_for_wifi6e(int socket_fd);
    static void set_high_priority_qos(int socket_fd);
    static void enable_tcp_no_delay(int socket_fd);
    static void set_large_buffers(int socket_fd);
    static void enable_multipath_tcp(int socket_fd);
    
private:
    static constexpr int WIFI6E_BUFFER_SIZE = 2 * 1024 * 1024; // 2MB buffers
    static constexpr int WIFI6E_PRIORITY = 6; // High priority for real-time data
};

} // namespace airusb