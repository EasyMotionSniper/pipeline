#include "dos_kv_client.h"
#include <aws/core/Aws.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <sstream>

void DOSKVClient::init_aws_sdk() {
    static std::once_flag flag;
    std::call_once(flag, []() {
        Aws::SDKOptions options;
        Aws::InitAPI(options);
    });
}

DOSKVClient::DOSKVClient(const std::string& bucket_name,
                         size_t max_memory_cache_size,
                         size_t max_disk_cache_size,
                         const std::string& disk_cache_path)
    : bucket_name_(bucket_name) {
    init_aws_sdk();
    s3_client_ = std::make_shared<aws::s3::S3Client>();
    unified_cache_ = std::make_unique<UnifiedLRUCache<std::string>>(
        max_memory_cache_size, max_disk_cache_size, disk_cache_path);
}

DOSKVClient::~DOSKVClient() {
    // 注意：AWS SDK通常在程序退出时关闭，而非单个客户端销毁时
}

bool DOSKVClient::download_from_s3(const std::string& key, std::string& value) {
    aws::s3::model::GetObjectRequest request;
    request.SetBucket(bucket_name_);
    request.SetKey(key);

    auto outcome = s3_client_->GetObject(request);
    if (!outcome.IsSuccess()) return false;

    std::stringstream ss;
    ss << outcome.GetResult().GetBody().rdbuf();
    value = ss.str();
    return true;
}

bool DOSKVClient::get(const std::string& key, std::string& value) {
    // 1. 先查统一缓存
    auto cached = unified_cache_->get(key);
    if (cached.has_value()) {
        value = *cached;
        return true;
    }

    // 2. 从S3下载
    if (download_from_s3(key, value)) {
        unified_cache_->put(key, value);  // 存入缓存
        return true;
    }

    return false;
}

void DOSKVClient::put(const std::string& key, const std::string& value) {
    // 1. 更新缓存
    unified_cache_->put(key, value);

    // 2. 上传到S3
    aws::s3::model::PutObjectRequest request;
    request.SetBucket(bucket_name_);
    request.SetKey(key);

    auto input_data = Aws::MakeShared<Aws::StringStream>("DOSKVClient");
    *input_data << value;
    request.SetBody(input_data);

    s3_client_->PutObject(request);
}
