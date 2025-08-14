#pragma once

#include "ikvstore.h"
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <queue>
#include <future>

struct Order {
    int skey;
    int64_t timestamp;
    int bidPrice;
    int bidSize;
    int askPrice;
    int askSize;

    bool operator>(const Order& other) const { return timestamp > other.timestamp; }
    bool operator==(const Order& other) const {
        return skey == other.skey && timestamp == other.timestamp &&
               bidPrice == other.bidPrice && bidSize == other.bidSize &&
               askPrice == other.askPrice && askSize == other.askSize;
    }
};

class OrderReader {
private:
    std::shared_ptr<IKVStore> kv_store_;
    std::string date_;
    std::vector<int> skey_list_;
    std::vector<std::vector<Order>> skey_data_;  // 存储每个skey的解析数据
    std::vector<size_t> skey_indices_;           // 每个skey的当前读取位置
    std::priority_queue<Order, std::vector<Order>, std::greater<Order>> merge_queue_;

    // 解析Parquet数据
    std::vector<Order> parse_parquet(const std::string& data);
    // 并行加载所有skey数据
    void load_all_skeys();

public:
    OrderReader(std::shared_ptr<IKVStore> kvStore,
               const std::string& date,
               const std::vector<int>& skeyList);

    // 获取下一个归并后的订单
    std::optional<Order> nextOrder();

    // 测试专用：获取当前归并队列大小（用于验证）
    size_t merge_queue_size() const { return merge_queue_.size(); }
};
