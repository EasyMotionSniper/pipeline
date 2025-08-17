// ============================================================================
// Example 1: Using nlohmann/json
// ============================================================================

// src/cache/cache_metadata_nlohmann.cpp
#include "cache/cache_manager.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

void CacheManager::persistMetadata() {
    json root;
    json entries = json::array();
    
    for (const auto& [key, entry] : cacheMap) {
        json entryJson;
        entryJson["key"] = entry.key;
        entryJson["filePath"] = entry.filePath;
        entryJson["size"] = entry.size;
        entryJson["lastAccessTime"] = entry.lastAccessTime;
        entryJson["accessCount"] = entry.accessCount;
        entries.push_back(entryJson);
    }
    
    root["entries"] = entries;
    root["currentSize"] = currentCacheSize.load();
    root["version"] = "1.0.0";
    root["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
    
    // Write to file with pretty printing
    std::ofstream file(fs::path(cacheDir) / "metadata.json");
    file << root.dump(4); // 4 spaces indentation
    file.close();
}

void CacheManager::loadMetadata() {
    std::string metadataPath = fs::path(cacheDir) / "metadata.json";
    if (!fs::exists(metadataPath)) {
        return;
    }
    
    std::ifstream file(metadataPath);
    json root = json::parse(file);
    file.close();
    
    // Check version compatibility
    if (root.contains("version") && root["version"] != "1.0.0") {
        spdlog::warn("Metadata version mismatch, clearing cache");
        return;
    }
    
    currentCacheSize = root["currentSize"].get<size_t>();
    
    for (const auto& entryJson : root["entries"]) {
        CacheEntry entry;
        entry.key = entryJson["key"].get<std::string>();
        entry.filePath = entryJson["filePath"].get<std::string>();
        entry.size = entryJson["size"].get<size_t>();
        entry.lastAccessTime = entryJson["lastAccessTime"].get<int64_t>();
        entry.accessCount = entryJson["accessCount"].get<int>();
        
        // Verify file still exists
        if (fs::exists(entry.filePath)) {
            cacheMap[entry.key] = entry;
            lruList.push_back(entry.key);
        }
    }
}

// ============================================================================
// Example 2: Using JsonCpp (if you prefer it over nlohmann)
// ============================================================================

// src/cache/cache_metadata_jsoncpp.cpp
#include "cache/cache_manager.h"
#include <json/json.h>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

void CacheManager::persistMetadataJsonCpp() {
    Json::Value root;
    Json::Value entries(Json::arrayValue);
    
    for (const auto& [key, entry] : cacheMap) {
        Json::Value entryJson;
        entryJson["key"] = entry.key;
        entryJson["filePath"] = entry.filePath;
        entryJson["size"] = static_cast<Json::UInt64>(entry.size);
        entryJson["lastAccessTime"] = static_cast<Json::Int64>(entry.lastAccessTime);
        entryJson["accessCount"] = entry.accessCount;
        entries.append(entryJson);
    }
    
    root["entries"] = entries;
    root["currentSize"] = static_cast<Json::UInt64>(currentCacheSize.load());
    
    // Write with custom writer for pretty printing
    std::ofstream file(fs::path(cacheDir) / "metadata.json");
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "    ";  // 4 spaces
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &file);
    file.close();
}

void CacheManager::loadMetadataJsonCpp() {
    std::string metadataPath = fs::path(cacheDir) / "metadata.json";
    if (!fs::exists(metadataPath)) {
        return;
    }
    
    std::ifstream file(metadataPath);
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        spdlog::error("Failed to parse metadata: {}", errors);
        return;
    }
    file.close();
    
    currentCacheSize = root["currentSize"].asUInt64();
    
    for (const auto& entryJson : root["entries"]) {
        CacheEntry entry;
        entry.key = entryJson["key"].asString();
        entry.filePath = entryJson["filePath"].asString();
        entry.size = entryJson["size"].asUInt64();
        entry.lastAccessTime = entryJson["lastAccessTime"].asInt64();
        entry.accessCount = entryJson["accessCount"].asInt();
        
        // Verify file still exists
        if (fs::exists(entry.filePath)) {
            cacheMap[entry.key] = entry;
            lruList.push_back(entry.key);
        }
    }
}
