// test/test_main.cpp
#include <gtest/gtest.h>
#include <aws/core/Aws.h>

int main(int argc, char** argv) {
    // Initialize AWS SDK
    Aws::SDKOptions options;
    Aws::InitAPI(options);
    
    // Run tests
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    
    // Cleanup AWS SDK
    Aws::ShutdownAPI(options);
    
    return result;
}

// test/test_order_reader.cpp
#include <gtest/gtest.h>
#include "order_reader.h"
#include "s3_kv_store.h"
#include "disk_cache_kv_store.h"
#include "sliced_kv_store.h"
#include "optimized_cache_kv_store.h"
#include <memory>
#include <vector>

class MockKVStore : public IKVStore {
private:
    std::unordered_map<std::string, std::string> data_;
    
public:
    void get(const std::string& key, std::string& value) override {
        auto it = data_.find(key);
        if (it != data_.end()) {
            value = it->second;
        } else {
            throw std::runtime_error("Key not found: " + key);
        }
    }
    
    void put(const std::string& key, const std::string& value) override {
        data_[key] = value;
    }
    
    // Helper to create mock parquet data
    void addMockData(const std::string& key, const std::vector<OrderUpdate>& orders) {
        // Create Arrow table from orders
        arrow::MemoryPool* pool = arrow::default_memory_pool();
        
        arrow::Int32Builder skey_builder(pool);
        arrow::Int64Builder timestamp_builder(pool);
        arrow::Int32Builder bid_price_builder(pool);
        arrow::Int32Builder bid_size_builder(pool);
        arrow::Int32Builder ask_price_builder(pool);
        arrow::Int32Builder ask_size_builder(pool);
        arrow::StringBuilder side_builder(pool);
        
        for (const auto& order : orders) {
            skey_builder.Append(order.skey);
            timestamp_builder.Append(order.timestamp);
            bid_price_builder.Append(order.bidPrice);
            bid_size_builder.Append(order.bidSize);
            ask_price_builder.Append(order.askPrice);
            ask_size_builder.Append(order.askSize);
            side_builder.Append(std::string(1, order.side));
        }
        
        std::shared_ptr<arrow::Array> skey_array, timestamp_array, bid_price_array;
        std::shared_ptr<arrow::Array> bid_size_array, ask_price_array, ask_size_array, side_array;
        
        skey_builder.Finish(&skey_array);
        timestamp_builder.Finish(&timestamp_array);
        bid_price_builder.Finish(&bid_price_array);
        bid_size_builder.Finish(&bid_size_array);
        ask_price_builder.Finish(&ask_price_array);
        ask_size_builder.Finish(&ask_size_array);
        side_builder.Finish(&side_array);
        
        auto schema = arrow::schema({
            arrow::field("skey", arrow::int32()),
            arrow::field("timestamp", arrow::int64()),
            arrow::field("bidPrice", arrow::int32()),
            arrow::field("bidSize", arrow::int32()),
            arrow::field("askPrice", arrow::int32()),
            arrow::field("askSize", arrow::int32()),
            arrow::field("side", arrow::utf8())
        });
        
        auto table = arrow::Table::Make(schema, {skey_array, timestamp_array,
                                                 bid_price_array, bid_size_array,
                                                 ask_price_array, ask_size_array,
                                                 side_array});
        
        // Write to buffer
        auto output = std::make_shared<arrow::io::BufferOutputStream>();
        parquet::arrow::WriteTable(*table, pool, output);
        
        auto buffer = output->Finish().ValueOrDie();
        data_[key] = std::string(reinterpret_cast<const char*>(buffer->data()), 
                                buffer->size());
    }
};

TEST(OrderReaderTest, BasicFunctionality) {
    auto mock_store = std::make_shared<MockKVStore>();
    
    // Create test data
    std::vector<OrderUpdate> orders1 = {
        {1001, 1000, 100, 10, 101, 10, 'B'},
        {1001, 2000, 102, 20, 103, 20, 'S'},
        {1001, 3000, 104, 30, 105, 30, 'B'}
    };
    
    std::vector<OrderUpdate> orders2 = {
        {1002, 1500, 200, 15, 201, 15, 'S'},
        {1002, 2500, 202, 25, 203, 25, 'B'},
        {1002, 3500, 204, 35, 205, 35, 'S'}
    };
    
    mock_store->addMockData("20240115/1001.parquet", orders1);
    mock_store->addMockData("20240115/1002.parquet", orders2);
    
    // Create OrderReader
    std::vector<int> skey_list = {1001, 1002};
    OrderReader reader(mock_store, 20240115, skey_list);
    
    // Read orders in timestamp order
    std::vector<int64_t> expected_timestamps = {1000, 1500, 2000, 2500, 3000, 3500};
    
    for (int64_t expected_ts : expected_timestamps) {
        auto order = reader.nextOrder();
        ASSERT_TRUE(order.has_value());
        EXPECT_EQ(order->timestamp, expected_ts);
    }
    
    // No more orders
    auto order = reader.nextOrder();
    EXPECT_FALSE(order.has_value());
}

TEST(OrderReaderTest, EmptySkeyList) {
    auto mock_store = std::make_shared<MockKVStore>();
    std::vector<int> empty_list;
    
    OrderReader reader(mock_store, 20240115, empty_list);
    auto order = reader.nextOrder();
    EXPECT_FALSE(order.has_value());
}

TEST(OrderReaderTest, MissingData) {
    auto mock_store = std::make_shared<MockKVStore>();
    
    // Only add data for one skey
    std::vector<OrderUpdate> orders = {
        {1001, 1000, 100, 10, 101, 10, 'B'}
    };
    mock_store->addMockData("20240115/1001.parquet", orders);
    
    // Request two skeys but only one has data
    std::vector<int> skey_list = {1001, 1002};
    OrderReader reader(mock_store, 20240115, skey_list);
    
    auto order = reader.nextOrder();
    ASSERT_TRUE(order.has_value());
    EXPECT_EQ(order->skey, 1001);
    
    order = reader.nextOrder();
    EXPECT_FALSE(order.has_value());
}

// test/test_disk_cache.cpp
TEST(DiskCacheTest, CacheHit) {
    auto mock_store = std::make_shared<MockKVStore>();
    auto cache_store = std::make_shared<DiskCacheKVStore>(mock_store, "/tmp/test_cache");
    
    // Clear cache first
    cache_store->clearCache();
    
    // Add test data
    std::string test_key = "test/key";
    std::string test_value = "test_data_12345";
    mock_store->put(test_key, test_value);
    
    // First get - should fetch from underlying store
    std::string value1;
    cache_store->get(test_key, value1);
    EXPECT_EQ(value1, test_value);
    
    // Second get - should hit cache
    std::string value2;
    cache_store->get(test_key, value2);
    EXPECT_EQ(value2, test_value);
    
    // Verify cache size
    EXPECT_GT(cache_store->getCacheSize(), 0);
}

// benchmark/benchmark_main.cpp
#include <benchmark/benchmark.h>
#include "order_reader.h"
#include "s3_kv_store.h"
#include "disk_cache_kv_store.h"
#include "optimized_cache_kv_store.h"
#include <memory>
#include <chrono>
#include <random>

class OrderReaderBenchmark : public benchmark::Fixture {
protected:
    std::shared_ptr<IKVStore> CreateTestStore() {
        auto store = std::make_shared<MockKVStore>();
        
        // Generate test data
        std::mt19937 gen(42);
        std::uniform_int_distribution<> price_dist(100, 500);
        std::uniform_int_distribution<> size_dist(1, 100);
        
        for (int skey = 1000; skey < 1100; ++skey) {
            std::vector<OrderUpdate> orders;
            for (int64_t i = 0; i < 10000; ++i) {
                OrderUpdate order;
                order.skey = skey;
                order.timestamp = i * 100;
                order.bidPrice = price_dist(gen);
                order.bidSize = size_dist(gen);
                order.askPrice = price_dist(gen);
                order.askSize = size_dist(gen);
                order.side = (i % 2 == 0) ? 'B' : 'S';
                orders.push_back(order);
            }
            
            std::string key = "20240115/" + std::to_string(skey) + ".parquet";
            store->addMockData(key, orders);
        }
        
        return store;
    }
};

BENCHMARK_F(OrderReaderBenchmark, SimpleKVStore)(benchmark::State& state) {
    auto store = CreateTestStore();
    
    for (auto _ : state) {
        std::vector<int> skey_list;
        for (int i = 1000; i < 1000 + state.range(0); ++i) {
            skey_list.push_back(i);
        }
        
        OrderReader reader(store, 20240115, skey_list);
        
        int count = 0;
        while (reader.nextOrder().has_value()) {
            count++;
        }
        
        state.counters["orders_processed"] = count;
        state.counters["memory_usage_mb"] = reader.getMemoryUsage() / (1024.0 * 1024.0);
    }
}
BENCHMARK(OrderReaderBenchmark_SimpleKVStore)->Range(1, 100);

BENCHMARK_F(OrderReaderBenchmark, DiskCacheKVStore)(benchmark::State& state) {
    auto base_store = CreateTestStore();
    auto cache_store = std::make_shared<DiskCacheKVStore>(base_store, "/tmp/bench_cache");
    
    for (auto _ : state) {
        std::vector<int> skey_list;
        for (int i = 1000; i < 1000 + state.range(0); ++i) {
            skey_list.push_back(i);
        }
        
        OrderReader reader(cache_store, 20240115, skey_list);
        
        int count = 0;
        auto start = std::chrono::high_resolution_clock::now();
        while (reader.nextOrder().has_value()) {
            count++;
        }
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        state.counters["orders_processed"] = count;
        state.counters["memory_usage_mb"] = reader.getMemoryUsage() / (1024.0 * 1024.0);
        state.counters["throughput_orders_per_sec"] = count / (duration.count() / 1000000.0);
    }
}
BENCHMARK(OrderReaderBenchmark_DiskCacheKVStore)->Range(1, 100);

BENCHMARK_F(OrderReaderBenchmark, OptimizedCacheKVStore)(benchmark::State& state) {
    auto base_store = CreateTestStore();
    auto opt_store = std::make_shared<OptimizedCacheKVStore>(base_store, "/tmp/bench_opt");
    
    // Pre-optimize batch
    std::vector<std::string> keys;
    for (int i = 1000; i < 1100; ++i) {
        keys.push_back("20240115/" + std::to_string(i) + ".parquet");
    }
    opt_store->optimizeBatch(keys);
    
    for (auto _ : state) {
        std::vector<int> skey_list;
        for (int i = 1000; i < 1000 + state.range(0); ++i) {
            skey_list.push_back(i);
        }
        
        OrderReader reader(opt_store, 20240115, skey_list);
        
        int count = 0;
        auto start = std::chrono::high_resolution_clock::now();
        while (reader.nextOrder().has_value()) {
            count++;
        }
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        state.counters["orders_processed"] = count;
        state.counters["memory_usage_mb"] = reader.getMemoryUsage() / (1024.0 * 1024.0);
        state.counters["throughput_orders_per_sec"] = count / (duration.count() / 1000000.0);
    }
    
    opt_store->printCacheStats();
}
BENCHMARK(OrderReaderBenchmark_OptimizedCacheKVStore)->Range(1, 100);

// Full day benchmark
static void BM_FullDayProcessing(benchmark::State& state) {
    // This would connect to real S3
    auto s3_store = std::make_shared<S3KVStore>("your-bucket", "mdl/mbd/order/");
    auto opt_store = std::make_shared<OptimizedCacheKVStore>(s3_store);
    
    // Get all keys for a specific date
    std::string date = "20240115";
    auto all_keys = s3_store->listKeys(date);
    
    std::vector<int> skey_list;
    for (const auto& key : all_keys) {
        // Extract skey from key format: {date}/{skey}.parquet
        size_t start = key.find_last_of('/') + 1;
        size_t end = key.find(".parquet");
        if (start != std::string::npos && end != std::string::npos) {
            int skey = std::stoi(key.substr(start, end - start));
            skey_list.push_back(skey);
        }
    }
    
    std::cout << "Processing " << skey_list.size() << " securities for date " << date << std::endl;
    
    for (auto _ : state) {
        OrderReader reader(opt_store, std::stoi(date), skey_list);
        
        int64_t count = 0;
        auto start = std::chrono::high_resolution_clock::now();
        
        while (reader.nextOrder().has_value()) {
            count++;
            if (count % 1000000 == 0) {
                std::cout << "Processed " << count / 1000000 << "M orders" << std::endl;
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
        
        state.counters["total_orders"] = count;
        state.counters["memory_usage_mb"] = reader.getMemoryUsage() / (1024.0 * 1024.0);
        state.counters["processing_time_sec"] = duration.count();
        state.counters["throughput_orders_per_sec"] = count / static_cast<double>(duration.count());
        
        std::cout << "\n=== Full Day Processing Results ===" << std::endl;
        std::cout << "Total orders processed: " << count << std::endl;
        std::cout << "Processing time: " << duration.count() << " seconds" << std::endl;
        std::cout << "Memory usage: " << reader.getMemoryUsage() / (1024.0 * 1024.0) << " MB" << std::endl;
        std::cout << "Throughput: " << count / static_cast<double>(duration.count()) << " orders/sec" << std::endl;
    }
}
// Register benchmark only if S3 credentials are available
// BENCHMARK(BM_FullDayProcessing)->Unit(benchmark::kSecond)->Iterations(1);

BENCHMARK_MAIN();