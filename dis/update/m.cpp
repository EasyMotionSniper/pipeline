// include/reader_factory.h
#pragma once
#include "order_reader.h"
#include "trade_reader.h"
#include "s3_kv_store.h"
#include "lru_disk_cache_kv_store.h"
#include "local_sliced_kv_store.h"
#include "optimized_cache_kv_store.h"
#include <memory>
#include <string>

enum class DataType {
    ORDER,
    TRADE
};

enum class CacheStrategy {
    NONE,           // Direct S3 access
    LRU_DISK,      // LRU disk cache
    LOCAL_SLICED,  // Local slicing
    OPTIMIZED      // Binary optimized cache
};

class ReaderFactory {
private:
    std::shared_ptr<Aws::S3::S3Client> s3_client_;
    std::string bucket_name_;
    std::string cache_base_dir_;
    
    // 创建带有特定缓存策略的KVStore
    std::shared_ptr<IKVStore> createKVStore(DataType type, CacheStrategy strategy) {
        std::string data_type_str = (type == DataType::ORDER) ? "order" : "trade";
        std::string prefix = "mdl/mbd/" + data_type_str + "/";
        
        // 基础S3存储
        auto s3_store = std::make_shared<S3KVStore>(bucket_name_, prefix);
        
        switch (strategy) {
            case CacheStrategy::NONE:
                return s3_store;
                
            case CacheStrategy::LRU_DISK: {
                std::string cache_dir = cache_base_dir_ + "/lru/" + data_type_str;
                return std::make_shared<LRUDiskCacheKVStore>(
                    s3_store, cache_dir, 1024); // 1GB cache
            }
            
            case CacheStrategy::LOCAL_SLICED: {
                std::string cache_dir = cache_base_dir_ + "/sliced/" + data_type_str;
                return std::make_shared<LocalSlicedKVStore>(
                    s3_store, cache_dir, 2 * 1024 * 1024); // 2MB slices
            }
            
            case CacheStrategy::OPTIMIZED: {
                std::string cache_dir = cache_base_dir_ + "/optimized/" + data_type_str;
                return std::make_shared<OptimizedCacheKVStore>(s3_store, cache_dir);
            }
            
            default:
                return s3_store;
        }
    }
    
public:
    ReaderFactory(const std::string& bucket, 
                  const std::string& cache_dir = "/tmp/reader_cache")
        : bucket_name_(bucket), cache_base_dir_(cache_dir) {
        
        Aws::Client::ClientConfiguration config;
        config.region = Aws::Region::US_EAST_1;
        s3_client_ = std::make_shared<Aws::S3::S3Client>(config);
    }
    
    // 创建OrderReader
    std::unique_ptr<OrderReader> createOrderReader(
        int date,
        const std::vector<int>& skeyList,
        CacheStrategy strategy = CacheStrategy::LRU_DISK) {
        
        auto kvStore = createKVStore(DataType::ORDER, strategy);
        return std::make_unique<OrderReader>(kvStore, date, skeyList);
    }
    
    // 创建TradeReader
    std::unique_ptr<TradeReader> createTradeReader(
        int date,
        const std::vector<int>& skeyList,
        CacheStrategy strategy = CacheStrategy::LRU_DISK) {
        
        auto kvStore = createKVStore(DataType::TRADE, strategy);
        return std::make_unique<TradeReader>(kvStore, date, skeyList);
    }
    
    // 通用创建方法
    template<typename ReaderType>
    std::unique_ptr<ReaderType> createReader(
        DataType type,
        int date,
        const std::vector<int>& skeyList,
        CacheStrategy strategy = CacheStrategy::LRU_DISK) {
        
        if constexpr (std::is_same_v<ReaderType, OrderReader>) {
            return createOrderReader(date, skeyList, strategy);
        } else if constexpr (std::is_same_v<ReaderType, TradeReader>) {
            return createTradeReader(date, skeyList, strategy);
        }
        return nullptr;
    }
};

// src/order_reader.cpp
#include "order_reader.h"
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>

std::vector<OrderUpdate> OrderReader::parseParquetData(const std::string& data) {
    std::vector<OrderUpdate> orders;
    
    auto buffer = arrow::Buffer::FromString(data);
    auto input = std::make_shared<arrow::io::BufferReader>(buffer);
    
    std::unique_ptr<parquet::arrow::FileReader> reader;
    auto status = parquet::arrow::OpenFile(input, arrow::default_memory_pool(), &reader);
    if (!status.ok()) {
        throw std::runtime_error("Failed to open parquet file: " + status.ToString());
    }
    
    std::shared_ptr<arrow::Table> table;
    status = reader->ReadTable(&table);
    if (!status.ok()) {
        throw std::runtime_error("Failed to read parquet table: " + status.ToString());
    }
    
    // Extract columns - 完全相同的解析逻辑
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
    
    int64_t num_rows = table->num_rows();
    orders.reserve(num_rows);
    
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
    
    return orders;
}

// src/trade_reader.cpp
#include "trade_reader.h"
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>

std::vector<TradeUpdate> TradeReader::parseParquetData(const std::string& data) {
    std::vector<TradeUpdate> trades;
    
    // 与OrderReader完全相同的解析逻辑，只是返回类型不同
    auto buffer = arrow::Buffer::FromString(data);
    auto input = std::make_shared<arrow::io::BufferReader>(buffer);
    
    std::unique_ptr<parquet::arrow::FileReader> reader;
    auto status = parquet::arrow::OpenFile(input, arrow::default_memory_pool(), &reader);
    if (!status.ok()) {
        throw std::runtime_error("Failed to open parquet file: " + status.ToString());
    }
    
    std::shared_ptr<arrow::Table> table;
    status = reader->ReadTable(&table);
    if (!status.ok()) {
        throw std::runtime_error("Failed to read parquet table: " + status.ToString());
    }
    
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
    
    int64_t num_rows = table->num_rows();
    trades.reserve(num_rows);
    
    for (int64_t i = 0; i < num_rows; ++i) {
        TradeUpdate trade;
        trade.skey = skey_array->Value(i);
        trade.timestamp = timestamp_array->Value(i);
        trade.bidPrice = bid_price_array->Value(i);
        trade.bidSize = bid_size_array->Value(i);
        trade.askPrice = ask_price_array->Value(i);
        trade.askSize = ask_size_array->Value(i);
        trade.side = side_array->GetString(i)[0];
        trades.push_back(trade);
    }
    
    return trades;
}

// example/usage_example.cpp
#include "reader_factory.h"
#include <iostream>
#include <chrono>

void demonstrateUsage() {
    ReaderFactory factory("your-bucket");
    
    std::vector<int> skey_list = {1001, 1002, 1003, 1004, 1005};
    int date = 20240115;
    
    // 1. 使用不同缓存策略的OrderReader
    {
        std::cout << "\n=== OrderReader with LRU Cache ===" << std::endl;
        auto order_reader = factory.createOrderReader(date, skey_list, 
                                                      CacheStrategy::LRU_DISK);
        
        int count = 0;
        auto start = std::chrono::high_resolution_clock::now();
        
        while (auto order = order_reader->nextOrder()) {
            count++;
            if (count % 10000 == 0) {
                std::cout << "Processed " << count << " orders" << std::endl;
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "Total orders: " << count << std::endl;
        std::cout << "Time: " << duration.count() << " ms" << std::endl;
        std::cout << "Memory: " << order_reader->getMemoryUsage() / (1024.0 * 1024.0) << " MB" << std::endl;
    }
    
    // 2. 使用相同缓存策略的TradeReader
    {
        std::cout << "\n=== TradeReader with LRU Cache ===" << std::endl;
        auto trade_reader = factory.createTradeReader(date, skey_list,
                                                      CacheStrategy::LRU_DISK);
        
        int count = 0;
        while (auto trade = trade_reader->nextTrade()) {
            count++;
        }
        
        std::cout << "Total trades: " << count << std::endl;
    }
    
    // 3. 测试不同缓存策略的性能
    {
        std::cout << "\n=== Performance Comparison ===" << std::endl;
        
        CacheStrategy strategies[] = {
            CacheStrategy::NONE,
            CacheStrategy::LRU_DISK,
            CacheStrategy::LOCAL_SLICED,
            CacheStrategy::OPTIMIZED
        };
        
        const char* strategy_names[] = {
            "No Cache",
            "LRU Disk Cache",
            "Local Sliced",
            "Optimized Binary"
        };
        
        for (int i = 0; i < 4; ++i) {
            auto reader = factory.createOrderReader(date, {skey_list[0]}, strategies[i]);
            
            auto start = std::chrono::high_resolution_clock::now();
            int count = 0;
            while (reader->nextOrder()) {
                count++;
            }
            auto end = std::chrono::high_resolution_clock::now();
            
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            
            std::cout << strategy_names[i] << ": " 
                     << duration.count() << " ms for " 
                     << count << " orders" << std::endl;
        }
    }
    
    // 4. 展示切片功能
    {
        std::cout << "\n=== Sliced Access Demo ===" << std::endl;
        
        auto kvStore = std::make_shared<S3KVStore>("your-bucket", "mdl/mbd/order/");
        auto sliced_store = std::make_shared<LocalSlicedKVStore>(kvStore);
        
        std::string key = "20240115/1001.parquet";
        
        // 获取第一个切片
        std::string slice_data;
        if (sliced_store->getSlice(key, 0, slice_data)) {
            std::cout << "First slice size: " << slice_data.size() << " bytes" << std::endl;
        }
        
        // 获取特定范围的数据
        std::string range_data;
        if (sliced_store->getRange(key, 1000, 5000, range_data)) {
            std::cout << "Range [1000-5000] size: " << range_data.size() << " bytes" << std::endl;
        }
        
        sliced_store->printSliceInfo(key);
    }
}

// example/combined_reader.cpp
#include "reader_factory.h"

// 同时处理Order和Trade的组合读取器
class CombinedReader {
private:
    std::unique_ptr<OrderReader> order_reader_;
    std::unique_ptr<TradeReader> trade_reader_;
    
    std::optional<OrderUpdate> next_order_;
    std::optional<TradeUpdate> next_trade_;
    
public:
    CombinedReader(std::unique_ptr<OrderReader> order_reader,
                   std::unique_ptr<TradeReader> trade_reader)
        : order_reader_(std::move(order_reader))
        , trade_reader_(std::move(trade_reader)) {
        
        next_order_ = order_reader_->nextOrder();
        next_trade_ = trade_reader_->nextTrade();
    }
    
    struct MarketEvent {
        enum Type { ORDER, TRADE } type;
        int64_t timestamp;
        union {
            OrderUpdate order;
            TradeUpdate trade;
        };
    };
    
    std::optional<MarketEvent> nextEvent() {
        if (!next_order_ && !next_trade_) {
            return std::nullopt;
        }
        
        MarketEvent event;
        
        if (next_order_ && (!next_trade_ || next_order_->timestamp <= next_trade_->timestamp)) {
            event.type = MarketEvent::ORDER;
            event.timestamp = next_order_->timestamp;
            event.order = *next_order_;
            next_order_ = order_reader_->nextOrder();
        } else {
            event.type = MarketEvent::TRADE;
            event.timestamp = next_trade_->timestamp;
            event.trade = *next_trade_;
            next_trade_ = trade_reader_->nextTrade();
        }
        
        return event;
    }
};

void demonstrateCombinedReader() {
    ReaderFactory factory("your-bucket");
    
    std::vector<int> skey_list = {1001, 1002, 1003};
    int date = 20240115;
    
    // 创建组合读取器
    CombinedReader combined(
        factory.createOrderReader(date, skey_list, CacheStrategy::LRU_DISK),
        factory.createTradeReader(date, skey_list, CacheStrategy::LRU_DISK)
    );
    
    // 按时间顺序读取所有市场事件
    int order_count = 0, trade_count = 0;
    while (auto event = combined.nextEvent()) {
        if (event->type == CombinedReader::MarketEvent::ORDER) {
            order_count++;
        } else {
            trade_count++;
        }
        
        if ((order_count + trade_count) % 10000 == 0) {
            std::cout << "Processed " << order_count << " orders and " 
                     << trade_count << " trades" << std::endl;
        }
    }
    
    std::cout << "Total: " << order_count << " orders, " 
             << trade_count << " trades" << std::endl;
}

int main() {
    try {
        Aws::SDKOptions options;
        Aws::InitAPI(options);
        
        demonstrateUsage();
        demonstrateCombinedReader();
        
        Aws::ShutdownAPI(options);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}