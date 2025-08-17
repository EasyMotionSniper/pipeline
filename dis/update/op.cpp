// include/types.h
#pragma once
#include <cstdint>

// 基础Update类型
template<typename T>
struct BaseUpdate {
    int skey;
    int64_t timestamp;
    int bidPrice;
    int bidSize;
    int askPrice;
    int askSize;
    char side;
    
    bool operator<(const T& other) const {
        return timestamp > other.timestamp; // For min-heap
    }
};

struct OrderUpdate : BaseUpdate<OrderUpdate> {};
struct TradeUpdate : BaseUpdate<TradeUpdate> {};

using Order = OrderUpdate;
using Trade = TradeUpdate;

// include/ikv_store.h
#pragma once
#include <string>
#include <memory>

class IKVStore {
public:
    virtual ~IKVStore() = default;
    virtual void get(const std::string& key, std::string& value) = 0;
    virtual void put(const std::string& key, const std::string& value) = 0;
};

// include/base_reader.h
#pragma once
#include "types.h"
#include "ikv_store.h"
#include <memory>
#include <optional>
#include <vector>
#include <queue>
#include <unordered_map>

// 通用的多路归并读取器基类
template<typename UpdateType>
class BaseReader {
protected:
    struct DataSource {
        int skey;
        std::vector<UpdateType> updates;
        size_t current_idx;
        
        DataSource(int sk) : skey(sk), current_idx(0) {}
        
        bool hasNext() const {
            return current_idx < updates.size();
        }
        
        const UpdateType& peek() const {
            return updates[current_idx];
        }
        
        UpdateType next() {
            return updates[current_idx++];
        }
    };
    
    struct UpdateComparator {
        bool operator()(const std::pair<UpdateType, int>& a,
                       const std::pair<UpdateType, int>& b) const {
            return a.first.timestamp > b.first.timestamp;
        }
    };
    
    std::shared_ptr<IKVStore> kv_store_;
    int date_;
    std::string data_type_prefix_;  // "order" or "trade"
    std::vector<std::unique_ptr<DataSource>> sources_;
    std::priority_queue<std::pair<UpdateType, int>, 
                       std::vector<std::pair<UpdateType, int>>,
                       UpdateComparator> pq_;
    
    size_t total_processed_ = 0;
    
    // 构造S3 key
    std::string constructKey(int skey) const {
        return std::to_string(date_) + "/" + std::to_string(skey) + ".parquet";
    }
    
    // 子类需要实现的解析方法
    virtual std::vector<UpdateType> parseParquetData(const std::string& data) = 0;
    
    void loadData(int skey) {
        auto source = std::make_unique<DataSource>(skey);
        std::string key = constructKey(skey);
        
        try {
            std::string parquet_data;
            kv_store_->get(key, parquet_data);
            source->updates = parseParquetData(parquet_data);
        } catch (const std::exception& e) {
            std::cerr << "Failed to load data for skey " << skey 
                     << ": " << e.what() << std::endl;
        }
        
        sources_.push_back(std::move(source));
    }
    
public:
    BaseReader(std::shared_ptr<IKVStore> kvStore, 
               int date,
               const std::vector<int>& skeyList,
               const std::string& dataType)
        : kv_store_(kvStore), date_(date), data_type_prefix_(dataType) {
        
        // Load data for each skey
        for (int skey : skeyList) {
            loadData(skey);
        }
        
        // Initialize priority queue
        for (size_t i = 0; i < sources_.size(); ++i) {
            if (sources_[i]->hasNext()) {
                pq_.push({sources_[i]->peek(), static_cast<int>(i)});
                sources_[i]->current_idx++;
            }
        }
    }
    
    std::optional<UpdateType> nextUpdate() {
        if (pq_.empty()) {
            return std::nullopt;
        }
        
        auto [update, source_idx] = pq_.top();
        pq_.pop();
        
        if (sources_[source_idx]->hasNext()) {
            pq_.push({sources_[source_idx]->next(), source_idx});
        }
        
        total_processed_++;
        return update;
    }
    
    size_t getTotalProcessed() const { return total_processed_; }
    
    size_t getMemoryUsage() const {
        size_t total = sizeof(*this);
        for (const auto& source : sources_) {
            total += sizeof(*source);
            total += source->updates.size() * sizeof(UpdateType);
        }
        return total;
    }
};

// include/order_reader.h
#pragma once
#include "base_reader.h"

class OrderReader : public BaseReader<OrderUpdate> {
protected:
    std::vector<OrderUpdate> parseParquetData(const std::string& data) override;
    
public:
    OrderReader(std::shared_ptr<IKVStore> kvStore, 
                int date,
                const std::vector<int>& skeyList)
        : BaseReader(kvStore, date, skeyList, "order") {}
    
    std::optional<Order> nextOrder() {
        return nextUpdate();
    }
};

// include/trade_reader.h
#pragma once
#include "base_reader.h"

class TradeReader : public BaseReader<TradeUpdate> {
protected:
    std::vector<TradeUpdate> parseParquetData(const std::string& data) override;
    
public:
    TradeReader(std::shared_ptr<IKVStore> kvStore,
                int date,
                const std::vector<int>& skeyList)
        : BaseReader(kvStore, date, skeyList, "trade") {}
    
    std::optional<Trade> nextTrade() {
        return nextUpdate();
    }
};

// include/s3_kv_store.h
#pragma once
#include "ikv_store.h"
#include <aws/s3/S3Client.h>
#include <memory>

class S3KVStore : public IKVStore {
private:
    std::shared_ptr<Aws::S3::S3Client> s3_client_;
    std::string bucket_name_;
    std::string prefix_;
    
public:
    S3KVStore(const std::string& bucket, 
              const std::string& prefix = "mdl/mbd/");
    
    void get(const std::string& key, std::string& value) override;
    void put(const std::string& key, const std::string& value) override;
    
    void setDataType(const std::string& dataType) {
        prefix_ = "mdl/mbd/" + dataType + "/";
    }
};

// include/lru_disk_cache_kv_store.h
#pragma once
#include "ikv_store.h"
#include <memory>
#include <list>
#include <unordered_map>
#include <mutex>
#include <filesystem>

class LRUDiskCacheKVStore : public IKVStore {
private:
    std::shared_ptr<IKVStore> underlying_store_;
    std::string cache_dir_;
    size_t max_cache_size_;  // Maximum cache size in bytes
    size_t current_cache_size_;
    
    // LRU implementation
    using CacheKey = std::string;
    struct CacheEntry {
        std::string file_path;
        size_t file_size;
    };
    
    std::list<CacheKey> lru_list_;
    std::unordered_map<CacheKey, std::pair<
        typename std::list<CacheKey>::iterator, CacheEntry>> cache_map_;
    
    mutable std::mutex cache_mutex_;
    
    // Helper methods
    std::string getCachePath(const std::string& key) const;
    void touchEntry(const std::string& key);
    void evictOldest();
    void addToCache(const std::string& key, const std::string& value);
    bool loadFromCache(const std::string& key, std::string& value);
    size_t getFileSize(const std::string& path) const;
    
public:
    LRUDiskCacheKVStore(std::shared_ptr<IKVStore> underlying_store,
                        const std::string& cache_dir = "/tmp/order_cache",
                        size_t max_cache_size_mb = 1024); // 1GB default
    
    ~LRUDiskCacheKVStore();
    
    void get(const std::string& key, std::string& value) override;
    void put(const std::string& key, const std::string& value) override;
    
    void clearCache();
    size_t getCurrentCacheSize() const { return current_cache_size_; }
    size_t getCacheEntryCount() const { return cache_map_.size(); }
    void printCacheStats() const;
};

// src/lru_disk_cache_kv_store.cpp
#include "lru_disk_cache_kv_store.h"
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

LRUDiskCacheKVStore::LRUDiskCacheKVStore(
    std::shared_ptr<IKVStore> underlying_store,
    const std::string& cache_dir,
    size_t max_cache_size_mb)
    : underlying_store_(underlying_store)
    , cache_dir_(cache_dir)
    , max_cache_size_(max_cache_size_mb * 1024 * 1024)
    , current_cache_size_(0) {
    
    fs::create_directories(cache_dir_);
    
    // Calculate existing cache size
    for (const auto& entry : fs::directory_iterator(cache_dir_)) {
        if (entry.is_regular_file()) {
            current_cache_size_ += entry.file_size();
        }
    }
}

LRUDiskCacheKVStore::~LRUDiskCacheKVStore() = default;

std::string LRUDiskCacheKVStore::getCachePath(const std::string& key) const {
    std::string filename = key;
    std::replace(filename.begin(), filename.end(), '/', '_');
    return cache_dir_ + "/" + filename;
}

void LRUDiskCacheKVStore::touchEntry(const std::string& key) {
    auto it = cache_map_.find(key);
    if (it != cache_map_.end()) {
        // Move to front of LRU list
        lru_list_.erase(it->second.first);
        lru_list_.push_front(key);
        it->second.first = lru_list_.begin();
    }
}

void LRUDiskCacheKVStore::evictOldest() {
    while (!lru_list_.empty() && current_cache_size_ > max_cache_size_) {
        const std::string& oldest_key = lru_list_.back();
        auto it = cache_map_.find(oldest_key);
        
        if (it != cache_map_.end()) {
            // Remove file
            fs::remove(it->second.second.file_path);
            current_cache_size_ -= it->second.second.file_size;
            
            // Remove from cache structures
            cache_map_.erase(it);
            lru_list_.pop_back();
            
            std::cout << "Evicted cache entry: " << oldest_key 
                     << " (freed " << it->second.second.file_size / 1024 
                     << " KB)" << std::endl;
        }
    }
}

void LRUDiskCacheKVStore::addToCache(const std::string& key, 
                                     const std::string& value) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    std::string cache_path = getCachePath(key);
    size_t file_size = value.size();
    
    // Check if we need to evict entries
    while (current_cache_size_ + file_size > max_cache_size_ && !lru_list_.empty()) {
        evictOldest();
    }
    
    // Write to file
    std::ofstream file(cache_path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to create cache file: " << cache_path << std::endl;
        return;
    }
    file.write(value.data(), value.size());
    file.close();
    
    // Update cache structures
    lru_list_.push_front(key);
    CacheEntry entry{cache_path, file_size};
    cache_map_[key] = {lru_list_.begin(), entry};
    current_cache_size_ += file_size;
}

bool LRUDiskCacheKVStore::loadFromCache(const std::string& key, 
                                        std::string& value) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    auto it = cache_map_.find(key);
    if (it == cache_map_.end()) {
        return false;
    }
    
    // Read from file
    std::ifstream file(it->second.second.file_path, std::ios::binary);
    if (!file.is_open()) {
        // File missing, remove from cache
        cache_map_.erase(it);
        lru_list_.erase(it->second.first);
        return false;
    }
    
    std::ostringstream ss;
    ss << file.rdbuf();
    value = ss.str();
    
    // Update LRU
    touchEntry(key);
    
    return true;
}

void LRUDiskCacheKVStore::get(const std::string& key, std::string& value) {
    // Try cache first
    if (loadFromCache(key, value)) {
        return;
    }
    
    // Fetch from underlying store
    underlying_store_->get(key, value);
    
    // Add to cache
    addToCache(key, value);
}

void LRUDiskCacheKVStore::put(const std::string& key, const std::string& value) {
    underlying_store_->put(key, value);
    addToCache(key, value);
}

void LRUDiskCacheKVStore::clearCache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    for (const auto& [key, entry] : cache_map_) {
        fs::remove(entry.second.file_path);
    }
    
    cache_map_.clear();
    lru_list_.clear();
    current_cache_size_ = 0;
}

void LRUDiskCacheKVStore::printCacheStats() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    std::cout << "=== LRU Disk Cache Stats ===" << std::endl;
    std::cout << "Cache entries: " << cache_map_.size() << std::endl;
    std::cout << "Current size: " << current_cache_size_ / (1024.0 * 1024.0) << " MB" << std::endl;
    std::cout << "Max size: " << max_cache_size_ / (1024.0 * 1024.0) << " MB" << std::endl;
    std::cout << "Usage: " << (current_cache_size_ * 100.0 / max_cache_size_) << "%" << std::endl;
}

// include/local_sliced_kv_store.h
#pragma once
#include "ikv_store.h"
#include <memory>
#include <vector>
#include <mutex>
#include <unordered_map>

class LocalSlicedKVStore : public IKVStore {
private:
    std::shared_ptr<IKVStore> underlying_store_;
    std::string slice_cache_dir_;
    size_t slice_size_;  // Target size for each slice in bytes
    
    struct SliceInfo {
        int num_slices;
        std::vector<size_t> slice_offsets;  // Start offset of each slice
        std::vector<size_t> slice_sizes;
        size_t total_size;
        std::string cache_file_path;  // Path to the complete cached file
    };
    
    mutable std::mutex slice_mutex_;
    std::unordered_map<std::string, SliceInfo> slice_map_;
    
    // Helper methods
    void createLocalSlices(const std::string& key, const std::string& data);
    std::string getCacheFilePath(const std::string& key) const;
    bool isSliced(const std::string& key) const;
    void loadSliceInfo(const std::string& key);
    
public:
    LocalSlicedKVStore(std::shared_ptr<IKVStore> underlying_store,
                      const std::string& cache_dir = "/tmp/sliced_cache",
                      size_t slice_size = 2 * 1024 * 1024); // 2MB default
    
    ~LocalSlicedKVStore();
    
    void get(const std::string& key, std::string& value) override;
    void put(const std::string& key, const std::string& value) override;
    
    // Get specific slice (0-indexed)
    bool getSlice(const std::string& key, int slice_idx, std::string& value);
    
    // Get range of data without loading entire file
    bool getRange(const std::string& key, size_t start_offset, 
                 size_t end_offset, std::string& value);
    
    // Get slice containing specific offset
    int getSliceForOffset(const std::string& key, size_t offset);
    
    void printSliceInfo(const std::string& key) const;
};

// src/local_sliced_kv_store.cpp
#include "local_sliced_kv_store.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

LocalSlicedKVStore::LocalSlicedKVStore(
    std::shared_ptr<IKVStore> underlying_store,
    const std::string& cache_dir,
    size_t slice_size)
    : underlying_store_(underlying_store)
    , slice_cache_dir_(cache_dir)
    , slice_size_(slice_size) {
    
    fs::create_directories(slice_cache_dir_);
    fs::create_directories(slice_cache_dir_ + "/data");
    fs::create_directories(slice_cache_dir_ + "/meta");
}

LocalSlicedKVStore::~LocalSlicedKVStore() = default;

std::string LocalSlicedKVStore::getCacheFilePath(const std::string& key) const {
    std::string filename = key;
    std::replace(filename.begin(), filename.end(), '/', '_');
    return slice_cache_dir_ + "/data/" + filename;
}

void LocalSlicedKVStore::createLocalSlices(const std::string& key, 
                                          const std::string& data) {
    std::lock_guard<std::mutex> lock(slice_mutex_);
    
    SliceInfo info;
    info.total_size = data.size();
    info.num_slices = (data.size() + slice_size_ - 1) / slice_size_;
    info.cache_file_path = getCacheFilePath(key);
    
    // Save complete data to cache
    std::ofstream file(info.cache_file_path, std::ios::binary);
    file.write(data.data(), data.size());
    file.close();
    
    // Calculate slice offsets and sizes
    for (int i = 0; i < info.num_slices; ++i) {
        size_t offset = i * slice_size_;
        size_t size = std::min(slice_size_, data.size() - offset);
        
        info.slice_offsets.push_back(offset);
        info.slice_sizes.push_back(size);
    }
    
    slice_map_[key] = info;
    
    // Save metadata
    std::string meta_file = slice_cache_dir_ + "/meta/" + 
                           key.substr(key.find_last_of('/') + 1) + ".meta";
    std::ofstream meta(meta_file);
    meta << info.num_slices << "\n";
    meta << info.total_size << "\n";
    for (int i = 0; i < info.num_slices; ++i) {
        meta << info.slice_offsets[i] << " " << info.slice_sizes[i] << "\n";
    }
    meta.close();
}

bool LocalSlicedKVStore::isSliced(const std::string& key) const {
    std::lock_guard<std::mutex> lock(slice_mutex_);
    return slice_map_.find(key) != slice_map_.end();
}

void LocalSlicedKVStore::loadSliceInfo(const std::string& key) {
    if (isSliced(key)) {
        return;
    }
    
    // Try to load from underlying store and create slices
    std::string data;
    underlying_store_->get(key, data);
    createLocalSlices(key, data);
}

void LocalSlicedKVStore::get(const std::string& key, std::string& value) {
    loadSliceInfo(key);
    
    std::lock_guard<std::mutex> lock(slice_mutex_);
    auto it = slice_map_.find(key);
    if (it == slice_map_.end()) {
        throw std::runtime_error("Failed to load sliced data for key: " + key);
    }
    
    // Read complete file
    std::ifstream file(it->second.cache_file_path, std::ios::binary);
    std::ostringstream ss;
    ss << file.rdbuf();
    value = ss.str();
}

void LocalSlicedKVStore::put(const std::string& key, const std::string& value) {
    underlying_store_->put(key, value);
    createLocalSlices(key, value);
}

bool LocalSlicedKVStore::getSlice(const std::string& key, int slice_idx, 
                                  std::string& value) {
    loadSliceInfo(key);
    
    std::lock_guard<std::mutex> lock(slice_mutex_);
    auto it = slice_map_.find(key);
    if (it == slice_map_.end() || slice_idx >= it->second.num_slices) {
        return false;
    }
    
    const SliceInfo& info = it->second;
    
    // Read specific slice
    std::ifstream file(info.cache_file_path, std::ios::binary);
    file.seekg(info.slice_offsets[slice_idx]);
    
    value.resize(info.slice_sizes[slice_idx]);
    file.read(&value[0], info.slice_sizes[slice_idx]);
    
    return true;
}

bool LocalSlicedKVStore::getRange(const std::string& key, 
                                  size_t start_offset,
                                  size_t end_offset, 
                                  std::string& value) {
    loadSliceInfo(key);
    
    std::lock_guard<std::mutex> lock(slice_mutex_);
    auto it = slice_map_.find(key);
    if (it == slice_map_.end()) {
        return false;
    }
    
    const SliceInfo& info = it->second;
    
    // Validate range
    if (start_offset >= info.total_size) {
        return false;
    }
    end_offset = std::min(end_offset, info.total_size);
    
    // Read range
    std::ifstream file(info.cache_file_path, std::ios::binary);
    file.seekg(start_offset);
    
    size_t read_size = end_offset - start_offset;
    value.resize(read_size);
    file.read(&value[0], read_size);
    
    return true;
}

int LocalSlicedKVStore::getSliceForOffset(const std::string& key, size_t offset) {
    loadSliceInfo(key);
    
    std::lock_guard<std::mutex> lock(slice_mutex_);
    auto it = slice_map_.find(key);
    if (it == slice_map_.end()) {
        return -1;
    }
    
    return offset / slice_size_;
}

void LocalSlicedKVStore::printSliceInfo(const std::string& key) const {
    std::lock_guard<std::mutex> lock(slice_mutex_);
    
    auto it = slice_map_.find(key);
    if (it == slice_map_.end()) {
        std::cout << "No slice info for key: " << key << std::endl;
        return;
    }
    
    const SliceInfo& info = it->second;
    std::cout << "=== Slice Info for " << key << " ===" << std::endl;
    std::cout << "Total size: " << info.total_size << " bytes" << std::endl;
    std::cout << "Number of slices: " << info.num_slices << std::endl;
    std::cout << "Slice size: " << slice_size_ << " bytes" << std::endl;
    
    for (int i = 0; i < info.num_slices; ++i) {
        std::cout << "  Slice " << i << ": offset=" << info.slice_offsets[i]
                 << ", size=" << info.slice_sizes[i] << std::endl;
    }
}