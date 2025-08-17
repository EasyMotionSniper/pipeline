// include/disk_cache_kv_store.h
#pragma once
#include "ikv_store.h"
#include <memory>
#include <string>
#include <mutex>
#include <unordered_map>

class DiskCacheKVStore : public IKVStore {
private:
    std::shared_ptr<IKVStore> underlying_store_;
    std::string cache_dir_;
    mutable std::mutex cache_mutex_;
    std::unordered_map<std::string, std::string> cache_metadata_;
    
    std::string getCachePath(const std::string& key) const;
    bool isCached(const std::string& key) const;
    void saveToCache(const std::string& key, const std::string& value);
    bool loadFromCache(const std::string& key, std::string& value) const;
    void ensureCacheDir() const;
    
public:
    DiskCacheKVStore(std::shared_ptr<IKVStore> underlying_store,
                     const std::string& cache_dir = "/tmp/order_cache");
    virtual ~DiskCacheKVStore();
    
    void get(const std::string& key, std::string& value) override;
    void put(const std::string& key, const std::string& value) override;
    
    void clearCache();
    size_t getCacheSize() const;
};

// src/disk_cache_kv_store.cpp
#include "disk_cache_kv_store.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <sstream>

namespace fs = std::filesystem;

DiskCacheKVStore::DiskCacheKVStore(std::shared_ptr<IKVStore> underlying_store,
                                   const std::string& cache_dir)
    : underlying_store_(underlying_store), cache_dir_(cache_dir) {
    ensureCacheDir();
}

DiskCacheKVStore::~DiskCacheKVStore() = default;

void DiskCacheKVStore::ensureCacheDir() const {
    fs::create_directories(cache_dir_);
}

std::string DiskCacheKVStore::getCachePath(const std::string& key) const {
    // Replace '/' with '_' to create valid filename
    std::string filename = key;
    std::replace(filename.begin(), filename.end(), '/', '_');
    return cache_dir_ + "/" + filename;
}

bool DiskCacheKVStore::isCached(const std::string& key) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    std::string cache_path = getCachePath(key);
    
    if (!fs::exists(cache_path)) {
        return false;
    }
    
    // Check if cache is still valid (e.g., not too old)
    auto file_time = fs::last_write_time(cache_path);
    auto now = fs::file_time_type::clock::now();
    auto age = std::chrono::duration_cast<std::chrono::hours>(now - file_time);
    
    // Cache is valid for 24 hours
    return age.count() < 24;
}

bool DiskCacheKVStore::loadFromCache(const std::string& key, std::string& value) const {
    std::string cache_path = getCachePath(key);
    
    std::ifstream file(cache_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    std::ostringstream ss;
    ss << file.rdbuf();
    value = ss.str();
    
    return true;
}

void DiskCacheKVStore::saveToCache(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    std::string cache_path = getCachePath(key);
    std::ofstream file(cache_path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open cache file for writing: " << cache_path << std::endl;
        return;
    }
    
    file.write(value.data(), value.size());
    file.close();
    
    // Update metadata
    cache_metadata_[key] = std::to_string(value.size());
}

void DiskCacheKVStore::get(const std::string& key, std::string& value) {
    // Try to load from cache first
    if (isCached(key) && loadFromCache(key, value)) {
        return;
    }
    
    // Fetch from underlying store
    underlying_store_->get(key, value);
    
    // Save to cache
    saveToCache(key, value);
}

void DiskCacheKVStore::put(const std::string& key, const std::string& value) {
    // Write through to underlying store
    underlying_store_->put(key, value);
    
    // Update cache
    saveToCache(key, value);
}

void DiskCacheKVStore::clearCache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    fs::remove_all(cache_dir_);
    ensureCacheDir();
    cache_metadata_.clear();
}

size_t DiskCacheKVStore::getCacheSize() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    size_t total_size = 0;
    
    for (const auto& entry : fs::directory_iterator(cache_dir_)) {
        if (entry.is_regular_file()) {
            total_size += entry.file_size();
        }
    }
    
    return total_size;
}