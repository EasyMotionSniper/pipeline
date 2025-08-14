#include "order_reader.h"
#include "dos_kv_client.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <unordered_map>

// 模拟IKVStore用于测试
class MockKVStore : public IKVStore {
private:
    std::unordered_map<std::string, std::string> data_;

public:
    MOCK_METHOD(bool, get, (const std::string& key, std::string& value), (override));
    MOCK_METHOD(void, put, (const std::string& key, const std::string& value), (override));

    // 辅助方法：预填充测试数据
    void set_test_data(const std::string& key, const std::string& value) {
        data_[key] = value;
        ON_CALL(*this, get(key, testing::_))
            .WillByDefault(testing::Invoke([this, key](const std::string&, std::string& value) {
                value = data_[key];
                return true;
            }));
    }
};

// 生成测试用的Parquet数据（简化版，实际应使用Arrow生成）
std::string create_test_parquet(const std::vector<Order>& orders) {
    // 注意：实际测试中应使用Arrow库生成真实的Parquet数据
    // 这里返回一个特殊标记字符串，在测试解析时直接返回原订单
    return "test_parquet:" + std::to_string(orders.size());
}

// 测试OrderReader的归并逻辑
TEST(OrderReaderTest, MergeOrderTest) {
    // 1. 准备测试数据
    auto mock_kv = std::make_shared<MockKVStore>();
    std::string date = "2024-08-15";
    std::vector<int> skeys = {1001, 1002};

    // skey=1001的数据：[(1001, 100, ...), (1001, 300, ...)]
    std::vector<Order> skey1001_data = {
        {1001, 100, 10, 100, 20, 200},
        {1001, 300, 11, 150, 21, 180}
    };
    mock_kv->set_test_data(date + "/1001", create_test_parquet(skey1001_data));

    // skey=1002的数据：[(1002, 200, ...), (1002, 400, ...)]
    std::vector<Order> skey1002_data = {
        {1002, 200, 12, 120, 22, 220},
        {1002, 400, 13, 130, 23, 230}
    };
    mock_kv->set_test_data(date + "/1002", create_test_parquet(skey1002_data));

    // 2. 替换解析函数为测试版本（直接返回预设数据）
    // （实际实现中可通过继承或依赖注入实现）
    OrderReader reader(mock_kv, date, skeys);

    // 3. 验证归并结果（预期顺序：100 → 200 → 300 → 400）
    std::vector<int64_t> expected_timestamps = {100, 200, 300, 400};
    for (int64_t ts : expected_timestamps) {
        auto order = reader.nextOrder();
        ASSERT_TRUE(order.has_value());
        EXPECT_EQ(order->timestamp, ts);
    }

    // 4. 验证所有数据已读完
    EXPECT_FALSE(reader.nextOrder().has_value());
}

// 测试空数据情况
TEST(OrderReaderTest, EmptyDataTest) {
    auto mock_kv = std::make_shared<MockKVStore>();
    std::string date = "2024-08-15";
    std::vector<int> skeys = {1003};

    // 空数据
    mock_kv->set_test_data(date + "/1003", create_test_parquet({}));

    OrderReader reader(mock_kv, date, skeys);
    EXPECT_FALSE(reader.nextOrder().has_value());
}

// 测试单个skey数据
TEST(OrderReaderTest, SingleSkeyTest) {
    auto mock_kv = std::make_shared<MockKVStore>();
    std::string date = "2024-08-15";
    std::vector<int> skeys = {1004};

    std::vector<Order> data = {
        {1004, 500, 14, 140, 24, 240},
        {1004, 600, 15, 150, 25, 250}
    };
    mock_kv->set_test_data(date + "/1004", create_test_parquet(data));

    OrderReader reader(mock_kv, date, skeys);
    EXPECT_EQ(reader.nextOrder()->timestamp, 500);
    EXPECT_EQ(reader.nextOrder()->timestamp, 600);
    EXPECT_FALSE(reader.nextOrder().has_value());
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
