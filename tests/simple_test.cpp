#include "../src/protocol.hpp"
#include <iostream>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

using namespace airusb;

// Simple test server
class TestServer {
private:
    int server_fd, client_fd;
    uint16_t port;
    
public:
    TestServer(uint16_t p) : port(p), server_fd(-1), client_fd(-1) {}
    
    bool start() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) return false;
        
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
        
        if (bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0) return false;
        if (listen(server_fd, 1) < 0) return false;
        
        std::cout << "Test server listening on port " << port << std::endl;
        
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
        
        if (client_fd >= 0) {
            std::cout << "Client connected!" << std::endl;
            handle_client();
        }
        
        return true;
    }
    
    void handle_client() {
        Message request;
        if (receive_message(request)) {
            std::cout << "Received message type: " << (int)request.header.type << std::endl;
            
            if (request.header.type == MessageType::DEVICE_LIST_REQUEST) {
                // Send back a test device
                Message response(MessageType::DEVICE_LIST_RESPONSE, request.header.sequence);
                
                DeviceInfo test_device{};
                test_device.device_id = 1;
                test_device.vendor_id = 0x1234;
                test_device.product_id = 0x5678;
                strcpy(test_device.manufacturer, "Test Manufacturer");
                strcpy(test_device.product, "Test USB Device");
                
                response.add_payload(test_device);
                
                if (send_message(response)) {
                    std::cout << "Sent device list response" << std::endl;
                } else {
                    std::cout << "Failed to send response" << std::endl;
                }
            }
        }
    }
    
    bool send_message(const Message& msg) {
        auto data = msg.serialize();
        ssize_t sent = send(client_fd, data.data(), data.size(), MSG_NOSIGNAL);
        return sent == (ssize_t)data.size();
    }
    
    bool receive_message(Message& msg) {
        MessageHeader header;
        ssize_t received = recv(client_fd, &header, sizeof(header), MSG_WAITALL);
        if (received != sizeof(header)) return false;
        
        std::vector<uint8_t> full_data(sizeof(header) + header.length);
        memcpy(full_data.data(), &header, sizeof(header));
        
        if (header.length > 0) {
            received = recv(client_fd, full_data.data() + sizeof(header), header.length, MSG_WAITALL);
            if (received != (ssize_t)header.length) return false;
        }
        
        return msg.deserialize(full_data);
    }
    
    ~TestServer() {
        if (client_fd >= 0) close(client_fd);
        if (server_fd >= 0) close(server_fd);
    }
};

// Simple test client  
class TestClient {
private:
    int socket_fd;
    
public:
    TestClient() : socket_fd(-1) {}
    
    bool connect_to_server(const std::string& host, uint16_t port) {
        socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd < 0) return false;
        
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);
        
        if (connect(socket_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(socket_fd);
            return false;
        }
        
        std::cout << "Connected to server!" << std::endl;
        return true;
    }
    
    bool test_device_list() {
        Message request(MessageType::DEVICE_LIST_REQUEST, 1);
        
        if (!send_message(request)) {
            std::cout << "Failed to send request" << std::endl;
            return false;
        }
        
        std::cout << "Sent device list request" << std::endl;
        
        Message response;
        if (!receive_message(response)) {
            std::cout << "Failed to receive response" << std::endl;
            return false;
        }
        
        std::cout << "Received response type: " << (int)response.header.type << std::endl;
        
        if (response.header.type == MessageType::DEVICE_LIST_RESPONSE) {
            if (response.payload.size() >= sizeof(DeviceInfo)) {
                const DeviceInfo* device = (const DeviceInfo*)response.payload.data();
                std::cout << "Device found: " << device->manufacturer << " " << device->product << std::endl;
                std::cout << "VID: 0x" << std::hex << device->vendor_id << " PID: 0x" << device->product_id << std::dec << std::endl;
                return true;
            }
        }
        
        return false;
    }
    
    bool send_message(const Message& msg) {
        auto data = msg.serialize();
        ssize_t sent = send(socket_fd, data.data(), data.size(), MSG_NOSIGNAL);
        return sent == (ssize_t)data.size();
    }
    
    bool receive_message(Message& msg) {
        MessageHeader header;
        ssize_t received = recv(socket_fd, &header, sizeof(header), MSG_WAITALL);
        if (received != sizeof(header)) return false;
        
        std::vector<uint8_t> full_data(sizeof(header) + header.length);
        memcpy(full_data.data(), &header, sizeof(header));
        
        if (header.length > 0) {
            received = recv(socket_fd, full_data.data() + sizeof(header), header.length, MSG_WAITALL);
            if (received != (ssize_t)header.length) return false;
        }
        
        return msg.deserialize(full_data);
    }
    
    ~TestClient() {
        if (socket_fd >= 0) close(socket_fd);
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <server|client>" << std::endl;
        return 1;
    }
    
    std::string mode = argv[1];
    
    if (mode == "server") {
        std::cout << "Starting test server..." << std::endl;
        TestServer server(3250);
        server.start();
    } else if (mode == "client") {
        std::cout << "Starting test client..." << std::endl;
        TestClient client;
        
        if (client.connect_to_server("127.0.0.1", 3250)) {
            if (client.test_device_list()) {
                std::cout << "✅ Test PASSED: Device list communication works!" << std::endl;
            } else {
                std::cout << "❌ Test FAILED: Device list communication failed" << std::endl;
            }
        } else {
            std::cout << "❌ Test FAILED: Could not connect to server" << std::endl;
        }
    } else {
        std::cout << "Invalid mode. Use 'server' or 'client'" << std::endl;
    }
    
    return 0;
}