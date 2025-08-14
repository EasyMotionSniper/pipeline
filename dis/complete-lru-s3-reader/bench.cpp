#include "order_reader.h"
#include "dos_kv_client.h"
#include <benchmark/benchmark.h>
#include <fstream>
#include <random>

// 生成测试用的大量Order数据
std::vector<Order> generate_test_orders(int skey, size_t count) {
    std::vector<Order> orders;
    orders.reserve(count);
    
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> price_dist(10, 100);
    std::uniform_int_distribution<int> size_dist(100, 1000);

    int64_t timestamp = 1000;
    for (size_t i = 0; i < count; ++i) {
        timestamp += rng() % 100;  // 确保有序但不连续
        orders.push_back({
            skey,
            timestamp,
            price_dist(rng),
            size_dist(rng),
            price_dist(rng) + 10,
            size_dist(rng)
        });
    }
    return orders;
}

// 基准测试：测量吞吐量（订单/秒）
static void BM_OrderReaderThroughput(benchmark::State& state) {
    // 准备测试环境
    auto mock_kv = std::make_shared<MockKVStore>();  // 复用测试中的MockKVStore
    std::string date = "2024-08-15";
    int skey_count = state.range(0);  // 可变参数：skey数量
    size_t orders_per_skey = state.range(1);  // 每个skey的订单数

    // 生成测试数据
    std::vector<int> skeys;
    for (int i = 0; i < skey_count; ++i) {
        skeys.push_back(1000 + i);
        auto orders = generate_test_orders(skeys.back(), orders_per_skey);
        mock_kv->set_test_data(date + "/" + std::to_string(skeys.back()), 
                              create_test_parquet(orders));
    }

    // 初始化OrderReader
    OrderReader reader(mock_kv, date, skeys);

    // 测量归并性能
    size_t total_orders = 0;
    for (auto _ : state) {
        // 重置状态（实际测试中可能需要重新创建reader）
        OrderReader reader(mock_kv, date, skeys);
        
        // 读取所有订单
        while (reader.nextOrder().has_value()) {
            total_orders++;
        }
    }

    // 计算吞吐量（订单/秒）
    state.SetItemsProcessed(total_orders);
    state.SetBytesProcessed(total_orders * sizeof(Order));
}

// 基准测试：测量内存使用
static void BM_OrderReaderMemory(benchmark::State& state) {
    // 类似吞吐量测试，但重点测量内存使用
    // 实际实现中可使用内存跟踪工具（如tcmalloc的HeapProfiler）
    auto mock_kv = std::make_shared<MockKVStore>();
    std::string date = "2024-08-15";
    std::vector<int> skeys = {1001, 1002, 1003};
    size_t orders_per_skey = state.range(0);

    for (int skey : skeys) {
        auto orders = generate_test_orders(skey, orders_per_skey);
        mock_kv->set_test_data(date + "/" + std::to_string(skey),
                              create_test_parquet(orders));
    }

    for (auto _ : state) {
        OrderReader reader(mock_kv, date, skeys);
        while (reader.nextOrder().has_value());
    }
}

// 注册基准测试（参数：skey数量，每个skey的订单数）
BENCHMARK(BM_OrderReaderThroughput)
    ->Args({2, 1000})    // 2个skey，每个1000订单
    ->Args({5, 5000})    // 5个skey，每个5000订单
    ->Args({10, 10000}); // 10个skey，每个10000订单

BENCHMARK(BM_OrderReaderMemory)
    ->Range(1000, 100000);  // 订单数从1000到100000

BENCHMARK_MAIN();
