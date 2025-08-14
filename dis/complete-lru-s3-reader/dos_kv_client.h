#pragma once

#include "ikvstore.h"
#include "lru_cache.h"
#include <aws/s3/S3Client.h>
#include <string>
#include <memory>

class DOSKVClient : public IKVStore {
private:
    std::string bucket_name_;
    std::shared_ptr<aws::s3::S3Client> s3_client_;
    std::unique_ptr<UnifiedLRUCache<std::string>> unified_cache_;  // 统一LRU缓存

    void init_aws_sdk();
    bool download_from_s3(const std::string& key, std::string& value);

public:
    DOSKVClient(const std::string& bucket_name,
               size_t max_memory_cache_size = 1024 * 1024 * 50,  // 50MB
               size_t max_disk_cache_size = 1024 * 1024 * 1024,   // 1GB
               const std::string& disk_cache_path = "./s3_unified_cache");

    ~DOSKVClient() override;

    bool get(const std::string& key, std::string& value) override;
    void put(const std::string& key, const std::string& value) override;

    // 用于测试和监控的接口
    size_t memory_used() const { return unified_cache_->memory_used(); }
    size_t disk_used() const { return unified_cache_->disk_used(); }
};
