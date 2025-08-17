// include/optimized_cache_kv_store.h
#pragma once
#include "ikv_store.h"
#include "types.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>

class OptimizedCacheKVStore : public IKVStore {
private:
    std::shared_ptr<IKVStore> underlying_store_;
    std::string cache_dir_;
    mutable std::mutex cache_mutex_;
    
    struct CacheEntry {
        std::string original_key;
        std::string cache_path;
        size_t num_records;
        int64_t min_timestamp;
        int64_t max_timestamp;
        bool is_sorted;
        bool is_indexed;
        std::vector<size_t> time_index; // Offsets for time-based access
    };
    
    std::unordered_map<std::string, CacheEntry> cache_map_;
    
    // Optimization methods
    void convertToBinaryFormat(const std::string& parquet_data, 
                              const std::string& cache_path);
    void createTimeIndex(const std::string& cache_path, CacheEntry& entry);
    void sortByTimestamp(std::vector<OrderUpdate>& orders);
    std::string serializeBinary(const std::vector<OrderUpdate>& orders);
    std::vector<OrderUpdate> deserializeBinary(const std::string& data);
    
    // Cache management
    std::string getCachePath(const std::string& key) const;
    bool loadFromOptimizedCache(const std::string& key, std::string& value);
    void saveToOptimizedCache(const std::string& key, const std::string& value);
    
public:
    OptimizedCacheKVStore(std::shared_ptr<IKVStore> underlying_store,
                         const std::string& cache_dir = "/tmp/order_cache_opt");
    virtual ~OptimizedCacheKVStore();
    
    void get(const std::string& key, std::string& value) override;
    void put(const std::string& key, const std::string& value) override;
    
    // Optimized access methods
    std::vector<OrderUpdate> getOrdersInTimeRange(const std::string& key,
                                                  int64_t start_time,
                                                  int64_t end_time);
    
    // Batch optimization
    void optimizeBatch(const std::vector<std::string>& keys);
    
    // Statistics
    void printCacheStats() const;
};

// src/optimized_cache_kv_store.cpp
#include "optimized_cache_kv_store.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>

namespace fs = std::filesystem;

OptimizedCacheKVStore::OptimizedCacheKVStore(std::shared_ptr<IKVStore> underlying_store,
                                             const std::string& cache_dir)
    : underlying_store_(underlying_store), cache_dir_(cache_dir) {
    fs::create_directories(cache_dir_);
    fs::create_directories(cache_dir_ + "/binary");
    fs::create_directories(cache_dir_ + "/index");
}

OptimizedCacheKVStore::~OptimizedCacheKVStore() = default;

std::string OptimizedCacheKVStore::getCachePath(const std::string& key) const {
    std::string filename = key;
    std::replace(filename.begin(), filename.end(), '/', '_');
    return cache_dir_ + "/binary/" + filename + ".bin";
}

std::string OptimizedCacheKVStore::serializeBinary(const std::vector<OrderUpdate>& orders) {
    std::string result;
    size_t total_size = sizeof(size_t) + orders.size() * sizeof(OrderUpdate);
    result.reserve(total_size);
    
    // Write number of records
    size_t num_records = orders.size();
    result.append(reinterpret_cast<const char*>(&num_records), sizeof(num_records));
    
    // Write records
    for (const auto& order : orders) {
        result.append(reinterpret_cast<const char*>(&order), sizeof(OrderUpdate));
    }
    
    return result;
}

std::vector<OrderUpdate> OptimizedCacheKVStore::deserializeBinary(const std::string& data) {
    std::vector<OrderUpdate> orders;
    
    if (data.size() < sizeof(size_t)) {
        return orders;
    }
    
    const char* ptr = data.data();
    size_t num_records = *reinterpret_cast<const size_t*>(ptr);
    ptr += sizeof(size_t);
    
    orders.reserve(num_records);
    for (size_t i = 0; i < num_records; ++i) {
        orders.push_back(*reinterpret_cast<const OrderUpdate*>(ptr));
        ptr += sizeof(OrderUpdate);
    }
    
    return orders;
}

void OptimizedCacheKVStore::sortByTimestamp(std::vector<OrderUpdate>& orders) {
    std::sort(orders.begin(), orders.end(), 
              [](const OrderUpdate& a, const OrderUpdate& b) {
                  return a.timestamp < b.timestamp;
              });
}

void OptimizedCacheKVStore::convertToBinaryFormat(const std::string& parquet_data,
                                                  const std::string& cache_path) {
    // Parse parquet data
    auto buffer = arrow::Buffer::FromString(parquet_data);
    auto input = std::make_shared<arrow::io::BufferReader>(buffer);
    
    std::unique_ptr<parquet::arrow::FileReader> reader;
    parquet::arrow::OpenFile(input, arrow::default_memory_pool(), &reader);
    
    std::shared_ptr<arrow::Table> table;
    reader->ReadTable(&table);
    
    // Convert to OrderUpdate vector
    std::vector<OrderUpdate> orders;
    int64_t num_rows = table->num_rows();
    orders.reserve(num_rows);
    
    auto skey_array = std::static_pointer_cast<arrow::Int32Array>(
        table->column(0)->chunk(0));
    auto timestamp_array = std::static_pointer_cast<arrow::Int64Array>(
        table->column(1)->chunk(0));
    auto bid_price_array = std::static_pointer_cast<arrow::Int32Array>(
        table->column(2)->chunk(0));
    auto bid_size_array = std::static_pointer_cast<arrow::Int32Array>(
        table->column(3)->chunk(0));
    auto ask_price_array = std::static_pointer_cast<arrow::Int32Array>(
        table->column(4)->chunk(0));
    auto ask_size_array = std::static_pointer_cast<arrow::Int32Array>(
        table->column(5)->chunk(0));
    auto side_array = std::static_pointer_cast<arrow::StringArray>(
        table->column(6)->chunk(0));
    
    for (int64_t i = 0; i < num_rows; ++i) {
        OrderUpdate order;
        order.skey = skey_array->Value(i);
        order.timestamp = timestamp_array->Value(i);
        order.bidPrice = bid_price_array->Value(i);
        order.bidSize = bid_size_array->Value(i);
        order.askPrice = ask_price_array->Value(i);
        order.askSize = ask_size_array->Value(i);
        order.side = side_array->GetString(i)[0];
        orders.push_back(order);
    }
    
    // Sort by timestamp
    sortByTimestamp(orders);
    
    // Serialize to binary
    std::string binary_data = serializeBinary(orders);
    
    // Save to cache
    std::ofstream file(cache_path, std::ios::binary);
    file.write(binary_data.data(), binary_data.size());
    file.close();
}

void OptimizedCacheKVStore::createTimeIndex(const std::string& cache_path, 
                                           CacheEntry& entry) {
    // Read binary data
    std::ifstream file(cache_path, std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
    file.close();
    
    auto orders = deserializeBinary(data);
    
    if (orders.empty()) return;
    
    entry.min_timestamp = orders.front().timestamp;
    entry.max_timestamp = orders.back().timestamp;
    entry.num_records = orders.size();
    
    // Create time index (every 1000 records)
    const size_t index_interval = 1000;
    for (size_t i = 0; i < orders.size(); i += index_interval) {
        entry.time_index.push_back(i * sizeof(OrderUpdate) + sizeof(size_t));
    }
    
    entry.is_sorted = true;
    entry.is_indexed = true;
}

bool OptimizedCacheKVStore::loadFromOptimizedCache(const std::string& key, 
                                                   std::string& value) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    auto it = cache_map_.find(key);
    if (it == cache_map_.end()) {
        return false;
    }
    
    std::ifstream file(it->second.cache_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    value = std::string((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return true;
}

void OptimizedCacheKVStore::saveToOptimizedCache(const std::string& key,
                                                 const std::string& value) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    std::string cache_path = getCachePath(key);
    
    // Convert parquet to optimized binary format
    convertToBinaryFormat(value, cache_path);
    
    CacheEntry entry;
    entry.original_key = key;
    entry.cache_path = cache_path;
    
    // Create time index
    createTimeIndex(cache_path, entry);
    
    cache_map_[key] = entry;
}

void OptimizedCacheKVStore::get(const std::string& key, std::string& value) {
    // Try optimized cache first
    if (loadFromOptimizedCache(key, value)) {
        return;
    }
    
    // Get from underlying store
    underlying_store_->get(key, value);
    
    // Save to optimized cache
    saveToOptimizedCache(key, value);
    
    // Return the binary format
    loadFromOptimizedCache(key, value);
}

void OptimizedCacheKVStore::put(const std::string& key, const std::string& value) {
    underlying_store_->put(key, value);
    saveToOptimizedCache(key, value);
}

std::vector<OrderUpdate> OptimizedCacheKVStore::getOrdersInTimeRange(
    const std::string& key, int64_t start_time, int64_t end_time) {
    
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    auto it = cache_map_.find(key);
    if (it == cache_map_.end()) {
        // Load data first
        std::string value;
        get(key, value);
        it = cache_map_.find(key);
    }
    
    if (it->second.min_timestamp > end_time || it->second.max_timestamp < start_time) {
        return {}; // No data in range
    }
    
    // Load binary data
    std::ifstream file(it->second.cache_path, std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
    file.close();
    
    auto all_orders = deserializeBinary(data);
    
    // Binary search for start and end positions
    auto start_it = std::lower_bound(all_orders.begin(), all_orders.end(),
                                     start_time,
                                     [](const OrderUpdate& order, int64_t time) {
                                         return order.timestamp < time;
                                     });
    
    auto end_it = std::upper_bound(all_orders.begin(), all_orders.end(),
                                   end_time,
                                   [](int64_t time, const OrderUpdate& order) {
                                       return time < order.timestamp;
                                   });
    
    return std::vector<OrderUpdate>(start_it, end_it);
}

void OptimizedCacheKVStore::optimizeBatch(const std::vector<std::string>& keys) {
    for (const auto& key : keys) {
        std::string value;
        get(key, value); // This will trigger optimization
    }
}

void OptimizedCacheKVStore::printCacheStats() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    std::cout << "Cache Statistics:" << std::endl;
    std::cout << "Total cached keys: " << cache_map_.size() << std::endl;
    
    size_t total_records = 0;
    size_t total_size = 0;
    
    for (const auto& [key, entry] : cache_map_) {
        total_records += entry.num_records;
        if (fs::exists(entry.cache_path)) {
            total_size += fs::file_size(entry.cache_path);
        }
    }
    
    std::cout << "Total records: " << total_records << std::endl;
    std::cout << "Total cache size: " << total_size / (1024.0 * 1024.0) << " MB" << std::endl;
}