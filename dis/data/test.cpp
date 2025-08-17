// src/kvstore/local_kvstore.cpp
#include "kvstore/local_kvstore.h"
#include "kvstore/custom_compressor.h"
#include <zstd.h>
#include <snappy.h>
#include <lz4.h>
#include <zlib.h>
#include <fstream>
#include <chrono>
#include <crc32c/crc32c.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

// Standard compressor implementations
class ZstdCompressor : public ICompressor {
private:
    int level;
public:
    ZstdCompressor(int compressionLevel = 3) : level(compressionLevel) {}
    
    std::string compress(const std::string& data) override {
        size_t dstSize = ZSTD_compressBound(data.size());
        std::string compressed(dstSize + 1, '\0');
        compressed[0] = static_cast<uint8_t>(CompressionType::ZSTD);
        
        size_t actualSize = ZSTD_compress(
            compressed.data() + 1, dstSize,
            data.data(), data.size(), level
        );
        
        if (ZSTD_isError(actualSize)) {
            throw std::runtime_error("ZSTD compression failed");
        }
        
        compressed.resize(actualSize + 1);
        lastCompressionRatio = static_cast<double>(data.size()) / compressed.size();
        return compressed;
    }
    
    std::string decompress(const std::string& compressed) override {
        if (compressed.empty() || compressed[0] != static_cast<uint8_t>(CompressionType::ZSTD)) {
            return compressed;
        }
        
        unsigned long long decompressedSize = ZSTD_getFrameContentSize(
            compressed.data() + 1, compressed.size() - 1
        );
        
        if (decompressedSize == ZSTD_CONTENTSIZE_ERROR || 
            decompressedSize == ZSTD_CONTENTSIZE_UNKNOWN) {
            throw std::runtime_error("Invalid ZSTD compressed data");
        }
        
        std::string decompressed(decompressedSize, '\0');
        size_t actualSize = ZSTD_decompress(
            decompressed.data(), decompressedSize,
            compressed.data() + 1, compressed.size() - 1
        );
        
        if (ZSTD_isError(actualSize)) {
            throw std::runtime_error("ZSTD decompression failed");
        }
        
        return decompressed;
    }
    
    CompressionType type() const override { return CompressionType::ZSTD; }
};

class SnappyCompressor : public ICompressor {
public:
    std::string compress(const std::string& data) override {
        std::string compressed;
        compressed.push_back(static_cast<uint8_t>(CompressionType::SNAPPY));
        
        std::string temp;
        snappy::Compress(data.data(), data.size(), &temp);
        compressed.append(temp);
        
        lastCompressionRatio = static_cast<double>(data.size()) / compressed.size();
        return compressed;
    }
    
    std::string decompress(const std::string& compressed) override {
        if (compressed.empty() || compressed[0] != static_cast<uint8_t>(CompressionType::SNAPPY)) {
            return compressed;
        }
        
        std::string decompressed;
        if (!snappy::Uncompress(compressed.data() + 1, compressed.size() - 1, &decompressed)) {
            throw std::runtime_error("Snappy decompression failed");
        }
        
        return decompressed;
    }
    
    CompressionType type() const override { return CompressionType::SNAPPY; }
};

class LZ4Compressor : public ICompressor {
public:
    std::string compress(const std::string& data) override {
        int maxSize = LZ4_compressBound(data.size());
        std::string compressed(maxSize + 1 + sizeof(int), '\0');
        compressed[0] = static_cast<uint8_t>(CompressionType::LZ4);
        
        // Store original size for decompression
        int originalSize = data.size();
        std::memcpy(compressed.data() + 1, &originalSize, sizeof(int));
        
        int compressedSize = LZ4_compress_default(
            data.data(), compressed.data() + 1 + sizeof(int),
            data.size(), maxSize
        );
        
        if (compressedSize <= 0) {
            throw std::runtime_error("LZ4 compression failed");
        }
        
        compressed.resize(compressedSize + 1 + sizeof(int));
        lastCompressionRatio = static_cast<double>(data.size()) / compressed.size();
        return compressed;
    }
    
    std::string decompress(const std::string& compressed) override {
        if (compressed.empty() || compressed[0] != static_cast<uint8_t>(CompressionType::LZ4)) {
            return compressed;
        }
        
        int originalSize;
        std::memcpy(&originalSize, compressed.data() + 1, sizeof(int));
        
        std::string decompressed(originalSize, '\0');
        int actualSize = LZ4_decompress_safe(
            compressed.data() + 1 + sizeof(int), decompressed.data(),
            compressed.size() - 1 - sizeof(int), originalSize
        );
        
        if (actualSize != originalSize) {
            throw std::runtime_error("LZ4 decompression failed");
        }
        
        return decompressed;
    }
    
    CompressionType type() const override { return CompressionType::LZ4; }
};

// LocalDiskKVStore Implementation
LocalDiskKVStore::LocalDiskKVStore(const std::string& path) : basePath(path) {
    fs::create_directories(basePath);
    
    // Initialize compressors
    compressors[CompressionType::ZSTD] = std::make_unique<ZstdCompressor>();
    compressors[CompressionType::SNAPPY] = std::make_unique<SnappyCompressor>();
    compressors[CompressionType::LZ4] = std::make_unique<LZ4Compressor>();
    compressors[CompressionType::CUSTOM_DELTA] = std::make_unique<DeltaCompressor>();
    compressors[CompressionType::CUSTOM_DICTIONARY] = std::make_unique<DictionaryCompressor>();
    compressors[CompressionType::CUSTOM_RLE_BITMAP] = std::make_unique<RLEBitmapCompressor>();
    
    loadMetadata();
}

LocalDiskKVStore::~LocalDiskKVStore() {
    saveMetadata();
}

std::string LocalDiskKVStore::getFilePath(const std::string& key) const {
    // Simple hash-based directory structure to avoid too many files in one directory
    std::hash<std::string> hasher;
    size_t hash = hasher(key);
    std::string subdir = std::to_string(hash % 256);
    
    fs::path dirPath = fs::path(basePath) / subdir;
    fs::create_directories(dirPath);
    
    // Sanitize key for filename
    std::string filename = key;
    std::replace(filename.begin(), filename.end(), '/', '_');
    std::replace(filename.begin(), filename.end(), '\\', '_');
    
    return (dirPath / filename).string();
}

CompressionType LocalDiskKVStore::detectCompression(const std::string& data) const {
    if (data.empty()) return CompressionType::NONE;
    
    uint8_t marker = data[0];
    if (marker >= static_cast<uint8_t>(CompressionType::NONE) && 
        marker <= static_cast<uint8_t>(CompressionType::CUSTOM_RLE_BITMAP)) {
        return static_cast<CompressionType>(marker);
    }
    
    return CompressionType::NONE;
}

CompressionType LocalDiskKVStore::chooseOptimalCompression(const std::string& data) const {
    size_t dataSize = data.size();
    
    // Heuristics for choosing compression
    if (dataSize < 100) {
        return CompressionType::NONE;  // Too small to compress
    } else if (dataSize < 1024) {
        return CompressionType::SNAPPY;  // Fast compression for small data
    } else if (dataSize < 10240) {
        return CompressionType::LZ4;  // Balanced for medium data
    } else {
        // For large data, test multiple algorithms
        double bestRatio = 1.0;
        CompressionType bestType = CompressionType::ZSTD;
        
        // Check if data looks like time series (mostly numbers)
        bool isTimeSeries = true;
        for (size_t i = 0; i < std::min(size_t(100), dataSize); i += 8) {
            if (i + 8 <= dataSize) {
                int64_t val;
                std::memcpy(&val, data.data() + i, sizeof(val));
                if (val < -1e15 || val > 1e15) {
                    isTimeSeries = false;
                    break;
                }
            }
        }
        
        if (isTimeSeries) {
            return CompressionType::CUSTOM_DELTA;
        }
        
        // Check for repetitive patterns
        std::unordered_map<std::string, int> patterns;
        for (size_t i = 0; i < std::min(size_t(1000), dataSize) - 4; ++i) {
            patterns[data.substr(i, 4)]++;
        }
        
        int maxFreq = 0;
        for (const auto& [pattern, freq] : patterns) {
            maxFreq = std::max(maxFreq, freq);
        }
        
        if (maxFreq > 10) {
            return CompressionType::CUSTOM_DICTIONARY;
        }
        
        // Check for sparse data (many zeros or repeated bytes)
        std::unordered_map<uint8_t, int> byteFreq;
        for (size_t i = 0; i < std::min(size_t(1000), dataSize); ++i) {
            byteFreq[data[i]]++;
        }
        
        int maxByteFreq = 0;
        for (const auto& [byte, freq] : byteFreq) {
            maxByteFreq = std::max(maxByteFreq, freq);
        }
        
        if (maxByteFreq > 500) {
            return CompressionType::CUSTOM_RLE_BITMAP;
        }
        
        return CompressionType::ZSTD;  // Default to ZSTD for large data
    }
}

uint32_t LocalDiskKVStore::calculateChecksum(const std::string& data) const {
    return crc32c::Crc32c(data);
}

void LocalDiskKVStore::put(const std::string& key, const std::string& value, PutOption opt) {
    auto start = std::chrono::high_resolution_clock::now();
    
    // Choose compression
    CompressionType compressionType = opt.compression;
    if (compressionType == CompressionType::AUTO) {
        compressionType = chooseOptimalCompression(value);
    }
    
    // Compress data
    std::string compressed;
    if (compressionType == CompressionType::NONE) {
        compressed = value;
    } else {
        auto it = compressors.find(compressionType);
        if (it != compressors.end()) {
            compressed = it->second->compress(value);
        } else {
            compressed = value;
            compressionType = CompressionType::NONE;
        }
    }
    
    // Write to disk
    std::string filepath = getFilePath(key);
    std::ofstream file(filepath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open file for writing: " + filepath);
    }
    
    file.write(compressed.data(), compressed.size());
    file.close();
    
    // Update metadata
    {
        std::unique_lock lock(metaMutex);
        FileMetadata meta;
        meta.filename = filepath;
        meta.originalSize = value.size();
        meta.compressedSize = compressed.size();
        meta.compression = compressionType;
        meta.checksum = calculateChecksum(value);
        meta.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        
        metadata[key] = meta;
    }
    
    // Update statistics
    stats.totalPuts++;
    stats.totalBytesWritten += compressed.size();
    stats.totalOriginalBytes += value.size();
    stats.totalCompressedBytes += compressed.size();
    stats.compressionUsage[compressionType]++;
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    spdlog::debug("PUT key={}, original_size={}, compressed_size={}, compression={}, ratio={:.2f}, time={}us",
                  key, value.size(), compressed.size(), static_cast<int>(compressionType),
                  static_cast<double>(value.size()) / compressed.size(), duration.count());
}

void LocalDiskKVStore::get(const std::string& key, std::string& value) {
    auto start = std::chrono::high_resolution_clock::now();
    
    // Get metadata
    FileMetadata meta;
    {
        std::shared_lock lock(metaMutex);
        auto it = metadata.find(key);
        if (it == metadata.end()) {
            throw std::runtime_error("Key not found: " + key);
        }
        meta = it->second;
    }
    
    // Read from disk
    std::ifstream file(meta.filename, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open file for reading: " + meta.filename);
    }
    
    size_t size = file.tellg();
    file.seekg(0);
    
    std::string compressed(size, '\0');
    file.read(compressed.data(), size);
    file.close();
    
    // Decompress
    if (meta.compression == CompressionType::NONE) {
        value = compressed;
    } else {
        CompressionType detectedType = detectCompression(compressed);
        auto it = compressors.find(detectedType);
        if (it != compressors.end()) {
            value = it->second->decompress(compressed);
        } else {
            value = compressed;
        }
    }
    
    // Verify checksum
    uint32_t checksum = calculateChecksum(value);
    if (checksum != meta.checksum) {
        spdlog::warn("Checksum mismatch for key={}", key);
    }
    
    // Update statistics
    stats.totalGets++;
    stats.totalBytesRead += compressed.size();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    spdlog::debug("GET key={}, compressed_size={}, decompressed_size={}, time={}us",
                  key, compressed.size(), value.size(), duration.count());
}

void LocalDiskKVStore::saveMetadata() {
    std::string metaPath = fs::path(basePath) / "metadata.bin";
    std::ofstream file(metaPath, std::ios::binary);
    
    uint32_t numEntries = metadata.size();
    file.write(reinterpret_cast<char*>(&numEntries), sizeof(numEntries));
    
    for (const auto& [key, meta] : metadata) {
        uint32_t keyLen = key.size();
        file.write(reinterpret_cast<char*>(&keyLen), sizeof(keyLen));
        file.write(key.data(), keyLen);
        
        uint32_t filenameLen = meta.filename.size();
        file.write(reinterpret_cast<char*>(&filenameLen), sizeof(filenameLen));
        file.write(meta.filename.data(), filenameLen);
        
        file.write(reinterpret_cast<const char*>(&meta.originalSize), sizeof(meta.originalSize));
        file.write(reinterpret_cast<const char*>(&meta.compressedSize), sizeof(meta.compressedSize));
        file.write(reinterpret_cast<const char*>(&meta.compression), sizeof(meta.compression));
        file.write(reinterpret_cast<const char*>(&meta.checksum), sizeof(meta.checksum));
        file.write(reinterpret_cast<const char*>(&meta.timestamp), sizeof(meta.timestamp));
    }
    
    file.close();
}

void LocalDiskKVStore::loadMetadata() {
    std::string metaPath = fs::path(basePath) / "metadata.bin";
    if (!fs::exists(metaPath)) {
        return;
    }
    
    std::ifstream file(metaPath, std::ios::binary);
    
    uint32_t numEntries;
    file.read(reinterpret_cast<char*>(&numEntries), sizeof(numEntries));
    
    for (uint32_t i = 0; i < numEntries; ++i) {
        uint32_t keyLen;
        file.read(reinterpret_cast<char*>(&keyLen), sizeof(keyLen));
        std::string key(keyLen, '\0');
        file.read(key.data(), keyLen);
        
        FileMetadata meta;
        uint32_t filenameLen;
        file.read(reinterpret_cast<char*>(&filenameLen), sizeof(filenameLen));
        meta.filename.resize(filenameLen);
        file.read(meta.filename.data(), filenameLen);
        
        file.read(reinterpret_cast<char*>(&meta.originalSize), sizeof(meta.originalSize));
        file.read(reinterpret_cast<char*>(&meta.compressedSize), sizeof(meta.compressedSize));
        file.read(reinterpret_cast<char*>(&meta.compression), sizeof(meta.compression));
        file.read(reinterpret_cast<char*>(&meta.checksum), sizeof(meta.checksum));
        file.read(reinterpret_cast<char*>(&meta.timestamp), sizeof(meta.timestamp));
        
        metadata[key] = meta;
    }
    
    file.close();
}

void LocalDiskKVStore::printStatistics() const {
    std::cout << "\n=== KVStore Statistics ===" << std::endl;
    std::cout << "Total PUTs: " << stats.totalPuts << std::endl;
    std::cout << "Total GETs: " << stats.totalGets << std::endl;
    std::cout << "Total Bytes Written: " << stats.totalBytesWritten << std::endl;
    std::cout << "Total Bytes Read: " << stats.totalBytesRead << std::endl;
    std::cout << "Average Compression Ratio: " << getAverageCompressionRatio() << std::endl;
    
    std::cout << "\nCompression Usage:" << std::endl;
    for (const auto& [type, count] : stats.compressionUsage) {
        std::cout << "  " << static_cast<int>(type) << ": " << count << std::endl;
    }
}

double LocalDiskKVStore::getAverageCompressionRatio() const {
    if (stats.totalCompressedBytes == 0) return 1.0;
    return static_cast<double>(stats.totalOriginalBytes) / stats.totalCompressedBytes;
}