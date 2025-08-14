#pragma once

#include <unordered_map>
#include <string>
#include <list>
#include <mutex>
#include <optional>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;

// 缓存项元数据（内存+磁盘通用）
struct CacheMetadata {
    std::string key;
    size_t size;  // 数据大小（字节）
    std::chrono::system_clock::time_point last_access;
    bool in_memory;  // 是否在内存中

    CacheMetadata(const std::string& k, size_t s, bool in_mem)
        : key(k), size(s), last_access(std::chrono::system_clock::now()), in_memory(in_mem) {}
};

// 基于双向链表的统一LRU缓存管理器（同时管理内存和磁盘）
template <typename T>
class UnifiedLRUCache {
private:
    size_t max_memory_size_;  // 内存缓存最大总大小（字节）
    size_t max_disk_size_;    // 磁盘缓存最大总大小（字节）
    std::string disk_cache_path_;  // 磁盘缓存路径

    // 内存缓存存储
    std::unordered_map<std::string, T> memory_cache_;
    // LRU链表（维护所有缓存项的访问顺序，包括内存和磁盘）
    std::list<CacheMetadata> lru_list_;
    // 哈希表快速查找元数据
    std::unordered_map<std::string, typename std::list<CacheMetadata>::iterator> metadata_map_;

    size_t current_memory_used_ = 0;  // 当前内存使用量
    size_t current_disk_used_ = 0;    // 当前磁盘使用量
    mutable std::mutex mutex_;

    // 生成安全的磁盘缓存文件名
    std::string get_safe_filename(const std::string& key) const {
        std::string safe_key = key;
        std::replace_if(safe_key.begin(), safe_key.end(),
                       [](char c) { return !std::isalnum(c) && c != '_'; }, '_');
        return disk_cache_path_ + "/" + safe_key + ".cache";
    }

    // 将内存中的项写入磁盘
    void write_to_disk(const std::string& key, const T& value) {
        auto path = get_safe_filename(key);
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) return;

        // 假设T可以序列化为字符串（实际实现需根据T类型调整）
        const std::string& data = *reinterpret_cast<const std::string*>(&value);
        ofs.write(data.data(), data.size());
    }

    // 从磁盘读取项到内存
    std::optional<T> read_from_disk(const std::string& key) {
        auto path = get_safe_filename(key);
        if (!fs::exists(path)) return std::nullopt;

        auto size = fs::file_size(path);
        std::string data(size, '\0');
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.read(&data[0], size)) return std::nullopt;

        return *reinterpret_cast<const T*>(&data);  // 假设T可从字符串构造
    }

    // 清理超出容量的缓存项
    void evict() {
        while ((current_memory_used_ > max_memory_size_ || current_disk_used_ > max_disk_size_) &&
               !lru_list_.empty()) {
            // 从LRU尾部移除最久未使用的项
            auto& meta = lru_list_.back();
            auto it = metadata_map_.find(meta.key);
            
            if (meta.in_memory) {
                // 内存项写入磁盘（如果需要）
                if (current_disk_used_ + meta.size <= max_disk_size_) {
                    write_to_disk(meta.key, memory_cache_[meta.key]);
                    current_disk_used_ += meta.size;
                    meta.in_memory = false;
                } else {
                    // 磁盘也满了，直接删除
                    current_memory_used_ -= meta.size;
                    memory_cache_.erase(meta.key);
                }
            } else {
                // 磁盘项直接删除
                fs::remove(get_safe_filename(meta.key));
                current_disk_used_ -= meta.size;
            }

            metadata_map_.erase(it);
            lru_list_.pop_back();
        }
    }

public:
    UnifiedLRUCache(size_t max_memory_size, size_t max_disk_size, const std::string& disk_path)
        : max_memory_size_(max_memory_size), max_disk_size_(max_disk_size), disk_cache_path_(disk_path) {
        fs::create_directories(disk_cache_path_);
    }

    std::optional<T> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = metadata_map_.find(key);

        if (it == metadata_map_.end()) return std::nullopt;

        // 更新访问时间并移到LRU头部
        auto& meta_it = it->second;
        meta_it->last_access = std::chrono::system_clock::now();
        lru_list_.splice(lru_list_.begin(), lru_list_, meta_it);

        // 从内存或磁盘获取数据
        if (meta_it->in_memory) {
            return memory_cache_[key];
        } else {
            // 从磁盘加载到内存
            auto data = read_from_disk(key);
            if (!data) return std::nullopt;

            // 确保内存有空间
            current_memory_used_ += meta_it->size;
            current_disk_used_ -= meta_it->size;
            meta_it->in_memory = true;
            memory_cache_[key] = *data;
            evict();  // 可能需要淘汰其他项

            return data;
        }
    }

    void put(const std::string& key, const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto size = sizeof(value);  // 实际应根据T类型计算真实大小
        auto path = get_safe_filename(key);

        // 检查是否已存在
        auto it = metadata_map_.find(key);
        if (it != metadata_map_.end()) {
            // 更新现有项
            auto& meta_it = it->second;
            current_memory_used_ -= meta_it->size;
            if (!meta_it->in_memory) {
                current_disk_used_ -= meta_it->size;
                fs::remove(path);
            }

            lru_list_.erase(meta_it);
            metadata_map_.erase(it);
        }

        // 添加新项到内存
        memory_cache_[key] = value;
        current_memory_used_ += size;
        lru_list_.emplace_front(key, size, true);
        metadata_map_[key] = lru_list_.begin();

        // 检查是否需要淘汰
        evict();
    }

    size_t memory_used() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_memory_used_;
    }

    size_t disk_used() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_disk_used_;
    }
};
