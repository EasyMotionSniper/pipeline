// include/sliced_kv_store.h
#pragma once
#include "ikv_store.h"
#include <memory>
#include <vector>
#include <mutex>

class SlicedKVStore : public IKVStore {
private:
    std::shared_ptr<IKVStore> underlying_store_;
    size_t slice_size_;  // Target size for each slice in bytes
    mutable std::mutex slice_mutex_;
    
    struct SliceInfo {
        std::string key_prefix;
        int num_slices;
        size_t total_size;
        std::vector<size_t> slice_sizes;
    };
    
    std::unordered_map<std::string, SliceInfo> slice_map_;
    
    // Helper methods
    std::vector<std::string> getSliceKeys(const std::string& key) const;
    void createSlices(const std::string& key, const std::string& value);
    std::string mergeSlices(const std::vector<std::string>& slice_keys);
    
public:
    SlicedKVStore(std::shared_ptr<IKVStore> underlying_store,
                  size_t slice_size = 2 * 1024 * 1024); // 2MB default
    virtual ~SlicedKVStore();
    
    void get(const std::string& key, std::string& value) override;
    void put(const std::string& key, const std::string& value) override;
    
    // Get specific slice
    void getSlice(const std::string& key, int slice_idx, std::string& value);
    
    // Get range of data without loading entire file
    void getRange(const std::string& key, size_t start_offset, 
                  size_t end_offset, std::string& value);
};

// src/sliced_kv_store.cpp
#include "sliced_kv_store.h"
#include <sstream>
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>

SlicedKVStore::SlicedKVStore(std::shared_ptr<IKVStore> underlying_store,
                             size_t slice_size)
    : underlying_store_(underlying_store), slice_size_(slice_size) {
}

SlicedKVStore::~SlicedKVStore() = default;

std::vector<std::string> SlicedKVStore::getSliceKeys(const std::string& key) const {
    std::vector<std::string> slice_keys;
    
    auto it = slice_map_.find(key);
    if (it != slice_map_.end()) {
        for (int i = 0; i < it->second.num_slices; ++i) {
            slice_keys.push_back(key + "-" + std::to_string(i));
        }
    } else {
        // Try to read metadata from underlying store
        std::string metadata_key = key + ".meta";
        std::string metadata;
        try {
            underlying_store_->get(metadata_key, metadata);
            // Parse metadata to get number of slices
            int num_slices = std::stoi(metadata);
            for (int i = 0; i < num_slices; ++i) {
                slice_keys.push_back(key + "-" + std::to_string(i));
            }
        } catch (...) {
            // No slices, try original key
            slice_keys.push_back(key);
        }
    }
    
    return slice_keys;
}

void SlicedKVStore::createSlices(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(slice_mutex_);
    
    size_t total_size = value.size();
    int num_slices = (total_size + slice_size_ - 1) / slice_size_;
    
    SliceInfo info;
    info.key_prefix = key;
    info.num_slices = num_slices;
    info.total_size = total_size;
    
    for (int i = 0; i < num_slices; ++i) {
        size_t start = i * slice_size_;
        size_t end = std::min(start + slice_size_, total_size);
        size_t slice_len = end - start;
        
        std::string slice_key = key + "-" + std::to_string(i);
        std::string slice_data = value.substr(start, slice_len);
        
        underlying_store_->put(slice_key, slice_data);
        info.slice_sizes.push_back(slice_len);
    }
    
    // Store metadata
    std::string metadata = std::to_string(num_slices);
    underlying_store_->put(key + ".meta", metadata);
    
    slice_map_[key] = info;
}

std::string SlicedKVStore::mergeSlices(const std::vector<std::string>& slice_keys) {
    std::ostringstream result;
    
    for (const auto& slice_key : slice_keys) {
        std::string slice_data;
        underlying_store_->get(slice_key, slice_data);
        result << slice_data;
    }
    
    return result.str();
}

void SlicedKVStore::get(const std::string& key, std::string& value) {
    auto slice_keys = getSliceKeys(key);
    
    if (slice_keys.size() == 1 && slice_keys[0] == key) {
        // No slicing, direct get
        underlying_store_->get(key, value);
    } else {
        // Merge slices
        value = mergeSlices(slice_keys);
    }
}

void SlicedKVStore::put(const std::string& key, const std::string& value) {
    if (value.size() > slice_size_) {
        createSlices(key, value);
    } else {
        underlying_store_->put(key, value);
    }
}

void SlicedKVStore::getSlice(const std::string& key, int slice_idx, std::string& value) {
    std::string slice_key = key + "-" + std::to_string(slice_idx);
    underlying_store_->get(slice_key, value);
}

void SlicedKVStore::getRange(const std::string& key, size_t start_offset,
                            size_t end_offset, std::string& value) {
    // Calculate which slices we need
    int start_slice = start_offset / slice_size_;
    int end_slice = (end_offset - 1) / slice_size_;
    
    std::ostringstream result;
    
    for (int i = start_slice; i <= end_slice; ++i) {
        std::string slice_data;
        getSlice(key, i, slice_data);
        
        if (i == start_slice && i == end_slice) {
            // Single slice case
            size_t local_start = start_offset % slice_size_;
            size_t local_end = end_offset % slice_size_;
            if (local_end == 0) local_end = slice_data.size();
            result << slice_data.substr(local_start, local_end - local_start);
        } else if (i == start_slice) {
            // First slice
            size_t local_start = start_offset % slice_size_;
            result << slice_data.substr(local_start);
        } else if (i == end_slice) {
            // Last slice
            size_t local_end = end_offset % slice_size_;
            if (local_end == 0) local_end = slice_data.size();
            result << slice_data.substr(0, local_end);
        } else {
            // Middle slices
            result << slice_data;
        }
    }
    
    value = result.str();
}

// Enhanced OrderReader for sliced data
class SlicedOrderReader : public OrderReader {
private:
    std::shared_ptr<SlicedKVStore> sliced_store_;
    
    // Load only needed portions of data
    void loadOrderDataSliced(int skey, int64_t start_time, int64_t end_time);
    
public:
    SlicedOrderReader(std::shared_ptr<SlicedKVStore> kvStore, int date,
                      const std::vector<int>& skeyList)
        : OrderReader(kvStore, date, skeyList), sliced_store_(kvStore) {
        // Custom initialization for sliced reading
    }
    
    // Load data on demand for specific time ranges
    void loadTimeRange(int64_t start_time, int64_t end_time);
};