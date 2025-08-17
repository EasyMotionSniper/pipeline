// src/reader/order_reader.cpp
#include "reader/order_reader.h"
#include "reader/parquet_parser.h"
#include <fmt/format.h>
#include <algorithm>

OrderReader::OrderReader(std::shared_ptr<IKVStore> kvStore, 
                        int date,
                        const std::vector<int>& skeyList)
    : kvStore(std::move(kvStore)), date(date) {
    
    // Initialize streams for each skey
    for (int skey : skeyList) {
        loadStream(skey);
    }
    
    // Initialize heap with first element from each stream
    for (size_t i = 0; i < streams.size(); ++i) {
        if (streams[i]->hasNext()) {
            minHeap.push({streams[i]->peek(), static_cast<int>(i)});
        }
    }
}

void OrderReader::loadStream(int skey) {
    auto stream = std::make_unique<OrderStream>();
    stream->skey = skey;
    stream->currentIndex = 0;
    
    // Construct S3 key
    std::string key = fmt::format("mdl/mbd/order/{}/{}.parquet", date, skey);
    std::string value;
    
    try {
        kvStore->get(key, value);
        stream->orders = ParquetParser::parseOrderData(value);
    } catch (const std::exception& e) {
        // Log error, stream will be empty
        stream->orders.clear();
    }
    
    streams.push_back(std::move(stream));
}

std::optional<OrderUpdate> OrderReader::nextOrder() {
    if (minHeap.empty()) {
        return std::nullopt;
    }
    
    // Get the minimum element
    auto [order, streamIndex] = minHeap.top();
    minHeap.pop();
    
    // Advance the stream and refill heap
    streams[streamIndex]->next();
    refillHeap(streamIndex);
    
    return order;
}

void OrderReader::refillHeap(int streamIndex) {
    if (streams[streamIndex]->hasNext()) {
        minHeap.push({streams[streamIndex]->peek(), streamIndex});
    }
}

// src/reader/parquet_parser.cpp
#include "reader/parquet_parser.h"
#include <arrow/io/memory.h>
#include <parquet/arrow/reader.h>
#include <arrow/table.h>

std::vector<OrderUpdate> ParquetParser::parseOrderData(const std::string& data) {
    std::vector<OrderUpdate> result;
    
    // Create Arrow memory buffer
    auto buffer = arrow::Buffer::FromString(data);
    auto input = std::make_shared<arrow::io::BufferReader>(buffer);
    
    // Open Parquet file
    auto reader = parquet::arrow::OpenFile(input, arrow::default_memory_pool()).ValueOrDie();
    
    // Read entire file as Arrow table
    std::shared_ptr<arrow::Table> table;
    reader->ReadTable(&table);
    
    // Extract columns
    auto skeyCol = std::static_pointer_cast<arrow::Int32Array>(
        table->column(0)->chunk(0));
    auto timestampCol = std::static_pointer_cast<arrow::Int64Array>(
        table->column(1)->chunk(0));
    auto bidPriceCol = std::static_pointer_cast<arrow::Int32Array>(
        table->column(2)->chunk(0));
    auto bidSizeCol = std::static_pointer_cast<arrow::Int32Array>(
        table->column(3)->chunk(0));
    auto askPriceCol = std::static_pointer_cast<arrow::Int32Array>(
        table->column(4)->chunk(0));
    auto askSizeCol = std::static_pointer_cast<arrow::Int32Array>(
        table->column(5)->chunk(0));
    auto sideCol = std::static_pointer_cast<arrow::StringArray>(
        table->column(6)->chunk(0));
    
    // Convert to OrderUpdate vector
    result.reserve(table->num_rows());
    for (int64_t i = 0; i < table->num_rows(); ++i) {
        OrderUpdate order;
        order.skey = skeyCol->Value(i);
        order.timestamp = timestampCol->Value(i);
        order.bidPrice = bidPriceCol->Value(i);
        order.bidSize = bidSizeCol->Value(i);
        order.askPrice = askPriceCol->Value(i);
        order.askSize = askSizeCol->Value(i);
        order.side = sideCol->GetString(i)[0];
        result.push_back(order);
    }
    
    return result;
}

// src/kvstore/s3_kvstore.cpp
#include "kvstore/s3_kvstore.h"
#include <aws/core/Aws.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <sstream>

S3KVStore::S3KVStore(const std::string& bucket) 
    : bucketName(bucket) {
    Aws::SDKOptions options;
    Aws::InitAPI(options);
    
    Aws::Client::ClientConfiguration config;
    config.region = Aws::Region::US_EAST_1;
    s3Client = std::make_shared<Aws::S3::S3Client>(config);
}

void S3KVStore::get(const std::string& key, std::string& value) {
    // Check memory cache first
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = memoryCache.find(key);
        if (it != memoryCache.end()) {
            value = it->second;
            return;
        }
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
    
    // Read the object data
    auto& stream = outcome.GetResult().GetBody();
    std::stringstream ss;
    ss << stream.rdbuf();
    value = ss.str();
    
    // Update memory cache
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        memoryCache[key] = value;
    }
}

std::vector<std::string> S3KVStore::listKeys(const std::string& prefix) {
    std::vector<std::string> keys;
    
    Aws::S3::Model::ListObjectsV2Request request;
    request.SetBucket(bucketName);
    request.SetPrefix(prefix);
    
    bool done = false;
    while (!done) {
        auto outcome = s3Client->ListObjectsV2(request);
        if (!outcome.IsSuccess()) {
            throw std::runtime_error("Failed to list objects: " + 
                                    outcome.GetError().GetMessage());
        }
        
        for (const auto& object : outcome.GetResult().GetContents()) {
            keys.push_back(object.GetKey());
        }
        
        if (outcome.GetResult().GetIsTruncated()) {
            request.SetContinuationToken(outcome.GetResult().GetNextContinuationToken());
        } else {
            done = true;
        }
    }
    
    return keys;
}

// src/cache/cache_manager.cpp
#include "cache/cache_manager.h"
#include <fstream>
#include <filesystem>
#include <chrono>
#include <json/json.h>

namespace fs = std::filesystem;

CacheManager::CacheManager(const std::string& dir, size_t maxSize)
    : cacheDir(dir), maxCacheSize(maxSize), currentCacheSize(0) {
    
    // Create cache directory if it doesn't exist
    fs::create_directories(cacheDir);
    
    // Load existing metadata
    loadMetadata();
}

bool CacheManager::exists(const std::string& key) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    return cacheMap.find(key) != cacheMap.end();
}

std::string CacheManager::getCachePath(const std::string& key) {
    // Convert S3 key to safe filename
    std::string filename = key;
    std::replace(filename.begin(), filename.end(), '/', '_');
    return fs::path(cacheDir) / filename;
}

void CacheManager::put(const std::string& key, const std::string& data) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    // Check if we need to evict
    while (currentCacheSize + data.size() > maxCacheSize && !lruList.empty()) {
        evictLRU();
    }
    
    // Write to disk
    std::string cachePath = getCachePath(key);
    std::ofstream file(cachePath, std::ios::binary);
    file.write(data.c_str(), data.size());
    file.close();
    
    // Update metadata
    CacheEntry entry;
    entry.key = key;
    entry.filePath = cachePath;
    entry.size = data.size();
    entry.lastAccessTime = std::chrono::system_clock::now().time_since_epoch().count();
    entry.accessCount = 1;
    
    cacheMap[key] = entry;
    lruList.push_front(key);
    currentCacheSize += data.size();
    
    // Persist metadata periodically
    persistMetadata();
}

bool CacheManager::get(const std::string& key, std::string& data) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    auto it = cacheMap.find(key);
    if (it == cacheMap.end()) {
        return false;
    }
    
    // Read from disk
    std::ifstream file(it->second.filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }
    
    size_t size = file.tellg();
    file.seekg(0);
    data.resize(size);
    file.read(&data[0], size);
    file.close();
    
    // Update access info
    updateAccessInfo(key);
    
    return true;
}

void CacheManager::evictLRU() {
    if (lruList.empty()) return;
    
    std::string keyToEvict = lruList.back();
    lruList.pop_back();
    
    auto it = cacheMap.find(keyToEvict);
    if (it != cacheMap.end()) {
        // Delete file
        fs::remove(it->second.filePath);
        currentCacheSize -= it->second.size;
        cacheMap.erase(it);
    }
}

void CacheManager::updateAccessInfo(const std::string& key) {
    auto it = cacheMap.find(key);
    if (it != cacheMap.end()) {
        it->second.lastAccessTime = std::chrono::system_clock::now().time_since_epoch().count();
        it->second.accessCount++;
        
        // Move to front of LRU list
        lruList.remove(key);
        lruList.push_front(key);
    }
}

void CacheManager::persistMetadata() {
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
    
    // Write to metadata file
    std::ofstream file(fs::path(cacheDir) / "metadata.json");
    Json::StreamWriterBuilder builder;
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &file);
    file.close();
}

void CacheManager::loadMetadata() {
    std::string metadataPath = fs::path(cacheDir) / "metadata.json";
    if (!fs::exists(metadataPath)) {
        return;
    }
    
    std::ifstream file(metadataPath);
    Json::Value root;
    file >> root;
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

// src/kvstore/cached_kvstore.cpp
#include "kvstore/cached_kvstore.h"

CachedKVStore::CachedKVStore(std::shared_ptr<IKVStore> base,
                           const std::string& cacheDir,
                           size_t maxCacheSize)
    : baseStore(std::move(base)),
      cacheManager(std::make_unique<CacheManager>(cacheDir, maxCacheSize)) {
}

void CachedKVStore::get(const std::string& key, std::string& value) {
    // Try cache first
    if (cacheManager->get(key, value)) {
        return;
    }
    
    // Fetch from base store
    baseStore->get(key, value);
    
    // Cache the result
    cacheManager->put(key, value);
}

void CachedKVStore::put(const std::string& key, const std::string& value) {
    // Write through to base store
    baseStore->put(key, value);
    
    // Update cache
    cacheManager->put(key, value);
}

std::vector<std::string> CachedKVStore::listKeys(const std::string& prefix) {
    return baseStore->listKeys(prefix);
}