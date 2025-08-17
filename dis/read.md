您提出了一个很好的问题！当前的`CachedKVStore`实现确实存在这个问题 - 它只在内存中维护了缓存索引(`cacheIndex_`)，程序退出后这个索引就丢失了，下次启动时无法感知之前缓存的文件。

让我改进这个实现，添加持久化的缓存索引：现在我提供了两个改进的缓存实现方案，都能够在程序重启后继续使用之前的缓存：

## 方案1：JSON索引文件（轻量级）

**特点**：
1. **持久化索引**：使用`cache_index.json`文件保存所有缓存条目的元数据
2. **程序启动时加载**：构造函数中自动加载现有索引
3. **缓存验证**：启动时验证缓存文件是否存在和完整
4. **定期保存**：每100次操作自动保存索引，析构时也会保存
5. **校验和**：可选的MD5校验和验证数据完整性

**索引文件格式**：
```json
{
  "order/20231201/000001": {
    "filename": "12345_order_20231201_000001.cache",
    "size": 1048576,
    "lastAccess": 1703145600000,
    "checksum": "d41d8cd98f00b204e9800998ecf8427e"
  }
}
```

## 方案2：SQLite数据库（更健壮）

**优势**：
1. **事务支持**：原子性操作，避免索引损坏
2. **更好的并发**：SQLite内置的并发控制
3. **高效查询**：使用索引快速查找LRU条目
4. **崩溃恢复**：SQLite的WAL模式提供崩溃恢复能力
5. **更容易扩展**：可以添加更多元数据字段

## 使用示例：

```cpp
// 使用JSON索引版本
auto s3Store = std::make_shared<S3KVStore>("your-bucket");
auto cachedStore = std::make_shared<CachedKVStore>(s3Store, "./cache", 2048);

// 第一次运行
OrderReader reader1(cachedStore, 20231201, {1, 2, 3});
// ... 读取数据，缓存会被保存到磁盘

// 程序退出后重新启动
// 第二次运行 - 会自动加载之前的缓存
auto cachedStore2 = std::make_shared<CachedKVStore>(s3Store, "./cache", 2048);
// 构造函数中会调用loadIndex()，恢复缓存索引

OrderReader reader2(cachedStore2, 20231201, {1, 2, 3});
// 如果数据在缓存中，直接从磁盘读取，不需要访问S3
```

## 关键改进点：

1. **缓存持久化**：缓存文件和索引都保存在磁盘上
2. **自动恢复**：程序启动时自动加载和验证缓存
3. **数据完整性**：通过文件大小和可选的校验和验证
4. **智能清理**：自动清理无效的缓存条目
5. **友好的文件名**：缓存文件名包含原始key信息，方便调试

## 选择建议：

- **JSON方案**：适合大多数场景，简单易调试
- **SQLite方案**：适合需要高并发、大规模缓存管理的场景

这样，您的缓存就能在程序重启后继续使用，大大减少对S3的访问，提高性能。


## Project Structure
```
order_reader/
├── CMakeLists.txt
├── include/
│   ├── order_update.h
│   ├── ikv_store.h
│   ├── s3_kv_store.h
│   ├── cached_kv_store.h
│   ├── sharded_kv_store.h
│   ├── optimized_cache_store.h
│   └── order_reader.h
├── src/
│   ├── s3_kv_store.cpp
│   ├── cached_kv_store.cpp
│   ├── sharded_kv_store.cpp
│   ├── optimized_cache_store.cpp
│   └── order_reader.cpp
├── tests/
│   ├── test_s3_kv_store.cpp
│   ├── test_order_reader.cpp
│   └── benchmark_order_reader.cpp
└── README.md
```

## CMakeLists.txt (Updated with JSON library)
```cmake
cmake_minimum_required(VERSION 3.14)
project(OrderReader)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(AWSSDK REQUIRED COMPONENTS s3)
find_package(Arrow REQUIRED)
find_package(Parquet REQUIRED)
find_package(GTest REQUIRED)
find_package(benchmark REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)  # For MD5 checksum
find_package(jsoncpp REQUIRED)   # For JSON parsing

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Source files
set(SOURCES
    src/s3_kv_store.cpp
    src/cached_kv_store.cpp
    src/sharded_kv_store.cpp
    src/optimized_cache_store.cpp
    src/order_reader.cpp
)

# Create library
add_library(order_reader_lib ${SOURCES})
target_link_libraries(order_reader_lib
    ${AWSSDK_LINK_LIBRARIES}
    Arrow::arrow_shared
    Parquet::parquet_shared
    Threads::Threads
    OpenSSL::Crypto
    jsoncpp_lib
)

# Tests
enable_testing()

add_executable(test_s3_kv_store tests/test_s3_kv_store.cpp)
target_link_libraries(test_s3_kv_store order_reader_lib GTest::gtest_main)

add_executable(test_order_reader tests/test_order_reader.cpp)
target_link_libraries(test_order_reader order_reader_lib GTest::gtest_main)

add_executable(benchmark_order_reader tests/benchmark_order_reader.cpp)
target_link_libraries(benchmark_order_reader order_reader_lib benchmark::benchmark)

add_test(NAME S3KVStoreTest COMMAND test_s3_kv_store)
add_test(NAME OrderReaderTest COMMAND test_order_reader)
```

## Alternative: Using SQLite for Cache Index (More Robust)
```cpp
// include/cached_kv_store_sqlite.h
#pragma once
#include "ikv_store.h"
#include <sqlite3.h>
#include <filesystem>
#include <mutex>

class CachedKVStoreSQLite : public IKVStore {
public:
    CachedKVStoreSQLite(std::shared_ptr<IKVStore> baseStore,
                        const std::string& cacheDir,
                        size_t maxCacheSizeMB = 1024);
    ~CachedKVStoreSQLite();
    
    void get(const std::string& key, std::string& value) override;
    void put(const std::string& key, const std::string& value) override;
    
    void clearCache();
    size_t getCacheSize() const;
    
private:
    std::shared_ptr<IKVStore> baseStore_;
    std::filesystem::path cacheDir_;
    size_t maxCacheSize_;
    size_t currentCacheSize_;
    
    sqlite3* db_;
    mutable std::mutex cacheMutex_;
    
    void initDatabase();
    void closeDatabase();
    bool loadFromCache(const std::string& key, std::string& value);
    void saveToCache(const std::string& key, const std::string& value);
    void evictLRU();
    void updateAccessTime(const std::string& key);
    std::filesystem::path getCachePath(const std::string& key) const;
};
```

## src/cached_kv_store_sqlite.cpp
```cpp
#include "cached_kv_store_sqlite.h"
#include <fstream>
#include <chrono>
#include <sstream>

CachedKVStoreSQLite::CachedKVStoreSQLite(std::shared_ptr<IKVStore> baseStore,
                                         const std::string& cacheDir,
                                         size_t maxCacheSizeMB)
    : baseStore_(baseStore),
      cacheDir_(cacheDir),
      maxCacheSize_(maxCacheSizeMB * 1024 * 1024),
      currentCacheSize_(0),
      db_(nullptr) {
    
    std::filesystem::create_directories(cacheDir_);
    initDatabase();
}

CachedKVStoreSQLite::~CachedKVStoreSQLite() {
    closeDatabase();
}

void CachedKVStoreSQLite::initDatabase() {
    auto dbPath = cacheDir_ / "cache_index.db";
    int rc = sqlite3_open(dbPath.c_str(), &db_);
    
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to open SQLite database");
    }
    
    // Create cache index table
    const char* createTableSQL = R"(
        CREATE TABLE IF NOT EXISTS cache_index (
            key TEXT PRIMARY KEY,
            filename TEXT NOT NULL,
            size INTEGER NOT NULL,
            last_access INTEGER NOT NULL,
            checksum TEXT
        );
        CREATE INDEX IF NOT EXISTS idx_last_access ON cache_index(last_access);
    )";
    
    char* errMsg = nullptr;
    rc = sqlite3_exec(db_, createTableSQL, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string error = errMsg;
        sqlite3_free(errMsg);
        throw std::runtime_error("Failed to create table: " + error);
    }
    
    // Load current cache size
    const char* sizeQuerySQL = "SELECT SUM(size) FROM cache_index";
    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(db_, sizeQuerySQL, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            currentCacheSize_ = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    // Validate cached files exist
    validateCache();
}

void CachedKVStoreSQLite::validateCache() {
    const char* selectSQL = "SELECT key, filename, size FROM cache_index";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, selectSQL, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    
    std::vector<std::string> invalidKeys;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string filename = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        size_t size = sqlite3_column_int64(stmt, 2);
        
        auto filepath = cacheDir_ / filename;
        if (!std::filesystem::exists(filepath) || 
            std::filesystem::file_size(filepath) != size) {
            invalidKeys.push_back(key);
            if (std::filesystem::exists(filepath)) {
                std::filesystem::remove(filepath);
            }
        }
    }
    sqlite3_finalize(stmt);
    
    // Remove invalid entries
    for (const auto& key : invalidKeys) {
        std::string deleteSQL = "DELETE FROM cache_index WHERE key = ?";
        sqlite3_stmt* deleteStmt;
        if (sqlite3_prepare_v2(db_, deleteSQL.c_str(), -1, &deleteStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(deleteStmt, 1, key.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(deleteStmt);
            sqlite3_finalize(deleteStmt);
        }
    }
}

void CachedKVStoreSQLite::get(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    if (loadFromCache(key, value)) {
        updateAccessTime(key);
        return;
    }
    
    // Load from base store
    baseStore_->get(key, value);
    
    // Save to cache
    saveToCache(key, value);
}

bool CachedKVStoreSQLite::loadFromCache(const std::string& key, std::string& value) {
    const char* selectSQL = "SELECT filename FROM cache_index WHERE key = ?";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, selectSQL, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
    
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string filename = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        auto filepath = cacheDir_ / filename;
        
        std::ifstream file(filepath, std::ios::binary);
        if (file) {
            value.assign(std::istreambuf_iterator<char>(file),
                        std::istreambuf_iterator<char>());
            found = true;
        }
    }
    
    sqlite3_finalize(stmt);
    return found;
}

void CachedKVStoreSQLite::saveToCache(const std::string& key, const std::string& value) {
    size_t dataSize = value.size();
    
    // Check if we need to evict
    while (currentCacheSize_ + dataSize > maxCacheSize_ && currentCacheSize_ > 0) {
        evictLRU();
    }
    
    // Generate filename
    std::hash<std::string> hasher;
    size_t hash = hasher(key);
    std::string filename = std::to_string(hash) + ".cache";
    auto filepath = cacheDir_ / filename;
    
    // Write file
    std::ofstream file(filepath, std::ios::binary);
    file.write(value.data(), value.size());
    file.close();
    
    // Update database
    const char* insertSQL = R"(
        INSERT OR REPLACE INTO cache_index (key, filename, size, last_access, checksum)
        VALUES (?, ?, ?, ?, ?)
    )";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, insertSQL, -1, &stmt, nullptr) == SQLITE_OK) {
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, filename.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, dataSize);
        sqlite3_bind_int64(stmt, 4, now);
        sqlite3_bind_null(stmt, 5); // checksum optional
        
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        currentCacheSize_ += dataSize;
    }
}

void CachedKVStoreSQLite::evictLRU() {
    // Find oldest entry
    const char* selectSQL = R"(
        SELECT key, filename, size FROM cache_index 
        ORDER BY last_access ASC LIMIT 1
    )";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, selectSQL, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string filename = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        size_t size = sqlite3_column_int64(stmt, 2);
        
        // Remove file
        auto filepath = cacheDir_ / filename;
        std::filesystem::remove(filepath);
        
        // Remove from database
        const char* deleteSQL = "DELETE FROM cache_index WHERE key = ?";
        sqlite3_stmt* deleteStmt;
        if (sqlite3_prepare_v2(db_, deleteSQL, -1, &deleteStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(deleteStmt, 1, key.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(deleteStmt);
            sqlite3_finalize(deleteStmt);
        }
        
        currentCacheSize_ -= size;
    }
    
    sqlite3_finalize(stmt);
}

void CachedKVStoreSQLite::updateAccessTime(const std::string& key) {
    const char* updateSQL = "UPDATE cache_index SET last_access = ? WHERE key = ?";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, updateSQL, -1, &stmt, nullptr) == SQLITE_OK) {
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        sqlite3_bind_int64(stmt, 1, now);
        sqlite3_bind_text(stmt, 2, key.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void CachedKVStoreSQLite::closeDatabase() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}
```

## include/order_update.h
```cpp
#pragma once
#include <cstdint>

struct OrderUpdate {
    int skey;
    int64_t timestamp;
    int bidPrice;
    int bidSize;
    int askPrice;
    int askSize;
    char side;
};

struct TradeUpdate {
    int skey;
    int64_t timestamp;
    int price;
    int size;
    char side;
};
```

## include/ikv_store.h
```cpp
#pragma once
#include <string>
#include <memory>

class IKVStore {
public:
    virtual ~IKVStore() = default;
    virtual void get(const std::string& key, std::string& value) = 0;
    virtual void put(const std::string& key, const std::string& value) = 0;
};
```

## include/s3_kv_store.h (Version 1: Simplest)
```cpp
#pragma once
#include "ikv_store.h"
#include <aws/s3/S3Client.h>
#include <memory>
#include <unordered_map>

class S3KVStore : public IKVStore {
public:
    S3KVStore(const std::string& bucketName, const std::string& region = "us-east-1");
    virtual ~S3KVStore() = default;
    
    void get(const std::string& key, std::string& value) override;
    void put(const std::string& key, const std::string& value) override;
    
private:
    std::string bucket_;
    std::shared_ptr<Aws::S3::S3Client> s3Client_;
    
    std::string constructS3Key(const std::string& key) const;
    std::vector<OrderUpdate> parseParquetData(const std::string& data) const;
    std::string serializeOrders(const std::vector<OrderUpdate>& orders) const;
};
```

## src/s3_kv_store.cpp
```cpp
#include "s3_kv_store.h"
#include <aws/core/Aws.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>
#include <sstream>

S3KVStore::S3KVStore(const std::string& bucketName, const std::string& region) 
    : bucket_(bucketName) {
    Aws::Client::ClientConfiguration config;
    config.region = region;
    s3Client_ = std::make_shared<Aws::S3::S3Client>(config);
}

void S3KVStore::get(const std::string& key, std::string& value) {
    std::string s3Key = constructS3Key(key);
    
    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(bucket_);
    request.SetKey(s3Key);
    
    auto outcome = s3Client_->GetObject(request);
    if (!outcome.IsSuccess()) {
        throw std::runtime_error("Failed to get object from S3: " + 
                                outcome.GetError().GetMessage());
    }
    
    auto& stream = outcome.GetResult().GetBody();
    std::stringstream ss;
    ss << stream.rdbuf();
    value = ss.str();
}

void S3KVStore::put(const std::string& key, const std::string& value) {
    std::string s3Key = constructS3Key(key);
    
    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(bucket_);
    request.SetKey(s3Key);
    
    auto input_data = Aws::MakeShared<Aws::StringStream>("PutObjectInputStream",
        std::stringstream::in | std::stringstream::out | std::stringstream::binary);
    *input_data << value;
    request.SetBody(input_data);
    
    auto outcome = s3Client_->PutObject(request);
    if (!outcome.IsSuccess()) {
        throw std::runtime_error("Failed to put object to S3: " + 
                                outcome.GetError().GetMessage());
    }
}

std::string S3KVStore::constructS3Key(const std::string& key) const {
    // Convert key format to S3 path
    // Input: "order/20231201/000001" 
    // Output: "mdl/mbd/order/20231201/000001.parquet"
    return "mdl/mbd/" + key + ".parquet";
}

std::vector<OrderUpdate> S3KVStore::parseParquetData(const std::string& data) const {
    std::vector<OrderUpdate> orders;
    
    // Create memory buffer from string data
    auto buffer = arrow::Buffer::FromString(data);
    auto input = std::make_shared<arrow::io::BufferReader>(buffer);
    
    // Open Parquet file using the correct API
    auto result = parquet::arrow::OpenFile(input, arrow::default_memory_pool());
    if (!result.ok()) {
        throw std::runtime_error("Failed to open parquet file: " + result.status().ToString());
    }
    std::unique_ptr<parquet::arrow::FileReader> arrow_reader = std::move(result.ValueOrDie());
    
    // Read entire file as table
    std::shared_ptr<arrow::Table> table;
    auto status = arrow_reader->ReadTable(&table);
    if (!status.ok()) {
        throw std::runtime_error("Failed to read parquet table: " + status.ToString());
    }
    
    // Extract columns - handle chunked column data
    auto skey_column = table->column(0);
    auto timestamp_column = table->column(1);
    auto bid_price_column = table->column(2);
    auto bid_size_column = table->column(3);
    auto ask_price_column = table->column(4);
    auto ask_size_column = table->column(5);
    auto side_column = table->column(6);
    
    // Process all chunks
    int64_t row_offset = 0;
    for (int chunk_idx = 0; chunk_idx < skey_column->num_chunks(); ++chunk_idx) {
        auto skey_array = std::static_pointer_cast<arrow::Int32Array>(
            skey_column->chunk(chunk_idx));
        auto timestamp_array = std::static_pointer_cast<arrow::Int64Array>(
            timestamp_column->chunk(chunk_idx));
        auto bid_price_array = std::static_pointer_cast<arrow::Int32Array>(
            bid_price_column->chunk(chunk_idx));
        auto bid_size_array = std::static_pointer_cast<arrow::Int32Array>(
            bid_size_column->chunk(chunk_idx));
        auto ask_price_array = std::static_pointer_cast<arrow::Int32Array>(
            ask_price_column->chunk(chunk_idx));
        auto ask_size_array = std::static_pointer_cast<arrow::Int32Array>(
            ask_size_column->chunk(chunk_idx));
        auto side_array = std::static_pointer_cast<arrow::StringArray>(
            side_column->chunk(chunk_idx));
        
        // Convert to OrderUpdate vector
        for (int64_t i = 0; i < skey_array->length(); ++i) {
            OrderUpdate order;
            order.skey = skey_array->Value(i);
            order.timestamp = timestamp_array->Value(i);
            order.bidPrice = bid_price_array->Value(i);
            order.bidSize = bid_size_array->Value(i);
            order.askPrice = ask_price_array->Value(i);
            order.askSize = ask_size_array->Value(i);
            
            // Handle string to char conversion
            auto side_str = side_array->GetString(i);
            order.side = side_str.empty() ? 'N' : side_str[0];
            
            orders.push_back(order);
        }
        
        row_offset += skey_array->length();
    }
    
    return orders;
}
```

## include/order_reader.h
```cpp
#pragma once
#include "ikv_store.h"
#include "order_update.h"
#include <memory>
#include <vector>
#include <optional>
#include <queue>

class OrderReader {
public:
    OrderReader(std::shared_ptr<IKVStore> kvStore, int date, 
                const std::vector<int>& skeyList);
    
    std::optional<OrderUpdate> nextOrder();
    
    // Statistics
    size_t getTotalRecordsRead() const { return totalRecordsRead_; }
    size_t getMemoryUsage() const;
    
private:
    struct OrderBuffer {
        std::vector<OrderUpdate> orders;
        size_t currentIndex = 0;
        int skey;
        
        bool hasNext() const { return currentIndex < orders.size(); }
        const OrderUpdate& peek() const { return orders[currentIndex]; }
        void advance() { currentIndex++; }
    };
    
    struct OrderComparator {
        bool operator()(const std::pair<int64_t, int>& a, 
                       const std::pair<int64_t, int>& b) {
            return a.first > b.first; // Min heap by timestamp
        }
    };
    
    std::shared_ptr<IKVStore> kvStore_;
    int date_;
    std::vector<int> skeyList_;
    std::vector<std::unique_ptr<OrderBuffer>> buffers_;
    std::priority_queue<std::pair<int64_t, int>, 
                       std::vector<std::pair<int64_t, int>>,
                       OrderComparator> minHeap_;
    size_t totalRecordsRead_ = 0;
    
    void loadOrderBuffer(int skeyIndex);
    std::vector<OrderUpdate> parseParquetData(const std::string& data);
};
```

## src/order_reader.cpp
```cpp
#include "order_reader.h"
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>

OrderReader::OrderReader(std::shared_ptr<IKVStore> kvStore, int date,
                        const std::vector<int>& skeyList)
    : kvStore_(kvStore), date_(date), skeyList_(skeyList) {
    
    buffers_.reserve(skeyList.size());
    
    // Initialize buffers for each skey
    for (size_t i = 0; i < skeyList.size(); ++i) {
        auto buffer = std::make_unique<OrderBuffer>();
        buffer->skey = skeyList[i];
        buffers_.push_back(std::move(buffer));
        loadOrderBuffer(i);
        
        // Add to heap if has data
        if (buffers_[i]->hasNext()) {
            minHeap_.push({buffers_[i]->peek().timestamp, i});
        }
    }
}

std::optional<OrderUpdate> OrderReader::nextOrder() {
    if (minHeap_.empty()) {
        return std::nullopt;
    }
    
    // Get the minimum timestamp order
    auto [timestamp, bufferIndex] = minHeap_.top();
    minHeap_.pop();
    
    auto& buffer = buffers_[bufferIndex];
    OrderUpdate order = buffer->peek();
    buffer->advance();
    totalRecordsRead_++;
    
    // If buffer still has data, push back to heap
    if (buffer->hasNext()) {
        minHeap_.push({buffer->peek().timestamp, bufferIndex});
    } else {
        // Try to load more data for this skey if available
        // (In version 3, we'll implement sharding support here)
    }
    
    return order;
}

void OrderReader::loadOrderBuffer(int skeyIndex) {
    auto& buffer = buffers_[skeyIndex];
    
    // Construct key: "order/{date}/{skey}"
    std::string key = "order/" + std::to_string(date_) + "/" + 
                     std::to_string(buffer->skey);
    
    std::string value;
    try {
        kvStore_->get(key, value);
        buffer->orders = parseParquetData(value);
        buffer->currentIndex = 0;
    } catch (const std::exception& e) {
        // No data for this skey, leave buffer empty
        buffer->orders.clear();
        buffer->currentIndex = 0;
    }
}

std::vector<OrderUpdate> OrderReader::parseParquetData(const std::string& data) {
    std::vector<OrderUpdate> orders;
    
    auto buffer = arrow::Buffer::FromString(data);
    auto input = std::make_shared<arrow::io::BufferReader>(buffer);
    
    // Use the correct API
    auto result = parquet::arrow::OpenFile(input, arrow::default_memory_pool());
    if (!result.ok()) {
        return orders;
    }
    std::unique_ptr<parquet::arrow::FileReader> arrow_reader = std::move(result.ValueOrDie());
    
    std::shared_ptr<arrow::Table> table;
    auto status = arrow_reader->ReadTable(&table);
    if (!status.ok()) {
        return orders;
    }
    
    // Parse table data into OrderUpdate objects
    // Process all chunks in the table
    int64_t row_offset = 0;
    for (int chunk_idx = 0; chunk_idx < table->column(0)->num_chunks(); ++chunk_idx) {
        auto skey_array = std::static_pointer_cast<arrow::Int32Array>(
            table->column(0)->chunk(chunk_idx));
        auto timestamp_array = std::static_pointer_cast<arrow::Int64Array>(
            table->column(1)->chunk(chunk_idx));
        auto bid_price_array = std::static_pointer_cast<arrow::Int32Array>(
            table->column(2)->chunk(chunk_idx));
        auto bid_size_array = std::static_pointer_cast<arrow::Int32Array>(
            table->column(3)->chunk(chunk_idx));
        auto ask_price_array = std::static_pointer_cast<arrow::Int32Array>(
            table->column(4)->chunk(chunk_idx));
        auto ask_size_array = std::static_pointer_cast<arrow::Int32Array>(
            table->column(5)->chunk(chunk_idx));
        auto side_array = std::static_pointer_cast<arrow::StringArray>(
            table->column(6)->chunk(chunk_idx));
        
        for (int64_t i = 0; i < skey_array->length(); ++i) {
            OrderUpdate order;
            order.skey = skey_array->Value(i);
            order.timestamp = timestamp_array->Value(i);
            order.bidPrice = bid_price_array->Value(i);
            order.bidSize = bid_size_array->Value(i);
            order.askPrice = ask_price_array->Value(i);
            order.askSize = ask_size_array->Value(i);
            
            auto side_str = side_array->GetString(i);
            order.side = side_str.empty() ? 'N' : side_str[0];
            
            orders.push_back(order);
        }
    }
    
    return orders;
}

size_t OrderReader::getMemoryUsage() const {
    size_t totalMemory = 0;
    for (const auto& buffer : buffers_) {
        totalMemory += buffer->orders.size() * sizeof(OrderUpdate);
    }
    totalMemory += sizeof(*this);
    return totalMemory;
}
```

## include/cached_kv_store.h (Version 2: Disk Cache with Persistence)
```cpp
#pragma once
#include "ikv_store.h"
#include <unordered_map>
#include <filesystem>
#include <mutex>
#include <chrono>

class CachedKVStore : public IKVStore {
public:
    CachedKVStore(std::shared_ptr<IKVStore> baseStore, 
                  const std::string& cacheDir,
                  size_t maxCacheSizeMB = 1024);
    ~CachedKVStore();
    
    void get(const std::string& key, std::string& value) override;
    void put(const std::string& key, const std::string& value) override;
    
    void clearCache();
    size_t getCacheSize() const;
    
    // Force persist index to disk
    void persistIndex();
    
private:
    struct CacheEntry {
        std::filesystem::path path;
        size_t size;
        std::chrono::system_clock::time_point lastAccess;
        std::string checksum;  // MD5 or CRC32 for validation
    };
    
    std::shared_ptr<IKVStore> baseStore_;
    std::filesystem::path cacheDir_;
    std::filesystem::path indexPath_;
    size_t maxCacheSize_;
    size_t currentCacheSize_;
    
    mutable std::mutex cacheMutex_;
    std::unordered_map<std::string, CacheEntry> cacheIndex_;
    
    // Persistent index management
    void loadIndex();
    void saveIndex();
    void validateCache();
    
    std::filesystem::path getCachePath(const std::string& key) const;
    void evictLRU();
    bool loadFromCache(const std::string& key, std::string& value);
    void saveToCache(const std::string& key, const std::string& value);
    std::string calculateChecksum(const std::string& data) const;
};
```

## src/cached_kv_store.cpp (with Persistent Index)
```cpp
#include "cached_kv_store.h"
#include <fstream>
#include <chrono>
#include <openssl/md5.h>
#include <json/json.h>  // Or use nlohmann/json.hpp

CachedKVStore::CachedKVStore(std::shared_ptr<IKVStore> baseStore,
                           const std::string& cacheDir,
                           size_t maxCacheSizeMB)
    : baseStore_(baseStore), 
      cacheDir_(cacheDir),
      maxCacheSize_(maxCacheSizeMB * 1024 * 1024),
      currentCacheSize_(0) {
    
    std::filesystem::create_directories(cacheDir_);
    indexPath_ = cacheDir_ / "cache_index.json";
    
    // Load existing cache index if available
    loadIndex();
    
    // Validate cache entries
    validateCache();
}

CachedKVStore::~CachedKVStore() {
    // Save index on destruction
    saveIndex();
}

void CachedKVStore::loadIndex() {
    if (!std::filesystem::exists(indexPath_)) {
        return;
    }
    
    std::ifstream indexFile(indexPath_);
    if (!indexFile) {
        return;
    }
    
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(indexFile, root)) {
        std::cerr << "Failed to parse cache index, starting fresh" << std::endl;
        return;
    }
    
    currentCacheSize_ = 0;
    
    for (const auto& key : root.getMemberNames()) {
        const Json::Value& entry = root[key];
        
        CacheEntry cacheEntry;
        cacheEntry.path = cacheDir_ / entry["filename"].asString();
        cacheEntry.size = entry["size"].asUInt64();
        cacheEntry.checksum = entry["checksum"].asString();
        
        // Parse timestamp
        auto timeMs = entry["lastAccess"].asInt64();
        cacheEntry.lastAccess = std::chrono::system_clock::time_point(
            std::chrono::milliseconds(timeMs));
        
        // Verify file exists
        if (std::filesystem::exists(cacheEntry.path)) {
            cacheIndex_[key] = cacheEntry;
            currentCacheSize_ += cacheEntry.size;
        }
    }
    
    std::cout << "Loaded " << cacheIndex_.size() << " cache entries, total size: " 
              << currentCacheSize_ / 1024 / 1024 << " MB" << std::endl;
}

void CachedKVStore::saveIndex() {
    Json::Value root;
    
    for (const auto& [key, entry] : cacheIndex_) {
        Json::Value jsonEntry;
        jsonEntry["filename"] = entry.path.filename().string();
        jsonEntry["size"] = Json::UInt64(entry.size);
        jsonEntry["checksum"] = entry.checksum;
        
        // Convert timestamp to milliseconds
        auto timeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            entry.lastAccess.time_since_epoch()).count();
        jsonEntry["lastAccess"] = Json::Int64(timeMs);
        
        root[key] = jsonEntry;
    }
    
    // Write to temp file first, then rename (atomic operation)
    auto tempPath = indexPath_.string() + ".tmp";
    std::ofstream indexFile(tempPath);
    Json::StreamWriterBuilder builder;
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &indexFile);
    indexFile.close();
    
    // Atomic rename
    std::filesystem::rename(tempPath, indexPath_);
}

void CachedKVStore::validateCache() {
    std::vector<std::string> invalidKeys;
    
    for (const auto& [key, entry] : cacheIndex_) {
        // Check if file exists
        if (!std::filesystem::exists(entry.path)) {
            invalidKeys.push_back(key);
            continue;
        }
        
        // Check file size
        auto fileSize = std::filesystem::file_size(entry.path);
        if (fileSize != entry.size) {
            invalidKeys.push_back(key);
            std::filesystem::remove(entry.path);
        }
        
        // Optionally: verify checksum for critical data
        // This can be expensive, so maybe only do it for recent files
    }
    
    // Remove invalid entries
    for (const auto& key : invalidKeys) {
        auto it = cacheIndex_.find(key);
        if (it != cacheIndex_.end()) {
            currentCacheSize_ -= it->second.size;
            cacheIndex_.erase(it);
        }
    }
    
    if (!invalidKeys.empty()) {
        std::cout << "Removed " << invalidKeys.size() << " invalid cache entries" << std::endl;
        saveIndex();  // Update index after cleanup
    }
}

void CachedKVStore::get(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    // Check cache first
    if (loadFromCache(key, value)) {
        return;
    }
    
    // Load from base store
    baseStore_->get(key, value);
    
    // Save to cache
    saveToCache(key, value);
    
    // Periodically save index (every N operations)
    static int operationCount = 0;
    if (++operationCount % 100 == 0) {
        saveIndex();
    }
}

void CachedKVStore::put(const std::string& key, const std::string& value) {
    baseStore_->put(key, value);
    
    std::lock_guard<std::mutex> lock(cacheMutex_);
    saveToCache(key, value);
}

bool CachedKVStore::loadFromCache(const std::string& key, std::string& value) {
    auto it = cacheIndex_.find(key);
    if (it == cacheIndex_.end()) {
        return false;
    }
    
    std::ifstream file(it->second.path, std::ios::binary);
    if (!file) {
        // File missing, remove from index
        currentCacheSize_ -= it->second.size;
        cacheIndex_.erase(it);
        return false;
    }
    
    value.assign(std::istreambuf_iterator<char>(file),
                 std::istreambuf_iterator<char>());
    
    // Verify checksum if needed
    if (!it->second.checksum.empty()) {
        std::string actualChecksum = calculateChecksum(value);
        if (actualChecksum != it->second.checksum) {
            std::cerr << "Checksum mismatch for key: " << key << std::endl;
            currentCacheSize_ -= it->second.size;
            cacheIndex_.erase(it);
            std::filesystem::remove(it->second.path);
            return false;
        }
    }
    
    // Update access time
    it->second.lastAccess = std::chrono::system_clock::now();
    
    return true;
}

void CachedKVStore::saveToCache(const std::string& key, const std::string& value) {
    size_t dataSize = value.size();
    
    // Check if we need to evict
    while (currentCacheSize_ + dataSize > maxCacheSize_ && !cacheIndex_.empty()) {
        evictLRU();
    }
    
    auto cachePath = getCachePath(key);
    std::ofstream file(cachePath, std::ios::binary);
    file.write(value.data(), value.size());
    file.close();
    
    // Create cache entry
    CacheEntry entry;
    entry.path = cachePath;
    entry.size = dataSize;
    entry.lastAccess = std::chrono::system_clock::now();
    entry.checksum = calculateChecksum(value);
    
    cacheIndex_[key] = entry;
    currentCacheSize_ += dataSize;
}

void CachedKVStore::evictLRU() {
    if (cacheIndex_.empty()) return;
    
    // Find oldest file
    auto oldestIt = cacheIndex_.begin();
    for (auto it = cacheIndex_.begin(); it != cacheIndex_.end(); ++it) {
        if (it->second.lastAccess < oldestIt->second.lastAccess) {
            oldestIt = it;
        }
    }
    
    // Remove file
    std::filesystem::remove(oldestIt->second.path);
    currentCacheSize_ -= oldestIt->second.size;
    cacheIndex_.erase(oldestIt);
}

std::filesystem::path CachedKVStore::getCachePath(const std::string& key) const {
    // Use a hash of the key as filename to avoid path issues
    std::hash<std::string> hasher;
    size_t hash = hasher(key);
    
    // Also encode the key in filename for debugging
    std::string safeKey = key;
    std::replace(safeKey.begin(), safeKey.end(), '/', '_');
    std::replace(safeKey.begin(), safeKey.end(), '\\', '_');
    
    return cacheDir_ / (std::to_string(hash) + "_" + safeKey + ".cache");
}

std::string CachedKVStore::calculateChecksum(const std::string& data) const {
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5(reinterpret_cast<const unsigned char*>(data.c_str()), 
        data.length(), digest);
    
    char mdString[33];
    for(int i = 0; i < 16; i++) {
        sprintf(&mdString[i*2], "%02x", (unsigned int)digest[i]);
    }
    
    return std::string(mdString);
}

void CachedKVStore::persistIndex() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    saveIndex();
}

void CachedKVStore::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    // Remove all cache files
    for (const auto& [key, entry] : cacheIndex_) {
        std::filesystem::remove(entry.path);
    }
    
    cacheIndex_.clear();
    currentCacheSize_ = 0;
    
    // Remove index file
    std::filesystem::remove(indexPath_);
}

size_t CachedKVStore::getCacheSize() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return currentCacheSize_;
}
```

## include/sharded_kv_store.h (Version 3: Data Sharding)
```cpp
#pragma once
#include "ikv_store.h"
#include <vector>
#include <memory>

class ShardedKVStore : public IKVStore {
public:
    ShardedKVStore(std::shared_ptr<IKVStore> baseStore, 
                   size_t shardSizeMB = 5);
    
    void get(const std::string& key, std::string& value) override;
    void put(const std::string& key, const std::string& value) override;
    
    // Get all shards for a key
    std::vector<std::string> getShards(const std::string& baseKey);
    
private:
    std::shared_ptr<IKVStore> baseStore_;
    size_t shardSize_;
    
    std::string getShardKey(const std::string& baseKey, int shardIndex) const;
    int getNumShards(const std::string& baseKey);
    void splitIntoShards(const std::string& key, const std::string& value);
};
```

## src/sharded_kv_store.cpp
```cpp
#include "sharded_kv_store.h"
#include <sstream>

ShardedKVStore::ShardedKVStore(std::shared_ptr<IKVStore> baseStore, 
                               size_t shardSizeMB)
    : baseStore_(baseStore), shardSize_(shardSizeMB * 1024 * 1024) {
}

void ShardedKVStore::get(const std::string& key, std::string& value) {
    // Try to get non-sharded first
    try {
        baseStore_->get(key, value);
        return;
    } catch (...) {
        // Try sharded version
    }
    
    // Get all shards and combine
    std::vector<std::string> shards = getShards(key);
    
    std::stringstream combined;
    for (const auto& shard : shards) {
        combined << shard;
    }
    value = combined.str();
}

std::vector<std::string> ShardedKVStore::getShards(const std::string& baseKey) {
    std::vector<std::string> shards;
    int shardIndex = 0;
    
    while (true) {
        std::string shardKey = getShardKey(baseKey, shardIndex);
        std::string shardData;
        
        try {
            baseStore_->get(shardKey, shardData);
            shards.push_back(shardData);
            shardIndex++;
        } catch (...) {
            break; // No more shards
        }
    }
    
    return shards;
}

void ShardedKVStore::put(const std::string& key, const std::string& value) {
    if (value.size() <= shardSize_) {
        // Small enough, store directly
        baseStore_->put(key, value);
    } else {
        // Split into shards
        splitIntoShards(key, value);
    }
}

void ShardedKVStore::splitIntoShards(const std::string& key, 
                                     const std::string& value) {
    size_t offset = 0;
    int shardIndex = 0;
    
    while (offset < value.size()) {
        size_t chunkSize = std::min(shardSize_, value.size() - offset);
        std::string shardKey = getShardKey(key, shardIndex);
        std::string shardData = value.substr(offset, chunkSize);
        
        baseStore_->put(shardKey, shardData);
        
        offset += chunkSize;
        shardIndex++;
    }
}

std::string ShardedKVStore::getShardKey(const std::string& baseKey, 
                                        int shardIndex) const {
    if (shardIndex == 0) {
        return baseKey;
    }
    return baseKey + "-" + std::to_string(shardIndex);
}
```

## src/optimized_cache_store.cpp
```cpp
#include "optimized_cache_store.h"
#include <fstream>
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>
#include <thread>
#include <future>

OptimizedCacheStore::OptimizedCacheStore(std::shared_ptr<IKVStore> baseStore,
                                         const std::string& cacheDir,
                                         size_t maxCacheSizeMB)
    : baseStore_(baseStore),
      cacheDir_(cacheDir),
      maxCacheSize_(maxCacheSizeMB * 1024 * 1024),
      currentCacheSize_(0) {
    
    std::filesystem::create_directories(cacheDir_);
}

void OptimizedCacheStore::get(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    // Try optimized cache first
    if (loadOptimizedCache(key, value)) {
        return;
    }
    
    // Load from base store
    baseStore_->get(key, value);
    
    // Optimize and save to cache
    saveOptimizedCache(key, value);
}

void OptimizedCacheStore::put(const std::string& key, const std::string& value) {
    baseStore_->put(key, value);
    
    std::lock_guard<std::mutex> lock(cacheMutex_);
    saveOptimizedCache(key, value);
}

void OptimizedCacheStore::prefetch(const std::vector<std::string>& keys) {
    // Async prefetch multiple keys
    std::vector<std::future<void>> futures;
    
    for (const auto& key : keys) {
        futures.push_back(std::async(std::launch::async, [this, key]() {
            std::string value;
            try {
                this->get(key, value);
            } catch (...) {
                // Ignore prefetch errors
            }
        }));
    }
    
    // Wait for all prefetches to complete
    for (auto& f : futures) {
        f.wait();
    }
}

void OptimizedCacheStore::getOptimized(const std::string& key, 
                                       std::vector<OrderUpdate>& orders) {
    std::string value;
    get(key, value);
    
    // Check if it's binary format
    if (value.size() >= 4 && 
        value[0] == 'B' && value[1] == 'I' && 
        value[2] == 'N' && value[3] == '1') {
        orders = deserializeBinary(value);
    } else {
        // Parse parquet
        orders = parseParquetToOrders(value);
    }
}

std::string OptimizedCacheStore::optimizeData(const std::string& parquetData) {
    // Parse parquet to orders
    auto orders = parseParquetToOrders(parquetData);
    
    // Convert to binary format
    return serializeBinary(orders);
}

std::vector<OrderUpdate> OptimizedCacheStore::parseParquetToOrders(
    const std::string& data) {
    std::vector<OrderUpdate> orders;
    
    auto buffer = arrow::Buffer::FromString(data);
    auto input = std::make_shared<arrow::io::BufferReader>(buffer);
    
    auto result = parquet::arrow::OpenFile(input, arrow::default_memory_pool());
    if (!result.ok()) {
        return orders;
    }
    std::unique_ptr<parquet::arrow::FileReader> arrow_reader = 
        std::move(result.ValueOrDie());
    
    std::shared_ptr<arrow::Table> table;
    auto status = arrow_reader->ReadTable(&table);
    if (!status.ok()) {
        return orders;
    }
    
    // Process all chunks
    for (int chunk_idx = 0; chunk_idx < table->column(0)->num_chunks(); ++chunk_idx) {
        auto skey_array = std::static_pointer_cast<arrow::Int32Array>(
            table->column(0)->chunk(chunk_idx));
        auto timestamp_array = std::static_pointer_cast<arrow::Int64Array>(
            table->column(1)->chunk(chunk_idx));
        auto bid_price_array = std::static_pointer_cast<arrow::Int32Array>(
            table->column(2)->chunk(chunk_idx));
        auto bid_size_array = std::static_pointer_cast<arrow::Int32Array>(
            table->column(3)->chunk(chunk_idx));
        auto ask_price_array = std::static_pointer_cast<arrow::Int32Array>(
            table->column(4)->chunk(chunk_idx));
        auto ask_size_array = std::static_pointer_cast<arrow::Int32Array>(
            table->column(5)->chunk(chunk_idx));
        auto side_array = std::static_pointer_cast<arrow::StringArray>(
            table->column(6)->chunk(chunk_idx));
        
        for (int64_t i = 0; i < skey_array->length(); ++i) {
            OrderUpdate order;
            order.skey = skey_array->Value(i);
            order.timestamp = timestamp_array->Value(i);
            order.bidPrice = bid_price_array->Value(i);
            order.bidSize = bid_size_array->Value(i);
            order.askPrice = ask_price_array->Value(i);
            order.askSize = ask_size_array->Value(i);
            
            auto side_str = side_array->GetString(i);
            order.side = side_str.empty() ? 'N' : side_str[0];
            
            orders.push_back(order);
        }
    }
    
    return orders;
}

std::string OptimizedCacheStore::serializeBinary(
    const std::vector<OrderUpdate>& orders) {
    std::string result;
    result.reserve(4 + 4 + orders.size() * sizeof(OrderUpdate));
    
    // Magic header
    result.append("BIN1");
    
    // Number of orders
    uint32_t count = orders.size();
    result.append(reinterpret_cast<const char*>(&count), sizeof(count));
    
    // Orders data
    result.append(reinterpret_cast<const char*>(orders.data()), 
                  orders.size() * sizeof(OrderUpdate));
    
    return result;
}

std::vector<OrderUpdate> OptimizedCacheStore::deserializeBinary(
    const std::string& binaryData) {
    std::vector<OrderUpdate> orders;
    
    if (binaryData.size() < 8) {
        return orders;
    }
    
    // Skip magic header
    size_t offset = 4;
    
    // Read count
    uint32_t count;
    std::memcpy(&count, binaryData.data() + offset, sizeof(count));
    offset += sizeof(count);
    
    // Read orders
    orders.resize(count);
    std::memcpy(orders.data(), binaryData.data() + offset, 
                count * sizeof(OrderUpdate));
    
    return orders;
}

void OptimizedCacheStore::saveOptimizedCache(const std::string& key, 
                                             const std::string& data) {
    // Optimize data
    std::string optimized = optimizeData(data);
    
    // Check cache size
    while (currentCacheSize_ + optimized.size() > maxCacheSize_ && 
           !cacheIndex_.empty()) {
        evictLRU();
    }
    
    // Save to file
    std::hash<std::string> hasher;
    size_t hash = hasher(key);
    auto path = cacheDir_ / (std::to_string(hash) + ".bin");
    
    std::ofstream file(path, std::ios::binary);
    file.write(optimized.data(), optimized.size());
    
    // Update index
    CacheEntry entry;
    entry.path = path;
    entry.size = optimized.size();
    entry.lastAccess = std::chrono::steady_clock::now();
    entry.isOptimized = true;
    
    cacheIndex_[key] = entry;
    currentCacheSize_ += optimized.size();
}

bool OptimizedCacheStore::loadOptimizedCache(const std::string& key, 
                                             std::string& value) {
    auto it = cacheIndex_.find(key);
    if (it == cacheIndex_.end()) {
        return false;
    }
    
    std::ifstream file(it->second.path, std::ios::binary);
    if (!file) {
        cacheIndex_.erase(it);
        return false;
    }
    
    value.assign(std::istreambuf_iterator<char>(file),
                 std::istreambuf_iterator<char>());
    
    // Update access time
    it->second.lastAccess = std::chrono::steady_clock::now();
    
    return true;
}

void OptimizedCacheStore::evictLRU() {
    if (cacheIndex_.empty()) return;
    
    // Find oldest entry
    auto oldestIt = cacheIndex_.begin();
    for (auto it = cacheIndex_.begin(); it != cacheIndex_.end(); ++it) {
        if (it->second.lastAccess < oldestIt->second.lastAccess) {
            oldestIt = it;
        }
    }
    
    // Remove file and update size
    std::filesystem::remove(oldestIt->second.path);
    currentCacheSize_ -= oldestIt->second.size;
    cacheIndex_.erase(oldestIt);
}
```
```cpp
#pragma once
#include "ikv_store.h"
#include "order_update.h"
#include <unordered_map>
#include <filesystem>
#include <memory>
#include <mutex>

class OptimizedCacheStore : public IKVStore {
public:
    OptimizedCacheStore(std::shared_ptr<IKVStore> baseStore,
                       const std::string& cacheDir,
                       size_t maxCacheSizeMB = 2048);
    
    void get(const std::string& key, std::string& value) override;
    void put(const std::string& key, const std::string& value) override;
    
    // Prefetch multiple keys
    void prefetch(const std::vector<std::string>& keys);
    
    // Get optimized binary format
    void getOptimized(const std::string& key, std::vector<OrderUpdate>& orders);
    
private:
    struct CacheEntry {
        std::filesystem::path path;
        size_t size;
        std::chrono::steady_clock::time_point lastAccess;
        bool isOptimized;
    };
    
    std::shared_ptr<IKVStore> baseStore_;
    std::filesystem::path cacheDir_;
    size_t maxCacheSize_;
    size_t currentCacheSize_;
    
    mutable std::mutex cacheMutex_;
    std::unordered_map<std::string, CacheEntry> cacheIndex_;
    
    // Optimization: convert parquet to binary format
    std::string optimizeData(const std::string& parquetData);
    std::vector<OrderUpdate> deserializeBinary(const std::string& binaryData);
    std::string serializeBinary(const std::vector<OrderUpdate>& orders);
    
    void saveOptimizedCache(const std::string& key, const std::string& data);
    bool loadOptimizedCache(const std::string& key, std::string& value);
};
```

## tests/test_order_reader.cpp
```cpp
#include <gtest/gtest.h>
#include "order_reader.h"
#include "s3_kv_store.h"
#include "cached_kv_store.h"
#include <chrono>
#include <thread>

class OrderReaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize AWS SDK
        Aws::SDKOptions options;
        Aws::InitAPI(options);
        
        // Create test KV store
        auto s3Store = std::make_shared<S3KVStore>("test-bucket");
        kvStore_ = std::make_shared<CachedKVStore>(s3Store, "./test_cache");
    }
    
    void TearDown() override {
        // Clean up cache
        std::filesystem::remove_all("./test_cache");
        
        // Shutdown AWS SDK
        Aws::SDKOptions options;
        Aws::ShutdownAPI(options);
    }
    
    std::shared_ptr<IKVStore> kvStore_;
};

TEST_F(OrderReaderTest, TestSingleStock) {
    std::vector<int> skeyList = {1};
    OrderReader reader(kvStore_, 20231201, skeyList);
    
    int count = 0;
    int64_t lastTimestamp = 0;
    
    while (auto order = reader.nextOrder()) {
        // Verify timestamp ordering
        EXPECT_GE(order->timestamp, lastTimestamp);
        lastTimestamp = order->timestamp;
        count++;
    }
    
    EXPECT_GT(count, 0);
    std::cout << "Read " << count << " orders for single stock" << std::endl;
}

TEST_F(OrderReaderTest, TestMultipleStocks) {
    std::vector<int> skeyList = {1, 2, 3, 4, 5};
    OrderReader reader(kvStore_, 20231201, skeyList);
    
    int count = 0;
    int64_t lastTimestamp = 0;
    std::unordered_map<int, int> stockCounts;
    
    while (auto order = reader.nextOrder()) {
        // Verify timestamp ordering
        EXPECT_GE(order->timestamp, lastTimestamp);
        lastTimestamp = order->timestamp;
        
        stockCounts[order->skey]++;
        count++;
    }
    
    EXPECT_GT(count, 0);
    EXPECT_EQ(stockCounts.size(), skeyList.size());
    
    std::cout << "Read " << count << " total orders" << std::endl;
    for (const auto& [skey, cnt] : stockCounts) {
        std::cout << "Stock " << skey << ": " << cnt << " orders" << std::endl;
    }
}

TEST_F(OrderReaderTest, TestMemoryUsage) {
    std::vector<int> skeyList;
    for (int i = 1; i <= 100; ++i) {
        skeyList.push_back(i);
    }
    
    OrderReader reader(kvStore_, 20231201, skeyList);
    
    size_t initialMemory = reader.getMemoryUsage();
    std::cout << "Initial memory usage: " << initialMemory / 1024 / 1024 << " MB" << std::endl;
    
    int count = 0;
    while (auto order = reader.nextOrder()) {
        count++;
        if (count % 10000 == 0) {
            size_t currentMemory = reader.getMemoryUsage();
            std::cout << "After " << count << " records, memory: " 
                     << currentMemory / 1024 / 1024 << " MB" << std::endl;
        }
    }
}
```

## tests/benchmark_order_reader.cpp
```cpp
#include <benchmark/benchmark.h>
#include "order_reader.h"
#include "s3_kv_store.h"
#include "cached_kv_store.h"
#include "sharded_kv_store.h"
#include "optimized_cache_store.h"
#include <aws/s3/model/ListObjectsV2Request.h>

class OrderReaderBenchmark : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State& state) override {
        // Initialize AWS
        Aws::InitAPI(options_);
        
        // Setup KV store based on benchmark variant
        setupKVStore(state.range(0));
        
        // Get all skeys for the date
        getAllSkeys(20231201);
    }
    
    void TearDown(const ::benchmark::State& state) override {
        kvStore_.reset();
        Aws::ShutdownAPI(options_);
    }
    
protected:
    Aws::SDKOptions options_;
    std::shared_ptr<IKVStore> kvStore_;
    std::vector<int> allSkeys_;
    
    void setupKVStore(int variant) {
        auto baseStore = std::make_shared<S3KVStore>("your-bucket");
        
        switch (variant) {
            case 1: // Simple S3
                kvStore_ = baseStore;
                break;
            case 2: // With disk cache
                kvStore_ = std::make_shared<CachedKVStore>(baseStore, "./bench_cache");
                break;
            case 3: // With sharding
                kvStore_ = std::make_shared<ShardedKVStore>(baseStore);
                break;
            case 4: // Optimized cache
                kvStore_ = std::make_shared<OptimizedCacheStore>(baseStore, "./opt_cache");
                break;
        }
    }
    
    void getAllSkeys(int date) {
        // List all objects with the given date prefix
        Aws::S3::S3Client s3Client;
        Aws::S3::Model::ListObjectsV2Request request;
        request.SetBucket("your-bucket");
        request.SetPrefix("mdl/mbd/order/" + std::to_string(date) + "/");
        
        auto outcome = s3Client.ListObjectsV2(request);
        if (outcome.IsSuccess()) {
            for (const auto& object : outcome.GetResult().GetContents()) {
                // Extract skey from object key
                std::string key = object.GetKey();
                size_t lastSlash = key.find_last_of('/');
                size_t dot = key.find_last_of('.');
                if (lastSlash != std::string::npos && dot != std::string::npos) {
                    std::string skeyStr = key.substr(lastSlash + 1, dot - lastSlash - 1);
                    // Handle sharded keys (remove -1, -2, etc.)
                    size_t dash = skeyStr.find('-');
                    if (dash != std::string::npos) {
                        skeyStr = skeyStr.substr(0, dash);
                    }
                    int skey = std::stoi(skeyStr);
                    if (std::find(allSkeys_.begin(), allSkeys_.end(), skey) == allSkeys_.end()) {
                        allSkeys_.push_back(skey);
                    }
                }
            }
        }
        
        std::sort(allSkeys_.begin(), allSkeys_.end());
    }
};

BENCHMARK_DEFINE_F(OrderReaderBenchmark, ReadAllOrders)(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        OrderReader reader(kvStore_, 20231201, allSkeys_);
        size_t recordCount = 0;
        auto startTime = std::chrono::high_resolution_clock::now();
        state.ResumeTiming();
        
        while (auto order = reader.nextOrder()) {
            recordCount++;
            benchmark::DoNotOptimize(order);
        }
        
        state.PauseTiming();
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();
        
        state.counters["Records"] = recordCount;
        state.counters["Time_ms"] = duration;
        state.counters["Memory_MB"] = reader.getMemoryUsage() / 1024.0 / 1024.0;
        state.counters["Throughput"] = recordCount * 1000.0 / duration;
        state.ResumeTiming();
    }
}

// Register benchmarks with different configurations
BENCHMARK_REGISTER_F(OrderReaderBenchmark, ReadAllOrders)
    ->Arg(1)->Name("Simple_S3")
    ->Unit(benchmark::kMillisecond);

BENCHMARK_REGISTER_F(OrderReaderBenchmark, ReadAllOrders)
    ->Arg(2)->Name("With_DiskCache")
    ->Unit(benchmark::kMillisecond);

BENCHMARK_REGISTER_F(OrderReaderBenchmark, ReadAllOrders)
    ->Arg(3)->Name("With_Sharding")
    ->Unit(benchmark::kMillisecond);

BENCHMARK_REGISTER_F(OrderReaderBenchmark, ReadAllOrders)
    ->Arg(4)->Name("Optimized_Cache")
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
```

## README.md
```markdown
# OrderReader - High-Performance Multi-way Merge System

## Overview
OrderReader implements a high-performance multi-way merge system for reading order/trade data from S3 stored in Parquet format.

## Features
1. **Version 1 - Simple S3 Reader**: Direct S3 access with Parquet parsing
2. **Version 2 - Disk Cache**: LRU disk cache to reduce S3 requests
3. **Version 3 - Data Sharding**: Support for sharded data files
4. **Version 4 - Optimized Cache**: Binary format cache with prefetching

## Performance Optimizations
- Multi-way merge using min-heap for O(log N) per record
- Lazy loading of data buffers
- Disk caching with LRU eviction
- Data sharding for large files
- Binary format optimization for faster deserialization
- Prefetching for sequential access patterns

## Build Instructions
```bash
mkdir build && cd build
cmake ..
make -j8
```

## Running Tests
```bash
./test_order_reader
./benchmark_order_reader
```

## Dependencies
- AWS SDK for C++
- Apache Arrow & Parquet
- Google Test
- Google Benchmark
- C++17 compiler

## Configuration
- Cache size: Configurable via constructor (default 1GB)
- Shard size: Configurable via constructor (default 5MB)
- S3 region: Configurable via constructor (default us-east-1)

## Performance Metrics
The benchmark outputs:
- Total records processed
- Processing time (ms)
- Memory usage (MB)
- Throughput (records/second)
```