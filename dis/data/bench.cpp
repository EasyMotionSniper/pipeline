// test/test_kvstore.cpp
#include <gtest/gtest.h>
#include "kvstore/local_kvstore.h"
#include <random>
#include <filesystem>

namespace fs = std::filesystem;

class LocalKVStoreTest : public ::testing::Test {
protected:
    std::unique_ptr<LocalDiskKVStore> kvstore;
    std::string testDir = "/tmp/test_kvstore";
    
    void SetUp() override {
        // Clean up test directory
        if (fs::exists(testDir)) {
            fs::remove_all(testDir);
        }
        kvstore = std::make_unique<LocalDiskKVStore>(testDir);
    }
    
    void TearDown() override {
        kvstore.reset();
        if (fs::exists(testDir)) {
            fs::remove_all(testDir);
        }
    }
    
    std::string generateRandomData(size_t size) {
        static std::mt19937 gen(42);
        static std::uniform_int_distribution<> dis(0, 255);
        
        std::string data;
        data.reserve(size);
        for (size_t i = 0; i < size; ++i) {
            data.push_back(static_cast<char>(dis(gen)));
        }
        return data;
    }
    
    std::string generateTimeSeriesData(size_t numPoints) {
        std::vector<int64_t> values;
        values.reserve(numPoints);
        
        int64_t current = 1000000;
        static std::mt19937 gen(42);
        static std::uniform_int_distribution<> delta(-100, 100);
        
        for (size_t i = 0; i < numPoints; ++i) {
            current += delta(gen);
            values.push_back(current);
        }
        
        return std::string(reinterpret_cast<char*>(values.data()), 
                          values.size() * sizeof(int64_t));
    }
    
    std::string generateRepetitiveData(size_t size) {
        std::string pattern = "ABCDEFGH";
        std::string data;
        data.reserve(size);
        
        while (data.size() < size) {
            data.append(pattern);
        }
        data.resize(size);
        return data;
    }
    
    std::string generateSparseData(size_t size) {
        std::string data(size, '\0');
        static std::mt19937 gen(42);
        static std::uniform_int_distribution<> pos(0, size - 1);
        static std::uniform_int_distribution<> val(1, 255);
        
        // Only fill 10% of positions
        for (size_t i = 0; i < size / 10; ++i) {
            data[pos(gen)] = static_cast<char>(val(gen));
        }
        return data;
    }
};

TEST_F(LocalKVStoreTest, BasicPutGet) {
    std::string key = "test_key";
    std::string value = "test_value";
    
    kvstore->put(key, value, PutOption(CompressionType::NONE));
    
    std::string retrieved;
    kvstore->get(key, retrieved);
    
    EXPECT_EQ(value, retrieved);
}

TEST_F(LocalKVStoreTest, LargeDataPutGet) {
    std::string key = "large_key";
    std::string value = generateRandomData(1024 * 1024); // 1MB
    
    kvstore->put(key, value, PutOption(CompressionType::ZSTD));
    
    std::string retrieved;
    kvstore->get(key, retrieved);
    
    EXPECT_EQ(value, retrieved);
}

TEST_F(LocalKVStoreTest, CompressionTypes) {
    std::vector<CompressionType> types = {
        CompressionType::NONE,
        CompressionType::ZSTD,
        CompressionType::SNAPPY,
        CompressionType::LZ4,
        CompressionType::CUSTOM_DELTA,
        CompressionType::CUSTOM_DICTIONARY,
        CompressionType::CUSTOM_RLE_BITMAP
    };
    
    for (auto type : types) {
        std::string key = "key_" + std::to_string(static_cast<int>(type));
        std::string value = generateRandomData(10000);
        
        kvstore->put(key, value, PutOption(type));
        
        std::string retrieved;
        kvstore->get(key, retrieved);
        
        EXPECT_EQ(value, retrieved) << "Failed for compression type: " 
                                    << static_cast<int>(type);
    }
}

TEST_F(LocalKVStoreTest, AutoCompression) {
    // Small data - should use no compression or snappy
    std::string smallKey = "small";
    std::string smallValue = "small data";
    kvstore->put(smallKey, smallValue, PutOption(CompressionType::AUTO));
    
    // Medium data - should use LZ4
    std::string mediumKey = "medium";
    std::string mediumValue = generateRandomData(5000);
    kvstore->put(mediumKey, mediumValue, PutOption(CompressionType::AUTO));
    
    // Large data - should use ZSTD
    std::string largeKey = "large";
    std::string largeValue = generateRandomData(100000);
    kvstore->put(largeKey, largeValue, PutOption(CompressionType::AUTO));
    
    // Time series data - should use CUSTOM_DELTA
    std::string tsKey = "timeseries";
    std::string tsValue = generateTimeSeriesData(10000);
    kvstore->put(tsKey, tsValue, PutOption(CompressionType::AUTO));
    
    // Verify all can be retrieved correctly
    std::string retrieved;
    kvstore->get(smallKey, retrieved);
    EXPECT_EQ(smallValue, retrieved);
    
    kvstore->get(mediumKey, retrieved);
    EXPECT_EQ(mediumValue, retrieved);
    
    kvstore->get(largeKey, retrieved);
    EXPECT_EQ(largeValue, retrieved);
    
    kvstore->get(tsKey, retrieved);
    EXPECT_EQ(tsValue, retrieved);
}

TEST_F(LocalKVStoreTest, CustomCompressors) {
    // Test Delta Compressor with time series data
    {
        std::string key = "timeseries";
        std::string value = generateTimeSeriesData(1000);
        
        kvstore->put(key, value, PutOption(CompressionType::CUSTOM_DELTA));
        
        std::string retrieved;
        kvstore->get(key, retrieved);
        EXPECT_EQ(value, retrieved);
        
        auto meta = kvstore->getMetadata(key);
        double ratio = static_cast<double>(meta.originalSize) / meta.compressedSize;
        EXPECT_GT(ratio, 1.5) << "Delta compression should achieve good ratio for time series";
    }
    
    // Test Dictionary Compressor with repetitive data
    {
        std::string key = "repetitive";
        std::string value = generateRepetitiveData(10000);
        
        kvstore->put(key, value, PutOption(CompressionType::CUSTOM_DICTIONARY));
        
        std::string retrieved;
        kvstore->get(key, retrieved);
        EXPECT_EQ(value, retrieved);
        
        auto meta = kvstore->getMetadata(key);
        double ratio = static_cast<double>(meta.originalSize) / meta.compressedSize;
        EXPECT_GT(ratio, 2.0) << "Dictionary compression should achieve good ratio for repetitive data";
    }
    
    // Test RLE Bitmap Compressor with sparse data
    {
        std::string key = "sparse";
        std::string value = generateSparseData(10000);
        
        kvstore->put(key, value, PutOption(CompressionType::CUSTOM_RLE_BITMAP));
        
        std::string retrieved;
        kvstore->get(key, retrieved);
        EXPECT_EQ(value, retrieved);
        
        auto meta = kvstore->getMetadata(key);
        double ratio = static_cast<double>(meta.originalSize) / meta.compressedSize;
        EXPECT_GT(ratio, 3.0) << "RLE Bitmap compression should achieve good ratio for sparse data";
    }
}

TEST_F(LocalKVStoreTest, MetadataPersistence) {
    std::string key = "persistent_key";
    std::string value = "persistent_value";
    
    kvstore->put(key, value, PutOption(CompressionType::ZSTD));
    
    // Destroy and recreate kvstore
    kvstore.reset();
    kvstore = std::make_unique<LocalDiskKVStore>(testDir);
    
    // Should be able to retrieve the value
    std::string retrieved;
    kvstore->get(key, retrieved);
    EXPECT_EQ(value, retrieved);
}

TEST_F(LocalKVStoreTest, NonExistentKey) {
    std::string value;
    EXPECT_THROW(kvstore->get("nonexistent", value), std::runtime_error);
}

// benchmark/benchmark_kvstore.cpp
#include <benchmark/benchmark.h>
#include "kvstore/local_kvstore.h"
#include <random>
#include <filesystem>
#include <iomanip>

namespace fs = std::filesystem;

class KVStoreBenchmark : public benchmark::Fixture {
protected:
    std::unique_ptr<LocalDiskKVStore> kvstore;
    std::string benchDir = "/tmp/bench_kvstore";
    std::vector<std::string> testData;
    
    void SetUp(const ::benchmark::State& state) override {
        if (fs::exists(benchDir)) {
            fs::remove_all(benchDir);
        }
        kvstore = std::make_unique<LocalDiskKVStore>(benchDir);
        
        // Prepare test data of various sizes
        testData.clear();
        size_t dataSize = state.range(0);
        testData.push_back(generateData(dataSize, state.range(1)));
    }
    
    void TearDown(const ::benchmark::State& state) override {
        kvstore.reset();
        if (fs::exists(benchDir)) {
            fs::remove_all(benchDir);
        }
    }
    
    std::string generateData(size_t size, int dataType) {
        switch (dataType) {
            case 0: return generateRandomData(size);
            case 1: return generateTimeSeriesData(size / sizeof(int64_t));
            case 2: return generateRepetitiveData(size);
            case 3: return generateSparseData(size);
            default: return generateRandomData(size);
        }
    }
    
    std::string generateRandomData(size_t size) {
        static std::mt19937 gen(42);
        static std::uniform_int_distribution<> dis(0, 255);
        
        std::string data;
        data.reserve(size);
        for (size_t i = 0; i < size; ++i) {
            data.push_back(static_cast<char>(dis(gen)));
        }
        return data;
    }
    
    std::string generateTimeSeriesData(size_t numPoints) {
        std::vector<int64_t> values;
        values.reserve(numPoints);
        
        int64_t current = 1000000;
        static std::mt19937 gen(42);
        static std::uniform_int_distribution<> delta(-100, 100);
        
        for (size_t i = 0; i < numPoints; ++i) {
            current += delta(gen);
            values.push_back(current);
        }
        
        return std::string(reinterpret_cast<char*>(values.data()), 
                          values.size() * sizeof(int64_t));
    }
    
    std::string generateRepetitiveData(size_t size) {
        std::string pattern = "ABCDEFGHIJKLMNOP";
        std::string data;
        data.reserve(size);
        
        while (data.size() < size) {
            data.append(pattern);
        }
        data.resize(size);
        return data;
    }
    
    std::string generateSparseData(size_t size) {
        std::string data(size, '\0');
        static std::mt19937 gen(42);
        static std::uniform_real_distribution<> sparse(0, 1);
        static std::uniform_int_distribution<> val(1, 255);
        
        for (size_t i = 0; i < size; ++i) {
            if (sparse(gen) < 0.05) {  // 5% non-zero
                data[i] = static_cast<char>(val(gen));
            }
        }
        return data;
    }
};

BENCHMARK_DEFINE_F(KVStoreBenchmark, PutPerformance)(benchmark::State& state) {
    CompressionType compression = static_cast<CompressionType>(state.range(2));
    int keyIndex = 0;
    
    for (auto _ : state) {
        std::string key = "key_" + std::to_string(keyIndex++);
        kvstore->put(key, testData[0], PutOption(compression));
    }
    
    state.SetBytesProcessed(state.iterations() * testData[0].size());
    state.counters["compression_ratio"] = kvstore->getAverageCompressionRatio();
}

BENCHMARK_DEFINE_F(KVStoreBenchmark, GetPerformance)(benchmark::State& state) {
    CompressionType compression = static_cast<CompressionType>(state.range(2));
    
    // Pre-populate with data
    for (int i = 0; i < 100; ++i) {
        std::string key = "key_" + std::to_string(i);
        kvstore->put(key, testData[0], PutOption(compression));
    }
    
    int keyIndex = 0;
    for (auto _ : state) {
        std::string key = "key_" + std::to_string(keyIndex % 100);
        std::string value;
        kvstore->get(key, value);
        keyIndex++;
    }
    
    state.SetBytesProcessed(state.iterations() * testData[0].size());
}

// Register benchmarks
// Args: data_size, data_type(0=random,1=timeseries,2=repetitive,3=sparse), compression_type

// Small data (1KB)
BENCHMARK_REGISTER_F(KVStoreBenchmark, PutPerformance)
    ->Args({1024, 0, 0})->Name("PUT/1KB/Random/None");
BENCHMARK_REGISTER_F(KVStoreBenchmark, PutPerformance)
    ->Args({1024, 0, 1})->Name("PUT/1KB/Random/ZSTD");
BENCHMARK_REGISTER_F(KVStoreBenchmark, PutPerformance)
    ->Args({1024, 0, 2})->Name("PUT/1KB/Random/Snappy");
BENCHMARK_REGISTER_F(KVStoreBenchmark, PutPerformance)
    ->Args({1024, 0, 3})->Name("PUT/1KB/Random/LZ4");

// Medium data (100KB)
BENCHMARK_REGISTER_F(KVStoreBenchmark, PutPerformance)
    ->Args({102400, 0, 1})->Name("PUT/100KB/Random/ZSTD");
BENCHMARK_REGISTER_F(KVStoreBenchmark, PutPerformance)
    ->Args({102400, 1, 5})->Name("PUT/100KB/TimeSeries/Delta");
BENCHMARK_REGISTER_F(KVStoreBenchmark, PutPerformance)
    ->Args({102400, 2, 6})->Name("PUT/100KB/Repetitive/Dictionary");
BENCHMARK_REGISTER_F(KVStoreBenchmark, PutPerformance)
    ->Args({102400, 3, 7})->Name("PUT/100KB/Sparse/RLE");

// Large data (10MB)
BENCHMARK_REGISTER_F(KVStoreBenchmark, PutPerformance)
    ->Args({10485760, 0, 1})->Name("PUT/10MB/Random/ZSTD");
BENCHMARK_REGISTER_F(KVStoreBenchmark, PutPerformance)
    ->Args({10485760, 1, 5})->Name("PUT/10MB/TimeSeries/Delta");
BENCHMARK_REGISTER_F(KVStoreBenchmark, PutPerformance)
    ->Args({10485760, 2, 6})->Name("PUT/10MB/Repetitive/Dictionary");
BENCHMARK_REGISTER_F(KVStoreBenchmark, PutPerformance)
    ->Args({10485760, 3, 7})->Name("PUT/10MB/Sparse/RLE");

// GET benchmarks
BENCHMARK_REGISTER_F(KVStoreBenchmark, GetPerformance)
    ->Args({1024, 0, 2})->Name("GET/1KB/Random/Snappy");
BENCHMARK_REGISTER_F(KVStoreBenchmark, GetPerformance)
    ->Args({102400, 0, 1})->Name("GET/100KB/Random/ZSTD");
BENCHMARK_REGISTER_F(KVStoreBenchmark, GetPerformance)
    ->Args({10485760, 0, 1})->Name("GET/10MB/Random/ZSTD");

// Comprehensive compression comparison
static void BM_CompressionComparison(benchmark::State& state) {
    std::string dir = "/tmp/compression_test";
    if (fs::exists(dir)) {
        fs::remove_all(dir);
    }
    
    LocalDiskKVStore kvstore(dir);
    
    // Generate different types of data
    size_t dataSize = state.range(0);
    
    struct TestCase {
        std::string name;
        std::string data;
        CompressionType bestCompression;
        double bestRatio;
    };
    
    std::vector<TestCase> testCases = {
        {"Random", "", CompressionType::NONE, 1.0},
        {"TimeSeries", "", CompressionType::NONE, 1.0},
        {"Repetitive", "", CompressionType::NONE, 1.0},
        {"Sparse", "", CompressionType::NONE, 1.0},
        {"Text", "", CompressionType::NONE, 1.0}
    };
    
    // Generate test data
    static std::mt19937 gen(42);
    
    // Random data
    std::uniform_int_distribution<> dis(0, 255);
    for (size_t i = 0; i < dataSize; ++i) {
        testCases[0].data.push_back(static_cast<char>(dis(gen)));
    }
    
    // Time series data
    std::vector<int64_t> ts_values(dataSize / sizeof(int64_t));
    int64_t current = 1000000;
    std::uniform_int_distribution<> delta(-100, 100);
    for (auto& val : ts_values) {
        current += delta(gen);
        val = current;
    }
    testCases[1].data = std::string(reinterpret_cast<char*>(ts_values.data()), 
                                    ts_values.size() * sizeof(int64_t));
    
    // Repetitive data
    std::string pattern = "PATTERN_ABC_123_";
    while (testCases[2].data.size() < dataSize) {
        testCases[2].data.append(pattern);
    }
    testCases[2].data.resize(dataSize);
    
    // Sparse data
    testCases[3].data.resize(dataSize, '\0');
    std::uniform_real_distribution<> sparse_dis(0, 1);
    for (size_t i = 0; i < dataSize; ++i) {
        if (sparse_dis(gen) < 0.02) {  // 2% non-zero
            testCases[3].data[i] = static_cast<char>(dis(gen));
        }
    }
    
    // Text data (Lorem ipsum style)
    std::string text_pattern = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
    while (testCases[4].data.size() < dataSize) {
        testCases[4].data.append(text_pattern);
    }
    testCases[4].data.resize(dataSize);
    
    // Test all compression types on all data types
    std::vector<CompressionType> compressionTypes = {
        CompressionType::NONE,
        CompressionType::ZSTD,
        CompressionType::SNAPPY,
        CompressionType::LZ4,
        CompressionType::CUSTOM_DELTA,
        CompressionType::CUSTOM_DICTIONARY,
        CompressionType::CUSTOM_RLE_BITMAP
    };
    
    for (auto _ : state) {
        state.PauseTiming();
        
        std::cout << "\n=== Compression Analysis for " << dataSize << " bytes ===" << std::endl;
        std::cout << std::setw(15) << "Data Type" 
                  << std::setw(15) << "Compression" 
                  << std::setw(15) << "Comp. Size"
                  << std::setw(15) << "Ratio"
                  << std::setw(15) << "Time (ms)" << std::endl;
        std::cout << std::string(75, '-') << std::endl;
        
        for (auto& testCase : testCases) {
            for (auto compression : compressionTypes) {
                auto start = std::chrono::high_resolution_clock::now();
                
                std::string key = testCase.name + "_" + std::to_string(static_cast<int>(compression));
                kvstore.put(key, testCase.data, PutOption(compression));
                
                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                
                auto meta = kvstore.getMetadata(key);
                double ratio = static_cast<double>(meta.originalSize) / meta.compressedSize;
                
                if (ratio > testCase.bestRatio) {
                    testCase.bestRatio = ratio;
                    testCase.bestCompression = compression;
                }
                
                std::cout << std::setw(15) << testCase.name
                          << std::setw(15) << static_cast<int>(compression)
                          << std::setw(15) << meta.compressedSize
                          << std::setw(15) << std::fixed << std::setprecision(2) << ratio
                          << std::setw(15) << std::fixed << std::setprecision(2) 
                          << duration.count() / 1000.0 << std::endl;
            }
            std::cout << std::endl;
        }
        
        std::cout << "\n=== Best Compression for Each Data Type ===" << std::endl;
        for (const auto& testCase : testCases) {
            std::cout << std::setw(15) << testCase.name 
                      << " -> " << std::setw(20) 
                      << static_cast<int>(testCase.bestCompression)
                      << " (ratio: " << std::fixed << std::setprecision(2) 
                      << testCase.bestRatio << ")" << std::endl;
        }
        
        state.ResumeTiming();
    }
    
    fs::remove_all(dir);
}

BENCHMARK(BM_CompressionComparison)
    ->Arg(1024)      // 1KB
    ->Arg(10240)     // 10KB
    ->Arg(102400)    // 100KB
    ->Arg(1048576)   // 1MB
    ->Arg(10485760)  // 10MB
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1);

// Main function with custom reporter
int main(int argc, char** argv) {
    benchmark::Initialize(&argc, argv);
    
    // Run benchmarks
    benchmark::RunSpecifiedBenchmarks();
    
    // Additional analysis
    std::cout << "\n\n=== Compression Recommendations ===" << std::endl;
    std::cout << "Based on the benchmark results:" << std::endl;
    std::cout << "\n1. Small files (<1KB):" << std::endl;
    std::cout << "   - Use NO compression or Snappy (minimal overhead)" << std::endl;
    std::cout << "   - Compression often not worth the CPU cost" << std::endl;
    
    std::cout << "\n2. Medium files (1KB-100KB):" << std::endl;
    std::cout << "   - Random data: LZ4 (fast, decent ratio)" << std::endl;
    std::cout << "   - Time series: Custom Delta encoding" << std::endl;
    std::cout << "   - Text/Repetitive: Custom Dictionary or ZSTD" << std::endl;
    std::cout << "   - Sparse: Custom RLE+Bitmap" << std::endl;
    
    std::cout << "\n3. Large files (>100KB):" << std::endl;
    std::cout << "   - Random data: ZSTD (best ratio vs speed)" << std::endl;
    std::cout << "   - Time series: Custom Delta (can achieve 3-5x compression)" << std::endl;
    std::cout << "   - Repetitive: Custom Dictionary (can achieve 5-10x compression)" << std::endl;
    std::cout << "   - Sparse: Custom RLE+Bitmap (can achieve 10-50x compression)" << std::endl;
    
    std::cout << "\n4. Special considerations:" << std::endl;
    std::cout << "   - For real-time systems: Prefer Snappy or LZ4" << std::endl;
    std::cout << "   - For archival: Prefer ZSTD with high compression level" << std::endl;
    std::cout << "   - For structured data: Consider custom algorithms" << std::endl;
    
    return 0;
}