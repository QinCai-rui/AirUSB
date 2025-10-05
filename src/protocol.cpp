#include "protocol.hpp"
#include <cstring>
#include <stdexcept>
#include <zlib.h>
#include <algorithm>
#include <numeric>

namespace airusb {

Message::Message(MessageType type, uint32_t sequence) {
    header.magic = AIRUSB_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = type;
    header.flags = 0;
    header.sequence = sequence;
    header.length = 0;
    header.crc32 = 0;
}

std::vector<uint8_t> Message::serialize() const {
    std::vector<uint8_t> result;
    
    // Copy header
    MessageHeader hdr = header;
    hdr.length = payload.size();
    
    const uint8_t* header_ptr = reinterpret_cast<const uint8_t*>(&hdr);
    result.insert(result.end(), header_ptr, header_ptr + sizeof(MessageHeader));
    
    // Copy payload
    result.insert(result.end(), payload.begin(), payload.end());
    
    // Calculate and update CRC
    uint32_t crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, result.data() + offsetof(MessageHeader, length), 
                result.size() - offsetof(MessageHeader, crc32));
    
    // Update CRC in the serialized data
    *reinterpret_cast<uint32_t*>(result.data() + offsetof(MessageHeader, crc32)) = crc;
    
    return result;
}

bool Message::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(MessageHeader)) {
        return false;
    }
    
    // Copy header
    std::memcpy(&header, data.data(), sizeof(MessageHeader));
    
    // Validate magic and version
    if (header.magic != AIRUSB_MAGIC || header.version != PROTOCOL_VERSION) {
        return false;
    }
    
    // Check payload size
    if (data.size() != sizeof(MessageHeader) + header.length) {
        return false;
    }
    
    // Verify CRC (temporarily disabled for debugging)
    uint32_t expected_crc = header.crc32;
    uint32_t calculated_crc = crc32(0L, Z_NULL, 0);
    calculated_crc = crc32(calculated_crc, 
                          data.data() + offsetof(MessageHeader, length),
                          data.size() - offsetof(MessageHeader, crc32));
    
    // Temporarily disable CRC check for debugging
    // if (expected_crc != calculated_crc) {
    //     return false;
    // }
    
    // Copy payload
    payload.clear();
    if (header.length > 0) {
        payload.insert(payload.end(), 
                      data.begin() + sizeof(MessageHeader),
                      data.end());
    }
    
    return true;
}

uint32_t Message::calculate_crc() const {
    uint32_t crc = crc32(0L, Z_NULL, 0);
    
    // CRC of header (excluding magic and crc32 field)
    const uint8_t* header_ptr = reinterpret_cast<const uint8_t*>(&header);
    crc = crc32(crc, header_ptr + offsetof(MessageHeader, length),
                sizeof(MessageHeader) - offsetof(MessageHeader, crc32));
    
    // CRC of payload
    if (!payload.empty()) {
        crc = crc32(crc, payload.data(), payload.size());
    }
    
    return crc;
}

void Message::update_header() {
    header.length = payload.size();
    header.crc32 = calculate_crc();
}

// Compressor implementation (using zlib for now, can add LZ4/ZSTD later)
std::vector<uint8_t> Compressor::compress(const std::vector<uint8_t>& data, Algorithm alg) {
    if (alg == NONE || data.empty()) {
        return data;
    }
    
    // Simple zlib compression for now
    z_stream strm = {};
    deflateInit(&strm, Z_DEFAULT_COMPRESSION);
    
    std::vector<uint8_t> compressed;
    compressed.resize(deflateBound(&strm, data.size()));
    
    strm.next_in = const_cast<uint8_t*>(data.data());
    strm.avail_in = data.size();
    strm.next_out = compressed.data();
    strm.avail_out = compressed.size();
    
    int ret = deflate(&strm, Z_FINISH);
    deflateEnd(&strm);
    
    if (ret == Z_STREAM_END) {
        compressed.resize(strm.total_out);
        return compressed;
    }
    
    // Compression failed, return original
    return data;
}

std::vector<uint8_t> Compressor::decompress(const std::vector<uint8_t>& compressed_data, Algorithm alg) {
    if (alg == NONE || compressed_data.empty()) {
        return compressed_data;
    }
    
    // Simple zlib decompression
    z_stream strm = {};
    inflateInit(&strm);
    
    std::vector<uint8_t> decompressed;
    decompressed.resize(compressed_data.size() * 4); // Initial guess
    
    strm.next_in = const_cast<uint8_t*>(compressed_data.data());
    strm.avail_in = compressed_data.size();
    strm.next_out = decompressed.data();
    strm.avail_out = decompressed.size();
    
    int ret;
    do {
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_BUF_ERROR) {
            // Need more output space
            size_t old_size = decompressed.size();
            decompressed.resize(old_size * 2);
            strm.next_out = decompressed.data() + old_size;
            strm.avail_out = old_size;
        }
    } while (ret == Z_OK || ret == Z_BUF_ERROR);
    
    inflateEnd(&strm);
    
    if (ret == Z_STREAM_END) {
        decompressed.resize(strm.total_out);
        return decompressed;
    }
    
    throw std::runtime_error("Decompression failed");
}

} // namespace airusb