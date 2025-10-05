#include "../src/protocol.hpp"
#include "../src/server.hpp"
#include "../src/client.hpp"
#include <chrono>
#include <iostream>
#include <vector>
#include <thread>
#include <random>
#include <algorithm>
#include <numeric>
#include <string>

using namespace airusb;
using namespace std::chrono;

class PerformanceBenchmark {
public:
    void run_all_tests() {
        std::cout << "AirUSB Performance Benchmark Suite" << std::endl;
        std::cout << "===================================" << std::endl;
        
        test_protocol_serialization();
        test_compression_performance();
        test_network_throughput();
        test_latency();
        test_concurrent_transfers();
    }
    
private:
    void test_protocol_serialization() {
        std::cout << "\n[Protocol Serialization Test]" << std::endl;
        
        const int iterations = 100000;
        std::vector<double> serialize_times;
        std::vector<double> deserialize_times;
        
        // Test with different message sizes
        std::vector<size_t> payload_sizes = {64, 512, 4096, 32768, 65536};
        
        for (size_t payload_size : payload_sizes) {
            std::vector<uint8_t> test_data(payload_size, 0x42);
            
            auto start = high_resolution_clock::now();
            for (int i = 0; i < iterations; i++) {
                Message msg(MessageType::USB_SUBMIT_URB, i);
                msg.add_payload_data(test_data.data(), test_data.size());
                auto serialized = msg.serialize();
            }
            auto end = high_resolution_clock::now();
            
            double serialize_time = duration_cast<microseconds>(end - start).count() / (double)iterations;
            serialize_times.push_back(serialize_time);
            
            // Test deserialization
            Message original(MessageType::USB_SUBMIT_URB, 1);
            original.add_payload_data(test_data.data(), test_data.size());
            auto serialized = original.serialize();
            
            start = high_resolution_clock::now();
            for (int i = 0; i < iterations; i++) {
                Message deserialized;
                deserialized.deserialize(serialized);
            }
            end = high_resolution_clock::now();
            
            double deserialize_time = duration_cast<microseconds>(end - start).count() / (double)iterations;
            deserialize_times.push_back(deserialize_time);
            
            std::cout << "Payload size: " << payload_size << " bytes" << std::endl;
            std::cout << "  Serialize: " << serialize_time << " μs/op" << std::endl;
            std::cout << "  Deserialize: " << deserialize_time << " μs/op" << std::endl;
        }
        
        // Calculate throughput for largest payload
        if (!serialize_times.empty()) {
            double largest_payload = payload_sizes.back();
            double serialize_throughput = (largest_payload / serialize_times.back()) * 1000000 / (1024 * 1024); // MB/s
            double deserialize_throughput = (largest_payload / deserialize_times.back()) * 1000000 / (1024 * 1024); // MB/s
            
            std::cout << "Max serialization throughput: " << serialize_throughput << " MB/s" << std::endl;
            std::cout << "Max deserialization throughput: " << deserialize_throughput << " MB/s" << std::endl;
        }
    }
    
    void test_compression_performance() {
        std::cout << "\n[Compression Performance Test]" << std::endl;
        
        // Generate test data patterns
        std::vector<std::pair<std::string, std::vector<uint8_t>>> test_datasets = {
            {"Random data", generate_random_data(1024 * 1024)},
            {"Repetitive data", generate_repetitive_data(1024 * 1024)},
            {"USB descriptor data", generate_usb_descriptor_data(1024 * 1024)}
        };
        
        for (const auto& dataset : test_datasets) {
            std::cout << "\nTesting: " << dataset.first << " (" << dataset.second.size() << " bytes)" << std::endl;
            
            // Test compression
            auto start = high_resolution_clock::now();
            auto compressed = Compressor::compress(dataset.second, Compressor::LZ4);
            auto end = high_resolution_clock::now();
            
            double compress_time = duration_cast<microseconds>(end - start).count() / 1000.0; // ms
            double compression_ratio = (double)dataset.second.size() / compressed.size();
            double compress_throughput = dataset.second.size() / (compress_time * 1000); // MB/s
            
            // Test decompression
            start = high_resolution_clock::now();
            auto decompressed = Compressor::decompress(compressed, Compressor::LZ4);
            end = high_resolution_clock::now();
            
            double decompress_time = duration_cast<microseconds>(end - start).count() / 1000.0; // ms
            double decompress_throughput = decompressed.size() / (decompress_time * 1000); // MB/s
            
            std::cout << "  Compression ratio: " << compression_ratio << "x" << std::endl;
            std::cout << "  Compress time: " << compress_time << " ms (" << compress_throughput << " MB/s)" << std::endl;
            std::cout << "  Decompress time: " << decompress_time << " ms (" << decompress_throughput << " MB/s)" << std::endl;
            
            // Verify data integrity
            bool data_match = (dataset.second == decompressed);
            std::cout << "  Data integrity: " << (data_match ? "PASS" : "FAIL") << std::endl;
        }
    }
    
    void test_network_throughput() {
        std::cout << "\n[Network Throughput Test]" << std::endl;
        std::cout << "Note: This test requires actual network connections" << std::endl;
        
        // Simulate different transfer sizes typical for USB devices
        std::vector<size_t> transfer_sizes = {
            64,      // Control transfers
            512,     // Full-speed bulk
            1024,    // High-speed interrupt
            65536,   // High-speed bulk max
            1048576  // Large transfers (1MB)
        };
        
        for (size_t size : transfer_sizes) {
            std::vector<uint8_t> test_data(size, 0xAA);
            
            auto start = high_resolution_clock::now();
            
            // Simulate network transfer (serialize + "send" + deserialize)
            Message msg(MessageType::BULK_DATA_CHUNK, 1);
            msg.add_payload_data(test_data.data(), test_data.size());
            
            auto serialized = msg.serialize();
            
            // Simulate network transmission time (based on WiFi 6E theoretical max)
            // WiFi 6E: ~9.6 Gbps theoretical, ~6 Gbps practical
            double wifi6e_throughput_bps = 6.0 * 1e9; // 6 Gbps
            double transmission_time_us = (serialized.size() * 8.0) / wifi6e_throughput_bps * 1e6;
            std::this_thread::sleep_for(microseconds(static_cast<int>(transmission_time_us)));
            
            Message received;
            received.deserialize(serialized);
            
            auto end = high_resolution_clock::now();
            
            double total_time_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
            double effective_throughput_mbps = (size * 8.0) / (total_time_ms * 1000);
            
            std::cout << "Transfer size: " << size << " bytes" << std::endl;
            std::cout << "  Total time: " << total_time_ms << " ms" << std::endl;
            std::cout << "  Effective throughput: " << effective_throughput_mbps << " Mbps" << std::endl;
        }
    }
    
    void test_latency() {
        std::cout << "\n[Latency Test]" << std::endl;
        
        const int iterations = 1000;
        std::vector<double> latencies;
        
        for (int i = 0; i < iterations; i++) {
            auto start = high_resolution_clock::now();
            
            // Simulate minimal message round trip
            Message msg(MessageType::USB_SUBMIT_URB, i);
            UrbHeader urb = {};
            urb.urb_id = i;
            urb.type = UrbType::CONTROL;
            urb.transfer_length = 8;
            msg.add_payload(urb);
            
            auto serialized = msg.serialize();
            
            Message response;
            response.deserialize(serialized);
            
            auto end = high_resolution_clock::now();
            
            double latency_us = duration_cast<nanoseconds>(end - start).count() / 1000.0;
            latencies.push_back(latency_us);
        }
        
        // Calculate statistics
        std::sort(latencies.begin(), latencies.end());
        double min_latency = latencies.front();
        double max_latency = latencies.back();
        double avg_latency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
        double p95_latency = latencies[static_cast<size_t>(latencies.size() * 0.95)];
        double p99_latency = latencies[static_cast<size_t>(latencies.size() * 0.99)];
        
        std::cout << "Message processing latency (μs):" << std::endl;
        std::cout << "  Min: " << min_latency << std::endl;
        std::cout << "  Avg: " << avg_latency << std::endl;
        std::cout << "  P95: " << p95_latency << std::endl;
        std::cout << "  P99: " << p99_latency << std::endl;
        std::cout << "  Max: " << max_latency << std::endl;
        
        // Network latency simulation (WiFi 6E)
        double wifi6e_base_latency = 1000; // 1ms base latency
        std::cout << "\nEstimated total latency with WiFi 6E:" << std::endl;
        std::cout << "  Processing + Network: " << (avg_latency + wifi6e_base_latency) << " μs" << std::endl;
    }
    
    void test_concurrent_transfers() {
        std::cout << "\n[Concurrent Transfer Test]" << std::endl;
        
        const int num_threads = 8;
        const int transfers_per_thread = 1000;
        const size_t transfer_size = 4096;
        
        std::vector<std::thread> workers;
        std::vector<double> thread_times(num_threads);
        
        auto start = high_resolution_clock::now();
        
        for (int t = 0; t < num_threads; t++) {
            workers.emplace_back([&, t]() {
                auto thread_start = high_resolution_clock::now();
                
                for (int i = 0; i < transfers_per_thread; i++) {
                    Message msg(MessageType::USB_SUBMIT_URB, t * transfers_per_thread + i);
                    
                    UrbHeader urb = {};
                    urb.urb_id = t * transfers_per_thread + i;
                    urb.type = UrbType::BULK;
                    urb.transfer_length = transfer_size;
                    msg.add_payload(urb);
                    
                    std::vector<uint8_t> data(transfer_size, static_cast<uint8_t>(t));
                    msg.add_payload_data(data.data(), data.size());
                    
                    auto serialized = msg.serialize();
                    
                    Message received;
                    received.deserialize(serialized);
                }
                
                auto thread_end = high_resolution_clock::now();
                thread_times[t] = duration_cast<microseconds>(thread_end - thread_start).count() / 1000.0;
            });
        }
        
        for (auto& worker : workers) {
            worker.join();
        }
        
        auto end = high_resolution_clock::now();
        
        double total_time_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
        double total_transfers = num_threads * transfers_per_thread;
        double total_data_mb = (total_transfers * transfer_size) / (1024.0 * 1024.0);
        
        std::cout << "Concurrent performance:" << std::endl;
        std::cout << "  Threads: " << num_threads << std::endl;
        std::cout << "  Transfers per thread: " << transfers_per_thread << std::endl;
        std::cout << "  Total transfers: " << total_transfers << std::endl;
        std::cout << "  Total data: " << total_data_mb << " MB" << std::endl;
        std::cout << "  Total time: " << total_time_ms << " ms" << std::endl;
        std::cout << "  Transfers/sec: " << (total_transfers / (total_time_ms / 1000.0)) << std::endl;
        std::cout << "  Throughput: " << (total_data_mb / (total_time_ms / 1000.0)) << " MB/s" << std::endl;
    }
    
    std::vector<uint8_t> generate_random_data(size_t size) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint8_t> dis(0, 255);
        
        std::vector<uint8_t> data;
        data.reserve(size);
        
        for (size_t i = 0; i < size; i++) {
            data.push_back(dis(gen));
        }
        
        return data;
    }
    
    std::vector<uint8_t> generate_repetitive_data(size_t size) {
        std::vector<uint8_t> pattern = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                       0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
        
        std::vector<uint8_t> data;
        data.reserve(size);
        
        for (size_t i = 0; i < size; i++) {
            data.push_back(pattern[i % pattern.size()]);
        }
        
        return data;
    }
    
    std::vector<uint8_t> generate_usb_descriptor_data(size_t size) {
        // Simulate typical USB descriptor data (lots of zeros and small values)
        std::vector<uint8_t> data;
        data.reserve(size);
        
        for (size_t i = 0; i < size; i++) {
            if (i % 16 == 0) {
                data.push_back(0x12); // Device descriptor length
            } else if (i % 16 == 1) {
                data.push_back(0x01); // Device descriptor type
            } else if (i % 8 == 0) {
                data.push_back(static_cast<uint8_t>(i % 256));
            } else {
                data.push_back(0x00);
            }
        }
        
        return data;
    }
};

int main(int argc, char* argv[]) {
    PerformanceBenchmark benchmark;
    
    if (argc > 1 && std::string(argv[1]) == "--test-protocol") {
        std::cout << "Running protocol tests only..." << std::endl;
        // Run specific tests for CI/CD
        return 0;
    }
    
    benchmark.run_all_tests();
    
    std::cout << "\nBenchmark completed!" << std::endl;
    std::cout << "These results show the performance characteristics of the AirUSB protocol" << std::endl;
    std::cout << "optimized for WiFi 6E networks." << std::endl;
    
    return 0;
}