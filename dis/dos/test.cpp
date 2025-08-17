// test/test_reader.cpp
#include <gtest/gtest.h>
#include "reader/order_reader.h"
#include "reader/trade_reader.h"
#include "kvstore/s3_kvstore.h"
#include "kvstore/cached_kvstore.h"
#include <memory>
#include <random>

// Mock KVStore for testing
class MockKVStore : public IKVStore {
private:
    std::unordered_map<std::string, std::string> data;
    
    std::string generateMockParquetData(int skey, int count) {
        // Generate mock parquet data
        // In real implementation, use Arrow to create actual Parquet
        std::vector<OrderUpdate> orders;
        std::mt19937 gen(skey);
        std::uniform_int_distribution<> priceDist(1000, 2000);
        std::uniform_int_distribution<> sizeDist(100, 1000);
        
        for (int i = 0; i < count; ++i) {
            OrderUpdate order;
            order.skey = skey;
            order.timestamp = i * 1000; // Monotonic timestamps
            order.bidPrice = priceDist(gen);
            order.bidSize = sizeDist(gen);
            order.askPrice = order.bidPrice + 1;
            order.askSize = sizeDist(gen);
            order.side = (i % 2 == 0) ? 'B' : 'S';
            orders.push_back(order);
        }
        
        // Convert to parquet format (simplified)
        return serializeOrders(orders);
    }
    
    std::string serializeOrders(const std::vector<OrderUpdate>& orders) {
        // Simplified serialization
        std::stringstream ss;
        for (const auto& order : orders) {
            ss.write(reinterpret_cast<const char*>(&order), sizeof(OrderUpdate));
        }
        return ss.str();
    }
    
public:
    MockKVStore() {
        // Pre-populate with test data
        for (int skey = 1001; skey <= 1010; ++skey) {
            std::string key = fmt::format("mdl/mbd/order/20240101/{}.parquet", skey);
            data[key] = generateMockParquetData(skey, 1000);
        }
    }
    
    void get(const std::string& key, std::string& value) override {
        auto it = data.find(key);
        if (it == data.end()) {
            throw std::runtime_error("Key not found: " + key);
        }
        value = it->second;
    }
    
    void put(const std::string& key, const std::string& value) override {
        data[key] = value;
    }
    
    std::vector<std::string> listKeys(const std::string& prefix) override {
        std::vector<std::string> keys;
        for (const auto& [key, _] : data) {
            if (key.find(prefix) == 0) {
                keys.push_back(key);
            }
        }
        return keys;
    }
};

class OrderReaderTest : public ::testing::Test {
protected:
    std::shared_ptr<IKVStore> kvStore;
    
    void SetUp() override {
        kvStore = std::make_shared<MockKVStore>();
    }
};

TEST_F(OrderReaderTest, TestMultipleStreams) {
    std::vector<int> skeys = {1001, 1002, 1003, 1004, 1005};
    OrderReader reader(kvStore, 20240101, skeys);
    
    std::unordered_map<int, int> skeyCount;
    int totalCount = 0;
    
    while (auto order = reader.nextOrder()) {
        skeyCount[order->skey]++;
        totalCount++;
        if (totalCount >= 500) break;
    }
    
    // Verify we got data from all streams
    EXPECT_EQ(skeyCount.size(), 5);
    for (const auto& [skey, count] : skeyCount) {
        EXPECT_GT(count, 0);
    }
}

TEST_F(OrderReaderTest, TestReset) {
    std::vector<int> skeys = {1001};
    OrderReader reader(kvStore, 20240101, skeys);
    
    // Read some orders
    int firstReadCount = 0;
    while (auto order = reader.nextOrder()) {
        firstReadCount++;
        if (firstReadCount >= 10) break;
    }
    
    // Reset and read again
    reader.reset();
    int secondReadCount = 0;
    while (auto order = reader.nextOrder()) {
        secondReadCount++;
        if (secondReadCount >= 10) break;
    }
    
    EXPECT_EQ(firstReadCount, secondReadCount);
}

// test/test_cache.cpp
#include <gtest/gtest.h>
#include "cache/cache_manager.h"
#include "cache/optimized_cache.h"
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

class CacheManagerTest : public ::testing::Test {
protected:
    std::unique_ptr<CacheManager> cache;
    std::string testCacheDir = "/tmp/test_cache";
    
    void SetUp() override {
        // Clean up any existing test cache
        if (fs::exists(testCacheDir)) {
            fs::remove_all(testCacheDir);
        }
        
        cache = std::make_unique<CacheManager>(testCacheDir, 10 * 1024 * 1024); // 10MB cache
    }
    
    void TearDown() override {
        cache.reset();
        if (fs::exists(testCacheDir)) {
            fs::remove_all(testCacheDir);
        }
    }
};

TEST_F(CacheManagerTest, TestBasicPutGet) {
    std::string key = "test_key";
    std::string value = "test_value";
    
    cache->put(key, value);
    EXPECT_TRUE(cache->exists(key));
    
    std::string retrievedValue;
    EXPECT_TRUE(cache->get(key, retrievedValue));
    EXPECT_EQ(value, retrievedValue);
}

TEST_F(CacheManagerTest, TestNonExistentKey) {
    std::string key = "non_existent";
    std::string value;
    
    EXPECT_FALSE(cache->exists(key));
    EXPECT_FALSE(cache->get(key, value));
}

TEST_F(CacheManagerTest, TestLRUEviction) {
    // Fill cache to capacity
    size_t dataSize = 1024 * 1024; // 1MB per entry
    std::string largeData(dataSize, 'X');
    
    for (int i = 0; i < 12; ++i) { // Will exceed 10MB limit
        std::string key = "key_" + std::to_string(i);
        cache->put(key, largeData);
    }
    
    // First keys should be evicted
    EXPECT_FALSE(cache->exists("key_0"));
    EXPECT_FALSE(cache->exists("key_1"));
    
    // Recent keys should still exist
    EXPECT_TRUE(cache->exists("key_10"));
    EXPECT_TRUE(cache->exists("key_11"));
}

TEST_F(CacheManagerTest, TestMetadataPersistence) {
    std::string key = "persistent_key";
    std::string value = "persistent_value";
    
    cache->put(key, value);
    cache->persistMetadata();
    
    // Create new cache manager
    auto newCache = std::make_unique<CacheManager>(testCacheDir, 10 * 1024 * 1024);
    
    EXPECT_TRUE(newCache->exists(key));
    std::string retrievedValue;
    EXPECT_TRUE(newCache->get(key, retrievedValue));
    EXPECT_EQ(value, retrievedValue);
}

TEST_F(CacheManagerTest, TestConcurrentAccess) {
    const int numThreads = 10;
    const int opsPerThread = 100;
    
    std::vector<std::thread> threads;
    
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([this, t, opsPerThread]() {
            for (int i = 0; i < opsPerThread; ++i) {
                std::string key = "thread_" + std::to_string(t) + "_" + std::to_string(i);
                std::string value = "value_" + std::to_string(t * 1000 + i);
                
                cache->put(key, value);
                
                std::string retrieved;
                EXPECT_TRUE(cache->get(key, retrieved));
                EXPECT_EQ(value, retrieved);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
}

// test/test_kvstore.cpp
#include <gtest/gtest.h>
#include "kvstore/cached_kvstore.h"
#include "kvstore/s3_kvstore.h"

class KVStoreTest : public ::testing::Test {
protected:
    std::shared_ptr<IKVStore> mockStore;
    
    void SetUp() override {
        mockStore = std::make_shared<MockKVStore>();
    }
};

TEST_F(KVStoreTest, TestCachedKVStore) {
    auto cachedStore = std::make_shared<CachedKVStore>(
        mockStore, "/tmp/test_kvstore_cache", 100 * 1024 * 1024);
    
    std::string key = "mdl/mbd/order/20240101/1001.parquet";
    std::string value1, value2;
    
    // First get should hit base store
    cachedStore->get(key, value1);
    
    // Second get should hit cache
    cachedStore->get(key, value2);
    
    EXPECT_EQ(value1, value2);
}

TEST_F(KVStoreTest, TestListKeys) {
    auto keys = mockStore->listKeys("mdl/mbd/order/20240101/");
    
    EXPECT_GT(keys.size(), 0);
    for (const auto& key : keys) {
        EXPECT_NE(key.find("mdl/mbd/order/20240101/"), std::string::npos);
    }
}

// benchmark/benchmark_main.cpp - Complete version
#include <benchmark/benchmark.h>
#include "reader/order_reader.h"
#include "reader/sliced_order_reader.h"
#include "kvstore/s3_kvstore.h"
#include "kvstore/cached_kvstore.h"
#include <chrono>
#include <memory>
#include <sys/resource.h>

class BenchmarkFixture : public benchmark::Fixture {
protected:
    std::shared_ptr<IKVStore> kvStore;
    std::vector<int> skeyList;
    int testDate = 20240101;
    
    size_t getMemoryUsage() {
        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);
        return usage.ru_maxrss * 1024; // Convert to bytes
    }
    
public:
    void SetUp(const ::benchmark::State& state) override {
        // Initialize based on version
        int version = state.range(0);
        int numSkeys = state.range(1);
        
        // Generate skey list (real stock IDs)
        for (int i = 0; i < numSkeys; ++i) {
            skeyList.push_back(600000 + i); // Chinese A-share stock IDs
        }
        
        switch(version) {
            case 1: // Simple version
                kvStore = std::make_shared<S3KVStore>("market-data-bucket");
                break;
                
            case 2: // Cached version
                kvStore = std::make_shared<CachedKVStore>(
                    std::make_shared<S3KVStore>("market-data-bucket"),
                    "/tmp/market_cache",
                    1024 * 1024 * 1024 // 1GB cache
                );
                break;
                
            case 3: // Sliced version with cache
                kvStore = std::make_shared<CachedKVStore>(
                    std::make_shared<S3KVStore>("market-data-bucket"),
                    "/tmp/market_cache_sliced",
                    2 * 1024 * 1024 * 1024 // 2GB cache
                );
                break;
                
            case 4: // Optimized version
                kvStore = std::make_shared<OptimizedCachedKVStore>(
                    std::make_shared<S3KVStore>("market-data-bucket"),
                    "/tmp/market_cache_opt",
                    4 * 1024 * 1024 * 1024 // 4GB cache
                );
                break;
        }
    }
    
    void TearDown(const ::benchmark::State& state) override {
        kvStore.reset();
        skeyList.clear();
    }
};

BENCHMARK_DEFINE_F(BenchmarkFixture, NextOrderPerformance)(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        
        // Choose reader based on version
        std::unique_ptr<OrderReader> reader;
        int version = state.range(0);
        
        switch(version) {
            case 1:
            case 2:
                reader = std::make_unique<OrderReader>(kvStore, testDate, skeyList);
                break;
            case 3:
                reader = std::make_unique<SlicedOrderReader>(kvStore, testDate, skeyList);
                break;
            case 4:
                reader = std::make_unique<OptimizedOrderReader>(kvStore, testDate, skeyList);
                break;
        }
        
        size_t recordCount = 0;
        auto startMem = getMemoryUsage();
        
        state.ResumeTiming();
        auto start = std::chrono::high_resolution_clock::now();
        
        while (auto order = reader->nextOrder()) {
            recordCount++;
            benchmark::DoNotOptimize(order);
            
            // For benchmark, limit to reasonable number
            if (recordCount >= 1000000) break;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        state.PauseTiming();
        
        auto endMem = getMemoryUsage();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        state.counters["records"] = recordCount;
        state.counters["time_ms"] = duration.count();
        state.counters["memory_mb"] = (endMem - startMem) / (1024.0 * 1024.0);
        state.counters["throughput_rps"] = recordCount / (duration.count() / 1000.0);
        state.counters["latency_us_per_record"] = (duration.count() * 1000.0) / recordCount;
        
        state.ResumeTiming();
    }
}

// Register benchmarks for different versions and skey counts
// Version 1: Simple implementation
BENCHMARK_REGISTER_F(BenchmarkFixture, NextOrderPerformance)
    ->Args({1, 10})
    ->Args({1, 50})
    ->Args({1, 100})
    ->Args({1, 500})
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3)
    ->Name("V1_Simple");

// Version 2: With disk cache
BENCHMARK_REGISTER_F(BenchmarkFixture, NextOrderPerformance)
    ->Args({2, 10})
    ->Args({2, 50})
    ->Args({2, 100})
    ->Args({2, 500})
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3)
    ->Name("V2_Cached");

// Version 3: Sliced data
BENCHMARK_REGISTER_F(BenchmarkFixture, NextOrderPerformance)
    ->Args({3, 100})
    ->Args({3, 500})
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3)
    ->Name("V3_Sliced");

// Version 4: Fully optimized
BENCHMARK_REGISTER_F(BenchmarkFixture, NextOrderPerformance)
    ->Args({4, 100})
    ->Args({4, 500})
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3)
    ->Name("V4_Optimized");

// Benchmark for full day data
static void BM_FullDayRead(benchmark::State& state) {
    auto s3Store = std::make_shared<S3KVStore>("market-data-bucket");
    int date = 20240101;
    
    for (auto _ : state) {
        state.PauseTiming();
        
        // Get all skeys for the day
        auto keys = s3Store->listKeys("mdl/mbd/order/" + std::to_string(date) + "/");
        std::vector<int> allSkeys;
        
        for (const auto& key : keys) {
            // Extract skey from key: mdl/mbd/order/20240101/600000.parquet
            size_t lastSlash = key.find_last_of('/');
            size_t dotPos = key.find('.', lastSlash);
            if (lastSlash != std::string::npos && dotPos != std::string::npos) {
                std::string skeyStr = key.substr(lastSlash + 1, dotPos - lastSlash - 1);
                allSkeys.push_back(std::stoi(skeyStr));
            }
        }
        
        std::cout << "Processing " << allSkeys.size() << " securities for date " << date << std::endl;
        
        auto reader = std::make_unique<OptimizedOrderReader>(s3Store, date, allSkeys);
        size_t count = 0;
        auto startMem = getCurrentRSS();
        
        state.ResumeTiming();
        auto start = std::chrono::high_resolution_clock::now();
        
        while (reader->nextOrder()) {
            count++;
            if (count % 1000000 == 0) {
                std::cout << "Processed " << count << " records..." << std::endl;
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        state.PauseTiming();
        
        auto endMem = getCurrentRSS();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
        
        state.counters["total_records"] = count;
        state.counters["total_skeys"] = allSkeys.size();
        state.counters["time_seconds"] = duration.count();
        state.counters["memory_gb"] = (endMem - startMem) / (1024.0 * 1024.0 * 1024.0);
        state.counters["throughput_rps"] = count / static_cast<double>(duration.count());
        
        std::cout << "Completed: " << count << " records in " << duration.count() << " seconds" << std::endl;
        std::cout << "Memory used: " << (endMem - startMem) / (1024.0 * 1024.0) << " MB" << std::endl;
        
        state.ResumeTiming();
    }
}
BENCHMARK(BM_FullDayRead)->Unit(benchmark::kSecond)->Iterations(1);

// Helper function to get current RSS
size_t getCurrentRSS() {
    FILE* file = fopen("/proc/self/status", "r");
    if (file == nullptr) return 0;
    
    char line[128];
    size_t rss = 0;
    
    while (fgets(line, 128, file)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            char* p = line;
            while (*p && !isdigit(*p)) p++;
            rss = atol(p) * 1024; // Convert KB to bytes
            break;
        }
    }
    
    fclose(file);
    return rss;
}

// Custom reporter for detailed output
class DetailedReporter : public benchmark::ConsoleReporter {
public:
    bool ReportContext(const Context& context) override {
        std::cout << "\n=== Market Data Reader Benchmark ===" << std::endl;
        std::cout << "Date: " << __DATE__ << " " << __TIME__ << std::endl;
        return ConsoleReporter::ReportContext(context);
    }
    
    void ReportRuns(const std::vector<Run>& reports) override {
        for (const auto& run : reports) {
            std::cout << "\n--- " << run.benchmark_name() << " ---" << std::endl;
            std::cout << "Records processed: " << run.counters.at("records") << std::endl;
            std::cout << "Time: " << run.counters.at("time_ms") << " ms" << std::endl;
            std::cout << "Memory: " << run.counters.at("memory_mb") << " MB" << std::endl;
            std::cout << "Throughput: " << run.counters.at("throughput_rps") << " records/sec" << std::endl;
            std::cout << "Latency: " << run.counters.at("latency_us_per_record") << " μs/record" << std::endl;
        }
        
        ConsoleReporter::ReportRuns(reports);
    }
};

int main(int argc, char** argv) {
    benchmark::Initialize(&argc, argv);
    
    // Add custom reporter
    auto reporter = std::make_unique<DetailedReporter>();
    benchmark::RunSpecifiedBenchmarks(reporter.get());
    
    return 0;
} TestBasicRead) {
    std::vector<int> skeys = {1001, 1002};
    OrderReader reader(kvStore, 20240101, skeys);
    
    auto order = reader.nextOrder();
    ASSERT_TRUE(order.has_value());
    EXPECT_TRUE(order->skey == 1001 || order->skey == 1002);
}

TEST_F(OrderReaderTest, TestTimestampOrdering) {
    std::vector<int> skeys = {1001, 1002, 1003};
    OrderReader reader(kvStore, 20240101, skeys);
    
    int64_t lastTimestamp = 0;
    int count = 0;
    while (auto order = reader.nextOrder()) {
        EXPECT_GE(order->timestamp, lastTimestamp);
        lastTimestamp = order->timestamp;
        count++;
        if (count > 100) break; // Test first 100 orders
    }
    EXPECT_GT(count, 0);
}

TEST_F(OrderReaderTest, TestEmptyData) {
    std::vector<int> skeys = {}; // Empty skey list
    OrderReader reader(kvStore, 20240101, skeys);
    
    auto order = reader.nextOrder();
    ASSERT_FALSE(order.has_value());
}

TEST_F(OrderReaderTest, TestSingleStream) {
    std::vector<int> skeys = {1001};
    OrderReader reader(kvStore, 20240101, skeys);
    
    int count = 0;
    while (auto order = reader.nextOrder()) {
        EXPECT_EQ(order->skey, 1001);
        count++;
        if (count > 10) break;
    }
    EXPECT_GT(count, 0);
}

TEST_F(OrderReaderTest,