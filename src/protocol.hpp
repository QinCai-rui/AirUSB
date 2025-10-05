#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <stdexcept>
#include <queue>

namespace airusb {

// Optimized protocol for WiFi 6E/7 - designed for high bandwidth, low latency
constexpr uint32_t AIRUSB_MAGIC = 0x41495255; // "AIRU"
constexpr uint16_t PROTOCOL_VERSION = 1;
constexpr uint16_t MAX_PACKET_SIZE = 8192; // Optimized for WiFi 6E frames

enum class MessageType : uint8_t {
    // Device management
    DEVICE_LIST_REQUEST = 0x01,
    DEVICE_LIST_RESPONSE = 0x02,
    DEVICE_ATTACH_REQUEST = 0x03,
    DEVICE_ATTACH_RESPONSE = 0x04,
    DEVICE_DETACH_REQUEST = 0x05,
    DEVICE_DETACH_RESPONSE = 0x06,
    
    // USB transfers - optimized for speed
    USB_SUBMIT_URB = 0x10,
    USB_COMPLETE_URB = 0x11,
    USB_UNLINK_URB = 0x12,
    
    // Bulk data streaming (for high-speed devices)
    BULK_DATA_START = 0x20,
    BULK_DATA_CHUNK = 0x21,
    BULK_DATA_END = 0x22,
    
    // Error handling
    ERROR = 0xFF
};

enum class UrbType : uint8_t {
    ISO = 0,
    INT = 1,
    CONTROL = 2,
    BULK = 3
};

enum class UrbDirection : uint8_t {
    OUT = 0,
    IN = 1
};

// Compact message header - optimized for minimal overhead
struct __attribute__((packed)) MessageHeader {
    uint32_t magic;           // AIRUSB_MAGIC
    uint16_t version;         // Protocol version
    MessageType type;         // Message type
    uint8_t flags;           // Compression, encryption flags
    uint32_t length;         // Payload length
    uint32_t sequence;       // Sequence number for ordering
    uint32_t crc32;          // Header + payload CRC
};

// Device information
struct __attribute__((packed)) DeviceInfo {
    uint32_t bus_id;
    uint32_t device_id;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_class;
    uint16_t device_subclass;
    uint8_t device_protocol;
    uint8_t configuration_value;
    uint8_t num_interfaces;
    uint8_t device_speed; // USB_SPEED_*
    uint8_t bus_num;
    uint8_t device_num;
    uint8_t port_number;
    uint8_t reserved;
    char manufacturer[64];
    char product[64];
    char serial[32];
    char busid[16]; // e.g., "2-2", "3-1", etc.
};

// URB (USB Request Block) for data transfers
struct __attribute__((packed)) UrbHeader {
    uint64_t urb_id;          // Unique URB identifier
    uint32_t device_id;       // Target device
    UrbType type;             // URB type
    UrbDirection direction;   // Transfer direction
    uint8_t endpoint;         // Endpoint address
    uint8_t flags;           // URB flags
    uint32_t transfer_length; // Expected/actual transfer length
    uint32_t start_frame;    // For isochronous transfers
    uint32_t number_of_packets; // For isochronous transfers
    int32_t status;          // Transfer status (for responses)
};

// Bulk data streaming header for high-throughput transfers
struct __attribute__((packed)) BulkDataHeader {
    uint64_t stream_id;      // Unique stream identifier
    uint32_t total_size;     // Total data size
    uint32_t chunk_size;     // This chunk size
    uint32_t chunk_offset;   // Offset in total data
    uint8_t compression;     // Compression algorithm used
    uint8_t reserved[3];
};

class Message {
public:
    MessageHeader header;
    std::vector<uint8_t> payload;
    
    Message() : Message(MessageType::ERROR, 0) {}
    Message(MessageType type, uint32_t sequence = 0);
    
    // Serialization
    std::vector<uint8_t> serialize() const;
    bool deserialize(const std::vector<uint8_t>& data);
    
    // Payload helpers
    template<typename T>
    void add_payload(const T& data) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&data);
        payload.insert(payload.end(), ptr, ptr + sizeof(T));
    }
    
    void add_payload_data(const void* data, size_t size) {
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        payload.insert(payload.end(), ptr, ptr + size);
    }
    
    template<typename T>
    T get_payload_as() const {
        if (payload.size() < sizeof(T)) {
            throw std::runtime_error("Payload too small");
        }
        return *reinterpret_cast<const T*>(payload.data());
    }
    
private:
    void update_header();
    uint32_t calculate_crc() const;
};

// Compression utilities for high-speed data
class Compressor {
public:
    enum Algorithm : uint8_t {
        NONE = 0,
        LZ4 = 1,      // Fast compression for real-time data
        ZSTD = 2      // Better compression ratio
    };
    
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& data, Algorithm alg = LZ4);
    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& compressed_data, Algorithm alg);
};

} // namespace airusb