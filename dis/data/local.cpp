// include/kvstore/compression.h
#pragma once
#include <string>
#include <memory>
#include <vector>
#include <cstdint>

enum class CompressionType {
    NONE = 0,
    ZSTD = 1,
    SNAPPY = 2,
    LZ4 = 3,
    GZIP = 4,
    CUSTOM_DELTA = 5,      // 自定义差分编码
    CUSTOM_DICTIONARY = 6,  // 自定义字典压缩
    CUSTOM_RLE_BITMAP = 7, // 自定义RLE+位图压缩
    AUTO = 8               // 自动选择最优算法
};

struct PutOption {
    CompressionType compression = CompressionType::AUTO;
    int compressionLevel = 3;  // 1-9, 3 is default
    bool async = false;
    
    PutOption() = default;
    PutOption(CompressionType type) : compression(type) {}
    PutOption(CompressionType type, int level) 
        : compression(type), compressionLevel(level) {}
};

class ICompressor {
public:
    virtual ~ICompressor() = default;
    virtual std::string compress(const std::string& data) = 0;
    virtual std::string decompress(const std::string& compressed) = 0;
    virtual CompressionType type() const = 0;
    virtual double getCompressionRatio() const { return lastCompressionRatio; }
    
protected:
    double lastCompressionRatio = 1.0;
};

// include/kvstore/custom_compressor.h
#pragma once
#include "compression.h"
#include <unordered_map>
#include <bitset>

// 自定义压缩算法1: 差分编码 + 变长整数编码 (适合时序数据)
class DeltaCompressor : public ICompressor {
private:
    template<typename T>
    std::vector<uint8_t> encodeVarint(T value);
    
    template<typename T>
    T decodeVarint(const uint8_t* data, size_t& offset);
    
public:
    std::string compress(const std::string& data) override;
    std::string decompress(const std::string& compressed) override;
    CompressionType type() const override { return CompressionType::CUSTOM_DELTA; }
};

// 自定义压缩算法2: 字典编码 + Huffman (适合重复模式多的数据)
class DictionaryCompressor : public ICompressor {
private:
    struct HuffmanNode {
        uint8_t symbol;
        uint32_t frequency;
        std::unique_ptr<HuffmanNode> left;
        std::unique_ptr<HuffmanNode> right;
        
        HuffmanNode(uint8_t s = 0, uint32_t f = 0) : symbol(s), frequency(f) {}
    };
    
    std::unordered_map<std::string, uint32_t> buildDictionary(const std::string& data);
    std::unique_ptr<HuffmanNode> buildHuffmanTree(const std::unordered_map<uint8_t, uint32_t>& freq);
    
public:
    std::string compress(const std::string& data) override;
    std::string decompress(const std::string& compressed) override;
    CompressionType type() const override { return CompressionType::CUSTOM_DICTIONARY; }
};

// 自定义压缩算法3: RLE + 位图压缩 (适合稀疏数据)
class RLEBitmapCompressor : public ICompressor {
private:
    struct RLESegment {
        uint8_t value;
        uint32_t count;
    };
    
    std::vector<RLESegment> runLengthEncode(const std::string& data);
    std::string bitmapEncode(const std::vector<RLESegment>& segments);
    
public:
    std::string compress(const std::string& data) override;
    std::string decompress(const std::string& compressed) override;
    CompressionType type() const override { return CompressionType::CUSTOM_RLE_BITMAP; }
};

// include/kvstore/local_kvstore.h
#pragma once
#include "kvstore/ikvstore.h"
#include "kvstore/compression.h"
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <filesystem>
#include <atomic>

struct FileMetadata {
    std::string filename;
    size_t originalSize;
    size_t compressedSize;
    CompressionType compression;
    uint32_t checksum;
    int64_t timestamp;
};

class LocalDiskKVStore : public IKVStore {
private:
    std::string basePath;
    mutable std::shared_mutex metaMutex;
    std::unordered_map<std::string, FileMetadata> metadata;
    std::unordered_map<CompressionType, std::unique_ptr<ICompressor>> compressors;
    
    // Statistics
    struct Stats {
        std::atomic<uint64_t> totalPuts{0};
        std::atomic<uint64_t> totalGets{0};
        std::atomic<uint64_t> totalBytesWritten{0};
        std::atomic<uint64_t> totalBytesRead{0};
        std::atomic<uint64_t> totalOriginalBytes{0};
        std::atomic<uint64_t> totalCompressedBytes{0};
        std::unordered_map<CompressionType, std::atomic<uint64_t>> compressionUsage;
    } stats;
    
    std::string getFilePath(const std::string& key) const;
    CompressionType detectCompression(const std::string& data) const;
    CompressionType chooseOptimalCompression(const std::string& data) const;
    uint32_t calculateChecksum(const std::string& data) const;
    void saveMetadata();
    void loadMetadata();
    
public:
    LocalDiskKVStore(const std::string& path);
    ~LocalDiskKVStore();
    
    void get(const std::string& key, std::string& value) override;
    void put(const std::string& key, const std::string& value, PutOption opt) override;
    
    // Additional methods for testing and monitoring
    FileMetadata getMetadata(const std::string& key) const;
    void printStatistics() const;
    double getAverageCompressionRatio() const;
    std::unordered_map<CompressionType, double> getCompressionRatioByType() const;
};

// src/kvstore/custom_compressor.cpp
#include "kvstore/custom_compressor.h"
#include <algorithm>
#include <queue>
#include <cstring>

// ========== Delta Compressor Implementation ==========

template<typename T>
std::vector<uint8_t> DeltaCompressor::encodeVarint(T value) {
    std::vector<uint8_t> result;
    uint64_t uval = static_cast<uint64_t>(value);
    
    while (uval >= 0x80) {
        result.push_back((uval & 0x7F) | 0x80);
        uval >>= 7;
    }
    result.push_back(uval & 0x7F);
    return result;
}

template<typename T>
T DeltaCompressor::decodeVarint(const uint8_t* data, size_t& offset) {
    uint64_t result = 0;
    int shift = 0;
    
    while (true) {
        uint8_t byte = data[offset++];
        result |= (uint64_t(byte & 0x7F) << shift);
        if ((byte & 0x80) == 0) break;
        shift += 7;
    }
    
    return static_cast<T>(result);
}

std::string DeltaCompressor::compress(const std::string& data) {
    if (data.size() < 8) {
        lastCompressionRatio = 1.0;
        return data;  // Too small to compress
    }
    
    std::vector<uint8_t> compressed;
    compressed.push_back(static_cast<uint8_t>(CompressionType::CUSTOM_DELTA));
    
    // Store original size
    auto sizeBytes = encodeVarint(data.size());
    compressed.insert(compressed.end(), sizeBytes.begin(), sizeBytes.end());
    
    // Delta encode assuming data represents integers
    const int64_t* values = reinterpret_cast<const int64_t*>(data.data());
    size_t numValues = data.size() / sizeof(int64_t);
    
    if (numValues > 0) {
        // Store first value as-is
        auto firstBytes = encodeVarint(values[0]);
        compressed.insert(compressed.end(), firstBytes.begin(), firstBytes.end());
        
        // Store deltas
        for (size_t i = 1; i < numValues; ++i) {
            int64_t delta = values[i] - values[i-1];
            auto deltaBytes = encodeVarint(delta);
            compressed.insert(compressed.end(), deltaBytes.begin(), deltaBytes.end());
        }
    }
    
    // Handle remaining bytes
    size_t remaining = data.size() % sizeof(int64_t);
    if (remaining > 0) {
        compressed.insert(compressed.end(), 
                         data.end() - remaining, data.end());
    }
    
    std::string result(compressed.begin(), compressed.end());
    lastCompressionRatio = static_cast<double>(data.size()) / result.size();
    return result;
}

std::string DeltaCompressor::decompress(const std::string& compressed) {
    if (compressed.empty() || compressed[0] != static_cast<uint8_t>(CompressionType::CUSTOM_DELTA)) {
        return compressed;
    }
    
    const uint8_t* data = reinterpret_cast<const uint8_t*>(compressed.data());
    size_t offset = 1;
    
    size_t originalSize = decodeVarint<size_t>(data, offset);
    std::vector<int64_t> values;
    
    if (offset < compressed.size()) {
        int64_t current = decodeVarint<int64_t>(data, offset);
        values.push_back(current);
        
        while (offset < compressed.size() && values.size() * sizeof(int64_t) < originalSize) {
            int64_t delta = decodeVarint<int64_t>(data, offset);
            current += delta;
            values.push_back(current);
        }
    }
    
    std::string result;
    result.reserve(originalSize);
    result.append(reinterpret_cast<char*>(values.data()), values.size() * sizeof(int64_t));
    
    // Append remaining bytes
    if (offset < compressed.size()) {
        result.append(compressed.begin() + offset, compressed.end());
    }
    
    return result;
}

// ========== Dictionary Compressor Implementation ==========

std::unordered_map<std::string, uint32_t> DictionaryCompressor::buildDictionary(const std::string& data) {
    std::unordered_map<std::string, uint32_t> patterns;
    const size_t minPatternLen = 3;
    const size_t maxPatternLen = 16;
    
    for (size_t len = minPatternLen; len <= maxPatternLen && len <= data.size(); ++len) {
        for (size_t i = 0; i <= data.size() - len; ++i) {
            std::string pattern = data.substr(i, len);
            patterns[pattern]++;
        }
    }
    
    // Keep only frequent patterns
    std::unordered_map<std::string, uint32_t> dictionary;
    for (const auto& [pattern, count] : patterns) {
        if (count > 2 && count * pattern.size() > pattern.size() + 4) {
            dictionary[pattern] = count;
        }
    }
    
    return dictionary;
}

std::string DictionaryCompressor::compress(const std::string& data) {
    if (data.size() < 100) {
        lastCompressionRatio = 1.0;
        return data;
    }
    
    auto dictionary = buildDictionary(data);
    if (dictionary.empty()) {
        lastCompressionRatio = 1.0;
        return data;
    }
    
    // Simple dictionary encoding
    std::string compressed;
    compressed.push_back(static_cast<uint8_t>(CompressionType::CUSTOM_DICTIONARY));
    
    // Store dictionary size
    uint32_t dictSize = dictionary.size();
    compressed.append(reinterpret_cast<char*>(&dictSize), sizeof(dictSize));
    
    // Store dictionary
    std::unordered_map<std::string, uint16_t> patternToId;
    uint16_t id = 0;
    for (const auto& [pattern, count] : dictionary) {
        uint8_t len = pattern.size();
        compressed.push_back(len);
        compressed.append(pattern);
        patternToId[pattern] = id++;
    }
    
    // Encode data using dictionary
    size_t i = 0;
    while (i < data.size()) {
        bool found = false;
        for (size_t len = 16; len >= 3; --len) {
            if (i + len <= data.size()) {
                std::string pattern = data.substr(i, len);
                auto it = patternToId.find(pattern);
                if (it != patternToId.end()) {
                    compressed.push_back(0xFF);  // Dictionary marker
                    compressed.append(reinterpret_cast<char*>(&it->second), sizeof(uint16_t));
                    i += len;
                    found = true;
                    break;
                }
            }
        }
        
        if (!found) {
            compressed.push_back(data[i]);
            i++;
        }
    }
    
    lastCompressionRatio = static_cast<double>(data.size()) / compressed.size();
    return compressed;
}

std::string DictionaryCompressor::decompress(const std::string& compressed) {
    if (compressed.empty() || compressed[0] != static_cast<uint8_t>(CompressionType::CUSTOM_DICTIONARY)) {
        return compressed;
    }
    
    size_t offset = 1;
    uint32_t dictSize;
    std::memcpy(&dictSize, compressed.data() + offset, sizeof(dictSize));
    offset += sizeof(dictSize);
    
    // Read dictionary
    std::vector<std::string> idToPattern(dictSize);
    for (uint32_t i = 0; i < dictSize; ++i) {
        uint8_t len = compressed[offset++];
        idToPattern[i] = compressed.substr(offset, len);
        offset += len;
    }
    
    // Decode data
    std::string result;
    while (offset < compressed.size()) {
        if (compressed[offset] == 0xFF) {
            offset++;
            uint16_t id;
            std::memcpy(&id, compressed.data() + offset, sizeof(id));
            offset += sizeof(id);
            result.append(idToPattern[id]);
        } else {
            result.push_back(compressed[offset++]);
        }
    }
    
    return result;
}

// ========== RLE Bitmap Compressor Implementation ==========

std::vector<RLEBitmapCompressor::RLESegment> RLEBitmapCompressor::runLengthEncode(const std::string& data) {
    std::vector<RLESegment> segments;
    if (data.empty()) return segments;
    
    uint8_t currentValue = data[0];
    uint32_t count = 1;
    
    for (size_t i = 1; i < data.size(); ++i) {
        if (data[i] == currentValue && count < UINT32_MAX) {
            count++;
        } else {
            segments.push_back({currentValue, count});
            currentValue = data[i];
            count = 1;
        }
    }
    segments.push_back({currentValue, count});
    
    return segments;
}

std::string RLEBitmapCompressor::compress(const std::string& data) {
    if (data.size() < 10) {
        lastCompressionRatio = 1.0;
        return data;
    }
    
    auto segments = runLengthEncode(data);
    
    // Check if RLE is beneficial
    if (segments.size() * 5 >= data.size()) {
        lastCompressionRatio = 1.0;
        return data;
    }
    
    std::string compressed;
    compressed.push_back(static_cast<uint8_t>(CompressionType::CUSTOM_RLE_BITMAP));
    
    // Store original size
    uint32_t originalSize = data.size();
    compressed.append(reinterpret_cast<char*>(&originalSize), sizeof(originalSize));
    
    // Store number of segments
    uint32_t numSegments = segments.size();
    compressed.append(reinterpret_cast<char*>(&numSegments), sizeof(numSegments));
    
    // Create bitmap for common values
    std::unordered_map<uint8_t, uint32_t> valueFreq;
    for (const auto& seg : segments) {
        valueFreq[seg.value] += seg.count;
    }
    
    // Find top 8 most common values for bitmap encoding
    std::vector<std::pair<uint8_t, uint32_t>> sortedFreq(valueFreq.begin(), valueFreq.end());
    std::sort(sortedFreq.begin(), sortedFreq.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    uint8_t bitmapSize = std::min(8UL, sortedFreq.size());
    compressed.push_back(bitmapSize);
    
    std::unordered_map<uint8_t, uint8_t> valueToBit;
    for (uint8_t i = 0; i < bitmapSize; ++i) {
        compressed.push_back(sortedFreq[i].first);
        valueToBit[sortedFreq[i].first] = i;
    }
    
    // Encode segments
    for (const auto& seg : segments) {
        auto it = valueToBit.find(seg.value);
        if (it != valueToBit.end()) {
            // Use bitmap encoding
            compressed.push_back(0x80 | it->second);  // Set high bit for bitmap
        } else {
            // Store value directly
            compressed.push_back(seg.value);
        }
        
        // Variable length encoding for count
        if (seg.count < 128) {
            compressed.push_back(static_cast<uint8_t>(seg.count));
        } else if (seg.count < 32768) {
            compressed.push_back(0x80 | (seg.count >> 8));
            compressed.push_back(seg.count & 0xFF);
        } else {
            compressed.push_back(0xC0 | (seg.count >> 24));
            compressed.push_back((seg.count >> 16) & 0xFF);
            compressed.push_back((seg.count >> 8) & 0xFF);
            compressed.push_back(seg.count & 0xFF);
        }
    }
    
    lastCompressionRatio = static_cast<double>(data.size()) / compressed.size();
    return compressed;
}

std::string RLEBitmapCompressor::decompress(const std::string& compressed) {
    if (compressed.empty() || compressed[0] != static_cast<uint8_t>(CompressionType::CUSTOM_RLE_BITMAP)) {
        return compressed;
    }
    
    size_t offset = 1;
    
    uint32_t originalSize;
    std::memcpy(&originalSize, compressed.data() + offset, sizeof(originalSize));
    offset += sizeof(originalSize);
    
    uint32_t numSegments;
    std::memcpy(&numSegments, compressed.data() + offset, sizeof(numSegments));
    offset += sizeof(numSegments);
    
    // Read bitmap
    uint8_t bitmapSize = compressed[offset++];
    std::vector<uint8_t> bitToValue(bitmapSize);
    for (uint8_t i = 0; i < bitmapSize; ++i) {
        bitToValue[i] = compressed[offset++];
    }
    
    // Decode segments
    std::string result;
    result.reserve(originalSize);
    
    for (uint32_t i = 0; i < numSegments && offset < compressed.size(); ++i) {
        uint8_t value;
        uint8_t firstByte = compressed[offset++];
        
        if (firstByte & 0x80) {
            // Bitmap encoded value
            value = bitToValue[firstByte & 0x07];
        } else {
            value = firstByte;
        }
        
        // Decode count
        uint32_t count;
        uint8_t countByte = compressed[offset++];
        if ((countByte & 0xC0) == 0xC0) {
            count = ((countByte & 0x3F) << 24) | 
                    (compressed[offset++] << 16) |
                    (compressed[offset++] << 8) |
                    compressed[offset++];
        } else if (countByte & 0x80) {
            count = ((countByte & 0x7F) << 8) | compressed[offset++];
        } else {
            count = countByte;
        }
        
        result.append(count, value);
    }
    
    return result;
}