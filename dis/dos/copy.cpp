// Fetch from S3
Aws::S3::Model::GetObjectRequest request;
request.SetBucket(bucketName);
request.SetKey(key);

auto outcome = s3Client->GetObject(request);
if (!outcome.IsSuccess()) {
    throw std::runtime_error("Failed to get object from S3: " + 
                            outcome.GetError().GetMessage());
}

// 1. 获取 S3 对象的大小（从响应头中获取，无额外IO）
auto& result = outcome.GetResult();
std::streamsize contentLength = result.GetContentLength();
if (contentLength < 0) {
    // 极端情况：S3 未返回 Content-Length（罕见，如动态生成的对象），用备用方案
    throw std::runtime_error("S3 object Content-Length is unknown");
}

// 2. 预分配 std::string 的内存（避免动态扩容拷贝）
std::string value;
value.reserve(static_cast<size_t>(contentLength));  // 提前分配足够空间

// 3. 直接从 S3 流读取到 string 中（仅一次拷贝）
auto& stream = result.GetBody();
// 用 istreambuf_iterator 直接访问流的缓冲区，写入 string 的末尾
std::copy(
    std::istreambuf_iterator<char>(stream),  // 流的起始迭代器
    std::istreambuf_iterator<char>(),        // 流的结束迭代器（EOF）
    std::back_inserter(value)                // 写入 string（back_inserter 调用 push_back）
);

// 4. 验证读取大小（可选，确保数据完整）
if (value.size() != static_cast<size_t>(contentLength)) {
    throw std::runtime_error("Incomplete read from S3: expected " + 
                            std::to_string(contentLength) + " bytes, got " + 
                            std::to_string(value.size()) + " bytes");
}





// Fetch from S3
Aws::S3::Model::GetObjectRequest request;
request.SetBucket(bucketName);
request.SetKey(key);

auto outcome = s3Client->GetObject(request);
if (!outcome.IsSuccess()) {
    throw std::runtime_error("Failed to get object from S3: " + 
                             outcome.GetError().GetMessage());
}

// Read the object data directly into a string
auto& stream = outcome.GetResult().GetBody();
std::string value;
value.reserve(stream.tellg());  // Reserve enough space to avoid reallocations
value.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
