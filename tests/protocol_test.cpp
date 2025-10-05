#include "../src/protocol.hpp" 
#include <iostream>
#include <cstring>

using namespace airusb;

int main() {
    std::cout << "=== AirUSB Protocol Test ===" << std::endl;
    
    // Test 1: Basic message serialization
    std::cout << "\n1. Testing message serialization..." << std::endl;
    
    Message original(MessageType::DEVICE_LIST_REQUEST, 42);
    original.add_payload_data("test", 4);
    
    auto serialized = original.serialize();
    std::cout << "Serialized size: " << serialized.size() << " bytes" << std::endl;
    
    Message deserialized;
    bool success = deserialized.deserialize(serialized);
    
    std::cout << "Deserialization: " << (success ? "SUCCESS" : "FAILED") << std::endl;
    std::cout << "Type matches: " << (deserialized.header.type == original.header.type ? "YES" : "NO") << std::endl;
    std::cout << "Sequence matches: " << (deserialized.header.sequence == original.header.sequence ? "YES" : "NO") << std::endl;
    std::cout << "Payload matches: " << (deserialized.payload == original.payload ? "YES" : "NO") << std::endl;
    
    // Test 2: DeviceInfo serialization
    std::cout << "\n2. Testing DeviceInfo handling..." << std::endl;
    
    DeviceInfo device{};
    device.device_id = 123;
    device.vendor_id = 0x1234;
    device.product_id = 0x5678;
    strncpy(device.manufacturer, "Test Company", sizeof(device.manufacturer) - 1);
    strncpy(device.product, "Test Device", sizeof(device.product) - 1);
    
    Message device_msg(MessageType::DEVICE_LIST_RESPONSE, 100);
    device_msg.add_payload(device);
    
    auto device_serialized = device_msg.serialize();
    std::cout << "Device message size: " << device_serialized.size() << " bytes" << std::endl;
    
    Message device_received;
    bool device_success = device_received.deserialize(device_serialized);
    
    std::cout << "Device deserialization: " << (device_success ? "SUCCESS" : "FAILED") << std::endl;
    if (!device_success) {
        std::cout << "Expected size: " << sizeof(DeviceInfo) << ", got: " << device_msg.payload.size() << std::endl;
        std::cout << "Serialized size: " << device_serialized.size() << std::endl;
    }
    
    if (device_success && device_received.payload.size() >= sizeof(DeviceInfo)) {
        const DeviceInfo* received_device = reinterpret_cast<const DeviceInfo*>(device_received.payload.data());
        std::cout << "Device ID: " << received_device->device_id << std::endl;
        std::cout << "Manufacturer: " << received_device->manufacturer << std::endl;
        std::cout << "Product: " << received_device->product << std::endl;
        std::cout << "VID: 0x" << std::hex << received_device->vendor_id << std::dec << std::endl;
        std::cout << "PID: 0x" << std::hex << received_device->product_id << std::dec << std::endl;
    }
    
    // Test 3: Multiple devices
    std::cout << "\n3. Testing multiple devices..." << std::endl;
    
    Message multi_device(MessageType::DEVICE_LIST_RESPONSE, 200);
    
    for (int i = 0; i < 3; i++) {
        DeviceInfo dev{};
        dev.device_id = i + 1;
        dev.vendor_id = 0x1000 + i;
        dev.product_id = 0x2000 + i;
        snprintf(dev.manufacturer, sizeof(dev.manufacturer), "Company%d", i);
        snprintf(dev.product, sizeof(dev.product), "Device%d", i);
        multi_device.add_payload(dev);
    }
    
    auto multi_serialized = multi_device.serialize();
    Message multi_received;
    bool multi_success = multi_received.deserialize(multi_serialized);
    
    std::cout << "Multi-device deserialization: " << (multi_success ? "SUCCESS" : "FAILED") << std::endl;
    
    if (multi_success) {
        size_t device_count = multi_received.payload.size() / sizeof(DeviceInfo);
        std::cout << "Device count: " << device_count << std::endl;
        
        for (size_t i = 0; i < device_count; i++) {
            const DeviceInfo* dev = reinterpret_cast<const DeviceInfo*>(
                multi_received.payload.data() + i * sizeof(DeviceInfo));
            std::cout << "  Device " << i << ": " << dev->manufacturer << " " << dev->product << std::endl;
        }
    }
    
    std::cout << "\n=== Protocol Test Complete ===" << std::endl;
    
    if (success && device_success && multi_success) {
        std::cout << "✅ ALL TESTS PASSED - Protocol is working correctly!" << std::endl;
        return 0;
    } else {
        std::cout << "❌ SOME TESTS FAILED" << std::endl;
        return 1;
    }
}