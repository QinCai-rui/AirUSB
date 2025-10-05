#include "server.hpp"
#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>
#include <cstdlib>

using namespace airusb;

UsbServer* g_server = nullptr;
std::atomic<bool> g_terminate{false};

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
    g_terminate.store(true);
}

int main(int argc, char* argv[]) {
    uint16_t port = 3240;
    
    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "AirUSB Server - High-speed USB over WiFi 6E" << std::endl;
    std::cout << "=============================================" << std::endl;
    
    UsbServer server(port);
    g_server = &server;
    
    if (!server.start()) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    std::cout << "Server running. Press Ctrl+C to stop." << std::endl;
    
    // Keep running until signal handler sets g_terminate
    while (!g_terminate.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}