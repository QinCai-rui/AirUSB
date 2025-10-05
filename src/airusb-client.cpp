#include "client.hpp"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>

using namespace airusb;

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <server_ip> [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -p <port>     Server port (default: 3240)" << std::endl;
    std::cout << "  -l            List available devices" << std::endl;
    std::cout << "  -a <busid>    Attach device by busid (e.g., 2-2)" << std::endl;
    std::cout << "  -d <id>       Detach device by ID" << std::endl;
    std::cout << "  -h            Show this help" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string server_ip = argv[1];
    uint16_t port = 3240;
    bool list_devices = false;
    uint32_t attach_device_id = 0;
    uint32_t detach_device_id = 0;
    std::string attach_busid;
    
    // Parse command line arguments
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-p" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "-l") {
            list_devices = true;
        } else if (arg == "-a" && i + 1 < argc) {
            attach_busid = argv[++i];
        } else if (arg == "-d" && i + 1 < argc) {
            detach_device_id = static_cast<uint32_t>(std::atoi(argv[++i]));
        }
    }
    
    std::cout << "AirUSB Client - High-speed USB over WiFi 6E" << std::endl;
    std::cout << "===========================================" << std::endl;
    
    UsbClient client;
    
    if (!client.connect(server_ip, port)) {
        std::cerr << "Failed to connect to server" << std::endl;
        return 1;
    }
    
    // Execute requested operations
    if (list_devices) {
        std::cout << "Listing available devices..." << std::endl;
        auto devices = client.list_devices();
        
        if (devices.empty()) {
            std::cout << "No devices found" << std::endl;
        } else {
            for (const auto& device : devices) {
                std::cout << "busid " << device.busid << " (" 
                          << std::hex << device.vendor_id 
                          << ":" << device.product_id << ")" << std::dec << std::endl;
                std::cout << "       " << device.manufacturer << " : " << device.product << std::endl;
            }
        }
    }
    
    if (!attach_busid.empty()) {
        std::cout << "Looking up device with busid " << attach_busid << "..." << std::endl;
        
        // First get the device list to find device info
        auto devices = client.list_devices();
        DeviceInfo target_device;
        bool found = false;
        
        for (const auto& device : devices) {
            if (std::string(device.busid) == attach_busid) {
                target_device = device;
                found = true;
                break;
            }
        }
        
        if (!found) {
            std::cerr << "Device with busid " << attach_busid << " not found" << std::endl;
            client.disconnect();
            return 1;
        }
        
        uint32_t device_id = target_device.device_id;
        std::cout << "Attaching device " << device_id << " (busid: " << attach_busid << ")..." << std::endl;
        
        if (client.attach_device(device_id)) {
            std::cout << "Device attached successfully" << std::endl;
            
            // Create virtual USB device
            auto virtual_device = std::make_shared<VirtualUsbDevice>(target_device, -1); // Socket will be managed by client
            
            // Initialize kernel driver interface
            KernelUsbDriver kernel_driver;
            if (!kernel_driver.initialize()) {
                std::cerr << "Failed to initialize kernel driver interface" << std::endl;
                client.detach_device(device_id);
                client.disconnect();
                return 1;
            }
            
            // Register virtual device with kernel
            if (kernel_driver.register_device(virtual_device)) {
                std::cout << "\nDevice is now available on this system!" << std::endl;
                std::cout << "You can check with: lsusb or lsblk" << std::endl;
                std::cout << "Press Ctrl+C to detach and exit" << std::endl;
                
                // Keep running and handle device I/O
                while (client.is_connected()) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                
                // Cleanup
                kernel_driver.unregister_device(device_id);
            } else {
                std::cerr << "Failed to register virtual USB device with kernel" << std::endl;
                std::cerr << "Make sure vhci_hcd module is loaded: sudo modprobe vhci-hcd" << std::endl;
                std::cerr << "And you have appropriate permissions (try with sudo)" << std::endl;
                client.detach_device(device_id);
            }
        } else {
            std::cerr << "Failed to attach device" << std::endl;
        }
    }
    
    if (detach_device_id > 0) {
        std::cout << "Detaching device " << detach_device_id << "..." << std::endl;
        if (client.detach_device(detach_device_id)) {
            std::cout << "Device detached successfully" << std::endl;
        } else {
            std::cerr << "Failed to detach device" << std::endl;
        }
    }
    
    client.disconnect();
    return 0;
}