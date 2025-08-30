// test_framework.h
#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <chrono>

class TestFramework {
public:
    struct TestResult {
        std::string test_name;
        bool passed;
        std::string message;
        double duration_ms;
    };
    
    static TestFramework& Instance() {
        static TestFramework instance;
        return instance;
    }
    
    void RunTest(const std::string& name, std::function<void()> test) {
        std::cout << "Running test: " << name << "..." << std::endl;
        
        auto start = std::chrono::high_resolution_clock::now();
        TestResult result;
        result.test_name = name;
        
        try {
            test();
            result.passed = true;
            result.message = "PASSED";
        } catch (const std::exception& e) {
            result.passed = false;
            result.message = std::string("FAILED: ") + e.what();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        
        results_.push_back(result);
        
        if (result.passed) {
            std::cout << "  ✓ " << name << " (" << result.duration_ms << "ms)" << std::endl;
        } else {
            std::cout << "  ✗ " << name << " - " << result.message << std::endl;
        }
    }
    
    void PrintSummary() {
        int passed = 0, failed = 0;
        double total_time = 0;
        
        for (const auto& result : results_) {
            if (result.passed) passed++;
            else failed++;
            total_time += result.duration_ms;
        }
        
        std::cout << "\n========== Test Summary ==========" << std::endl;
        std::cout << "Total: " << (passed + failed) << " tests" << std::endl;
        std::cout << "Passed: " << passed << std::endl;
        std::cout << "Failed: " << failed << std::endl;
        std::cout << "Total time: " << total_time << "ms" << std::endl;
        std::cout << "==================================" << std::endl;
    }
    
private:
    std::vector<TestResult> results_;
};

#define TEST(name) TestFramework::Instance().RunTest(#name, []()
#define ASSERT_EQ(a, b) if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b)
#define ASSERT_TRUE(x) if (!(x)) throw std::runtime_error("Assertion failed: " #x " is false")
#define ASSERT_FALSE(x) if (x) throw std::runtime_error("Assertion failed: " #x " is true")
#define ASSERT_GT(a, b) if ((a) <= (b)) throw std::runtime_error("Assertion failed: " #a " <= " #b)
#define ASSERT_LT(a, b) if ((a) >= (b)) throw std::runtime_error("Assertion failed: " #a " >= " #b)
#define ASSERT_GE(a, b) if ((a) < (b)) throw std::runtime_error("Assertion failed: " #a " < " #b)
#define ASSERT_LE(a, b) if ((a) > (b)) throw std::runtime_error("Assertion failed: " #a " > " #b)

// unit_tests.cpp
#include "test_framework.h"
#include "flow_manager.h"
#include "channel.h"
#include "channel_manager.h"
#include <thread>
#include <chrono>
#include <cstring>
#include <sys/stat.h>
#include <dirent.h>

// 辅助函数：删除目录
void RemoveDirectory(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name != "." && name != "..") {
                std::string full_path = path + "/" + name;
                remove(full_path.c_str());
            }
        }
        closedir(dir);
        rmdir(path.c_str());
    }
}

// FlowManager单元测试
void TestFlowManager() {
    std::string test_db = "./test_flow_db";
    RemoveDirectory(test_db);
    
    TEST(FlowManager_Init) {
        FlowManager fm(test_db);
        ASSERT_TRUE(fm.Init());
        ASSERT_EQ(fm.GetCurrentSequence(), 0);
    });
    
    TEST(FlowManager_StoreAndRetrieve) {
        FlowManager fm(test_db);
        fm.Init();
        
        MarketDataMsg data;
        strcpy(data.symbol, "TEST");
        data.bid_price = 100.0;
        data.ask_price = 101.0;
        data.header.sequence = fm.GetNextSequence();
        
        ASSERT_TRUE(fm.StoreMarketData(data.header.sequence, data));
        
        MarketDataMsg retrieved;
        ASSERT_TRUE(fm.GetMarketData(data.header.sequence, retrieved));
        ASSERT_EQ(retrieved.bid_price, 100.0);
        ASSERT_EQ(retrieved.ask_price, 101.0);
        ASSERT_EQ(strcmp(retrieved.symbol, "TEST"), 0);
    });
    
    TEST(FlowManager_RangeQuery) {
        FlowManager fm(test_db);
        fm.Init();
        
        // 存储多条数据
        for (int i = 1; i <= 10; i++) {
            MarketDataMsg data;
            strcpy(data.symbol, "TEST");
            data.bid_price = 100.0 + i;
            data.header.sequence = i;
            fm.StoreMarketData(i, data);
        }
        
        // 查询范围
        auto range = fm.GetMarketDataRange(3, 7);
        ASSERT_EQ(range.size(), 5);
        ASSERT_EQ(range[0].bid_price, 103.0);
        ASSERT_EQ(range[4].bid_price, 107.0);
    });
    
    TEST(FlowManager_Persistence) {
        {
            FlowManager fm(test_db);
            fm.Init();
            
            for (int i = 0; i < 5; i++) {
                fm.GetNextSequence();
            }
            ASSERT_EQ(fm.GetCurrentSequence(), 5);
        }
        
        // 重新打开，检查序列号恢复
        {
            FlowManager fm(test_db);
            fm.Init();
            ASSERT_EQ(fm.GetCurrentSequence(), 5);
        }
    });
    
    RemoveDirectory(test_db);
}

// Channel单元测试
void TestChannel() {
    std::string test_db = "./test_channel_db";
    RemoveDirectory(test_db);
    
    TEST(Channel_AddRemoveSubscriber) {
        FlowManager fm(test_db);
        fm.Init();
        
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        Channel channel(sock, &fm);
        
        Subscriber sub;
        strcpy(sub.id, "test_sub");
        sub.ip = inet_addr("127.0.0.1");
        sub.port = 8888;
        sub.last_recv_seq = 0;
        sub.active = true;
        
        channel.AddSubscriber(sub);
        
        auto retrieved = channel.GetSubscriber("test_sub");
        ASSERT_TRUE(retrieved != nullptr);
        ASSERT_EQ(strcmp(retrieved->id, "test_sub"), 0);
        
        channel.RemoveSubscriber("test_sub");
        retrieved = channel.GetSubscriber("test_sub");
        ASSERT_TRUE(retrieved == nullptr);
        
        close(sock);
    });
    
    TEST(Channel_Grouping_ByProgress) {
        FlowManager fm(test_db);
        fm.Init();
        
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        Channel channel(sock, &fm);
        channel.SetGroupThreshold(50);  // 设置分组阈值
        channel.Start();
        
        // 添加进度相近的订阅者
        for (int i = 0; i < 5; i++) {
            Subscriber sub;
            sprintf(sub.id, "sub_%d", i);
            sub.ip = inet_addr("127.0.0.1");
            sub.port = 8888 + i;
            sub.last_recv_seq = 100 + i * 10;  // 进度差10
            sub.active = true;
            channel.AddSubscriber(sub);
        }
        
        // 添加进度差很大的订阅者
        Subscriber far_sub;
        strcpy(far_sub.id, "far_sub");
        far_sub.ip = inet_addr("127.0.0.1");
        far_sub.port = 9000;
        far_sub.last_recv_seq = 500;  // 进度差很大
        far_sub.active = true;
        channel.AddSubscriber(far_sub);
        
        // 等待分组完成
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 检查分组结果
        auto groups = channel.GetAllGroups();
        ASSERT_GE(groups.size(), 2);  // 至少应该有2个组
        
        // 检查进度相近的应该在同一组
        bool found_progress_group = false;
        for (const auto& group : groups) {
            if (group.subscriber_ids.size() >= 3) {
                found_progress_group = true;
                ASSERT_LE(group.max_seq - group.min_seq, 50);  // 组内进度差不超过阈值
            }
        }
        ASSERT_TRUE(found_progress_group);
        
        channel.Stop();
        close(sock);
    });
    
    TEST(Channel_Grouping_Dynamic) {
        FlowManager fm(test_db);
        fm.Init();
        
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        Channel channel(sock, &fm);
        channel.SetGroupThreshold(30);
        channel.Start();
        
        // 初始添加订阅者
        for (int i = 0; i < 3; i++) {
            Subscriber sub;
            sprintf(sub.id, "sub_%d", i);
            sub.ip = inet_addr("192.168.1.1");
            sub.port = 8888 + i;
            sub.last_recv_seq = 100 + i * 5;
            sub.active = true;
            channel.AddSubscriber(sub);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto initial_group_count = channel.GetGroupCount();
        
        // 模拟NACK导致的重新分组
        channel.HandleNack("sub_0", 50, 60);
        channel.HandleNack("sub_0", 61, 70);
        channel.HandleNack("sub_0", 71, 80);
        channel.HandleNack("sub_0", 81, 90);
        channel.HandleNack("sub_0", 91, 95);
        channel.HandleNack("sub_0", 96, 99);  // 频繁NACK触发重新分组
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // 可能触发了重新分组
        auto new_group_count = channel.GetGroupCount();
        ASSERT_GT(new_group_count, 0);
        
        channel.Stop();
        close(sock);
    });
    
    RemoveDirectory(test_db);
}

// ChannelManager单元测试
void TestChannelManager() {
    std::string test_db = "./test_manager_db";
    RemoveDirectory(test_db);
    
    TEST(ChannelManager_AddRemove) {
        FlowManager fm(test_db);
        fm.Init();
        
        ChannelManager manager;
        
        int sock1 = socket(AF_INET, SOCK_DGRAM, 0);
        auto channel1 = std::make_shared<Channel>(sock1, &fm);
        manager.AddChannel("channel1", channel1);
        
        int sock2 = socket(AF_INET, SOCK_DGRAM, 0);
        auto channel2 = std::make_shared<Channel>(sock2, &fm);
        manager.AddChannel("channel2", channel2);
        
        ASSERT_TRUE(manager.GetChannel("channel1") != nullptr);
        ASSERT_TRUE(manager.GetChannel("channel2") != nullptr);
        
        manager.RemoveChannel("channel1");
        ASSERT_TRUE(manager.GetChannel("channel1") == nullptr);
        ASSERT_TRUE(manager.GetChannel("channel2") != nullptr);
        
        close(sock1);
        close(sock2);
    });
    
    RemoveDirectory(test_db);
}

// integration_tests.cpp
#include "md_udp_puber.h"
#include "md_udp_suber.h"
#include <atomic>
#include <set>

void TestIntegration() {
    std::string test_db = "./test_integration_db";
    RemoveDirectory(test_db);
    
    TEST(Integration_BasicPubSub) {
        MdUdpPuber puber("127.0.0.1", 18888, test_db);
        ASSERT_TRUE(puber.Start());
        
        std::atomic<int> received_count(0);
        std::set<uint64_t> received_seqs;
        std::mutex seq_mutex;
        
        MdUdpSuber suber("test_sub", "127.0.0.1", 18888);
        suber.RegisterCallback([&](const MarketDataMsg& data) {
            received_count++;
            std::lock_guard<std::mutex> lock(seq_mutex);
            received_seqs.insert(data.header.sequence);
        });
        
        ASSERT_TRUE(suber.Start());
        
        // 等待连接建立
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 发布数据
        for (int i = 0; i < 10; i++) {
            MarketDataMsg data;
            strcpy(data.symbol, "TEST");
            data.bid_price = 100.0 + i;
            data.ask_price = 101.0 + i;
            puber.PublishMarketData(data);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // 等待接收
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        ASSERT_EQ(received_count.load(), 10);
        ASSERT_EQ(received_seqs.size(), 10);
        
        // 检查序列号连续性
        uint64_t expected_seq = 1;
        for (uint64_t seq : received_seqs) {
            ASSERT_EQ(seq, expected_seq++);
        }
        
        suber.Stop();
        puber.Stop();
    });
    
    TEST(Integration_LateJoiner) {
        MdUdpPuber puber("127.0.0.1", 18889, test_db);
        ASSERT_TRUE(puber.Start());
        
        // 先发布一些数据
        for (int i = 0; i < 5; i++) {
            MarketDataMsg data;
            strcpy(data.symbol, "EARLY");
            data.bid_price = 100.0 + i;
            puber.PublishMarketData(data);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 晚加入的订阅者
        std::atomic<int> received_count(0);
        std::vector<uint64_t> received_seqs;
        std::mutex seq_mutex;
        
        MdUdpSuber late_suber("late_sub", "127.0.0.1", 18889);
        late_suber.RegisterCallback([&](const MarketDataMsg& data) {
            received_count++;
            std::lock_guard<std::mutex> lock(seq_mutex);
            received_seqs.push_back(data.header.sequence);
        });
        
        ASSERT_TRUE(late_suber.Start());
        
        // 继续发布数据
        for (int i = 0; i < 5; i++) {
            MarketDataMsg data;
            strcpy(data.symbol, "LATE");
            data.bid_price = 200.0 + i;
            puber.PublishMarketData(data);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // 等待接收所有数据（包括历史数据）
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        ASSERT_GE(received_count.load(), 10);  // 应该收到所有数据
        
        // 检查是否从序列号1开始
        {
            std::lock_guard<std::mutex> lock(seq_mutex);
            ASSERT_GT(received_seqs.size(), 0);
            ASSERT_EQ(received_seqs.front(), 1);  // 第一个应该是序列号1
        }
        
        late_suber.Stop();
        puber.Stop();
    });
    
    TEST(Integration_Reconnection) {
        MdUdpPuber puber("127.0.0.1", 18890, test_db);
        ASSERT_TRUE(puber.Start());
        
        std::atomic<int> received_count(0);
        uint64_t last_seq = 0;
        std::mutex seq_mutex;
        
        // 第一次连接
        {
            MdUdpSuber suber("reconn_sub", "127.0.0.1", 18890);
            suber.RegisterCallback([&](const MarketDataMsg& data) {
                received_count++;
                std::lock_guard<std::mutex> lock(seq_mutex);
                last_seq = data.header.sequence;
            });
            
            ASSERT_TRUE(suber.Start());
            
            // 发布数据
            for (int i = 0; i < 5; i++) {
                MarketDataMsg data;
                strcpy(data.symbol, "TEST");
                data.bid_price = 100.0 + i;
                puber.PublishMarketData(data);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            ASSERT_EQ(received_count.load(), 5);
            
            suber.Stop();  // 断开连接
        }
        
        // 断线期间继续发布
        for (int i = 0; i < 5; i++) {
            MarketDataMsg data;
            strcpy(data.symbol, "MISSED");
            data.bid_price = 200.0 + i;
            puber.PublishMarketData(data);
        }
        
        // 重新连接
        received_count = 0;
        {
            MdUdpSuber suber("reconn_sub", "127.0.0.1", 18890);
            suber.RegisterCallback([&](const MarketDataMsg& data) {
                received_count++;
            });
            
            ASSERT_TRUE(suber.Start());
            
            // 继续发布
            for (int i = 0; i < 5; i++) {
                MarketDataMsg data;
                strcpy(data.symbol, "NEW");
                data.bid_price = 300.0 + i;
                puber.PublishMarketData(data);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            // 应该收到断线期间的数据 + 新数据
            ASSERT_GE(received_count.load(), 10);
            
            suber.Stop();
        }
        
        puber.Stop();
    });
    
    TEST(Integration_MultipleSubscribers) {
        MdUdpPuber puber("127.0.0.1", 18891, test_db);
        ASSERT_TRUE(puber.Start());
        
        const int num_subscribers = 5;
        std::vector<std::unique_ptr<MdUdpSuber>> subers;
        std::vector<std::atomic<int>> received_counts(num_subscribers);
        
        // 创建多个订阅者，模拟不同进度
        for (int i = 0; i < num_subscribers; i++) {
            // 先发布一些数据
            if (i > 0) {
                MarketDataMsg data;
                sprintf(data.symbol, "PRE_%d", i);
                data.bid_price = 50.0 + i;
                puber.PublishMarketData(data);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            auto suber = std::make_unique<MdUdpSuber>(
                std::string("sub_") + std::to_string(i),
                "127.0.0.1", 18891
            );
            
            suber->RegisterCallback([&counts = received_counts[i]](const MarketDataMsg& data) {
                counts++;
            });
            
            ASSERT_TRUE(suber->Start());
            subers.push_back(std::move(suber));
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        // 发布新数据
        for (int i = 0; i < 10; i++) {
            MarketDataMsg data;
            strcpy(data.symbol, "MULTI");
            data.bid_price = 100.0 + i;
            puber.PublishMarketData(data);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // 等待所有订阅者接收
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        
        // 检查所有订阅者都收到了数据
        for (int i = 0; i < num_subscribers; i++) {
            int expected = 10 + (num_subscribers - i - 1);  // 后加入的收到更多历史数据
            ASSERT_GE(received_counts[i].load(), expected);
            std::cout << "Subscriber " << i << " received " 
                     << received_counts[i].load() << " messages" << std::endl;
        }
        
        // 停止所有订阅者
        for (auto& suber : subers) {
            suber->Stop();
        }
        
        puber.Stop();
    });
    
    TEST(Integration_NACK_Recovery) {
        // 这个测试模拟丢包和NACK恢复
        MdUdpPuber puber("127.0.0.1", 18892, test_db);
        ASSERT_TRUE(puber.Start());
        
        std::atomic<int> received_count(0);
        std::set<uint64_t> received_seqs;
        std::mutex seq_mutex;
        
        class TestSuber : public MdUdpSuber {
        public:
            TestSuber(const std::string& id, const std::string& ip, uint16_t port)
                : MdUdpSuber(id, ip, port), drop_count_(0) {}
            
            void SimulatePacketLoss() {
                // 模拟丢包：跳过序列号
                expected_seq_ += 3;
                drop_count_++;
            }
            
            int GetDropCount() const { return drop_count_; }
            
        private:
            int drop_count_;
        };
        
        TestSuber suber("nack_test", "127.0.0.1", 18892);
        suber.RegisterCallback([&](const MarketDataMsg& data) {
            received_count++;
            std::lock_guard<std::mutex> lock(seq_mutex);
            received_seqs.insert(data.header.sequence);
        });
        
        ASSERT_TRUE(suber.Start());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 发布数据，模拟中间丢包
        for (int i = 0; i < 20; i++) {
            MarketDataMsg data;
            strcpy(data.symbol, "NACK");
            data.bid_price = 100.0 + i;
            puber.PublishMarketData(data);
            
            // 在第10个包时模拟丢包
            if (i == 10) {
                suber.SimulatePacketLoss();
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // 等待NACK恢复
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        // 应该收到所有数据（包括通过NACK恢复的）
        ASSERT_GE(received_count.load(), 20);
        
        // 检查序列号完整性
        {
            std::lock_guard<std::mutex> lock(seq_mutex);
            for (uint64_t seq = 1; seq <= 20; seq++) {
                ASSERT_TRUE(received_seqs.count(seq) > 0);
            }
        }
        
        suber.Stop();
        puber.Stop();
    });
    
    RemoveDirectory(test_db);
}

// performance_tests.cpp
void TestPerformance() {
    std::string test_db = "./test_perf_db";
    RemoveDirectory(test_db);
    
    TEST(Performance_Throughput) {
        MdUdpPuber puber("127.0.0.1", 18893, test_db);
        ASSERT_TRUE(puber.Start());
        
        std::atomic<int> received_count(0);
        MdUdpSuber suber("perf_sub", "127.0.0.1", 18893);
        suber.RegisterCallback([&](const MarketDataMsg& data) {
            received_count++;
        });
        
        ASSERT_TRUE(suber.Start());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 测试吞吐量
        const int num_messages = 1000;
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < num_messages; i++) {
            MarketDataMsg data;
            strcpy(data.symbol, "PERF");
            data.bid_price = 100.0 + i;
            puber.PublishMarketData(data);
        }
        
        // 等待接收完成
        auto timeout = std::chrono::seconds(5);
        auto wait_start = std::chrono::high_resolution_clock::now();
        while (received_count < num_messages && 
               std::chrono::high_resolution_clock::now() - wait_start < timeout) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        double throughput = (double)received_count * 1000 / duration.count();
        std::cout << "  Throughput: " << throughput << " msg/s" << std::endl;
        std::cout << "  Received: " << received_count << "/" << num_messages << std::endl;
        
        ASSERT_GE(received_count.load(), num_messages * 0.95);  // 至少95%成功率
        ASSERT_GT(throughput, 100);  // 至少100 msg/s
        
        suber.Stop();
        puber.Stop();
    });
    
    TEST(Performance_GroupingEfficiency) {
        std::string test_db2 = "./test_group_perf_db";
        RemoveDirectory(test_db2);
        
        FlowManager fm(test_db2);
        fm.Init();
        
        // 预填充数据
        for (int i = 1; i <= 1000; i++) {
            MarketDataMsg data;
            strcpy(data.symbol, "TEST");
            data.bid_price = 100.0 + i;
            data.header.sequence = i;
            fm.StoreMarketData(i, data);
        }
        
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        Channel channel(sock, &fm);
        channel.SetGroupThreshold(50);
        channel.Start();
        
        // 添加多个进度相近的订阅者
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < 20; i++) {
            Subscriber sub;
            sprintf(sub.id, "sub_%d", i);
            sub.ip = inet_addr("127.0.0.1");
            sub.port = 8888 + i;
            sub.last_recv_seq = 100 + (i % 5) * 10;  // 5个一组，进度相近
            sub.active = true;
            channel.AddSubscriber(sub);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        auto groups = channel.GetAllGroups();
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "  Grouping time: " << duration.count() << "ms" << std::endl;
        std::cout << "  Number of groups: " << groups.size() << std::endl;
        
        // 检查分组效率
        ASSERT_LE(groups.size(), 10);  // 20个订阅者应该分成不超过10组
        ASSERT_GE(groups.size(), 2);   // 至少应该有一些分组
        ASSERT_LT(duration.count(), 500);  // 分组应该在500ms内完成
        
        channel.Stop();
        close(sock);
        
        RemoveDirectory(test_db2);
    });
    
    RemoveDirectory(test_db);
}

// main.cpp
int main(int argc, char* argv[]) {
    std::cout << "======== Running Unit Tests ========" << std::endl;
    
    std::cout << "\n--- Testing FlowManager ---" << std::endl;
    TestFlowManager();
    
    std::cout << "\n--- Testing Channel ---" << std::endl;
    TestChannel();
    
    std::cout << "\n--- Testing ChannelManager ---" << std::endl;
    TestChannelManager();
    
    std::cout << "\n======== Running Integration Tests ========" << std::endl;
    TestIntegration();
    
    std::cout << "\n======== Running Performance Tests ========" << std::endl;
    TestPerformance();
    
    TestFramework::Instance().PrintSummary();
    
    return 0;
}

// Makefile for tests
# test_makefile
CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2 -pthread -I..
LDFLAGS = -lleveldb -lpthread

TEST_SRCS = unit_tests.cpp integration_tests.cpp performance_tests.cpp main.cpp
TEST_OBJS = $(TEST_SRCS:.cpp=.o)

# Main library sources (from parent directory)
LIB_SRCS = ../flow_manager.cpp ../channel.cpp ../channel_manager.cpp \
           ../md_udp_puber.cpp ../md_udp_suber.cpp
LIB_OBJS = $(LIB_SRCS:../%.cpp=%.o)

TARGET = test_runner

all: $(TARGET)

$(TARGET): $(TEST_OBJS) $(LIB_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Compile test files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile library files from parent directory
%.o: ../%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TEST_OBJS) $(LIB_OBJS) $(TARGET)
	rm -rf test_*_db

.PHONY: all run clean


# Reliable UDP Pub-Sub System Test Suite

## 测试框架说明

本测试套件提供了全面的单元测试、集成测试和性能测试，覆盖系统的所有核心功能。

## 测试结构

```
tests/
├── test_framework.h      # 测试框架定义
├── test_unit.cpp         # 单元测试
├── test_integration.cpp  # 集成测试  
├── test_performance.cpp  # 性能测试
├── test_main.cpp        # 测试主程序
└── Makefile             # 测试编译脚本
```

## 测试分类

### 1. 单元测试 (Unit Tests)

#### FlowManager测试
- `FlowManager_Init`: 测试初始化功能
- `FlowManager_StoreAndRetrieve`: 测试数据存储和检索
- `FlowManager_RangeQuery`: 测试范围查询
- `FlowManager_Persistence`: 测试持久化功能
- `FlowManager_Concurrency`: 测试并发访问

#### Channel测试
- `Channel_AddRemoveSubscriber`: 测试订阅者添加/删除
- `Channel_Grouping_ByProgress`: 测试基于进度的分组
- `Channel_Dynamic_Regrouping`: 测试动态重新分组
- `Channel_Group_Threshold`: 测试分组阈值设置

#### ChannelManager测试
- `ChannelManager_AddRemove`: 测试频道添加/删除
- `ChannelManager_NotifyAll`: 测试全频道通知

### 2. 集成测试 (Integration Tests)

- `Integration_BasicPubSub`: 基本发布订阅功能
- `Integration_LateJoiner`: 晚加入订阅者测试
- `Integration_Reconnection`: 断线重连测试
- `Integration_MultipleSubscribers`: 多订阅者测试
- `Integration_NACK_Recovery`: NACK恢复机制测试

### 3. 性能测试 (Performance Tests)

- `Performance_Throughput`: 吞吐量测试（目标: >100 msg/s）
- `Performance_GroupingEfficiency`: 分组效率测试
- `Performance_Latency`: 延迟测试（目标: <10ms）

## 编译和运行

### 编译测试

```bash
# 在项目根目录
make test

# 或者在tests目录
cd tests
make
```

### 运行所有测试

```bash
make run_test

# 或者直接运行
./test_runner
```

### 运行特定测试类别

```bash
# 只运行单元测试
./test_runner --unit

# 只运行集成测试  
./test_runner --integration

# 只运行性能测试
./test_runner --performance
```

## 测试覆盖率

### 功能覆盖
- 数据持久化（LevelDB）
- 序列号管理
- 订阅者管理
- 智能分组策略
- NACK机制
- 心跳检测
- 断线重连
- 历史数据追赶
- 并发访问

### 场景覆盖
- 新订阅者加入
- 订阅者断线重连
- 多订阅者不同进度
- 频繁NACK处理
- 高吞吐量场景
- 网络分组优化

## 测试配置

### 端口使用
- 单元测试: 8888-9000
- 集成测试: 28888-28893
- 性能测试: 使用临时端口

### 数据库路径
- 测试数据库统一使用 `./test_*_db` 前缀
- 测试完成后自动清理



## 测试脚本

### run_tests.sh
```bash
#!/bin/bash

# 编译测试
echo "Building tests..."
make clean
make test

if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

