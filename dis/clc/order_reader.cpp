// include/types.h
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
    
    bool operator<(const OrderUpdate& other) const {
        return timestamp > other.timestamp; // For min-heap
    }
};

using Order = OrderUpdate;

// include/ikv_store.h
#pragma once
#include <string>
#include <memory>

class IKVStore {
public:
    virtual ~IKVStore() = default;
    virtual void get(const std::string& key, std::string& value) = 0;
    virtual void put(const std::string& key, const std::string& value) = 0;
};

// include/order_reader.h
#pragma once
#include "types.h"
#include "ikv_store.h"
#include <memory>
#include <optional>
#include <vector>
#include <queue>
#include <unordered_map>

class OrderReader {
private:
    struct OrderSource {
        int skey;
        std::vector<OrderUpdate> orders;
        size_t current_idx;
        
        OrderSource(int sk) : skey(sk), current_idx(0) {}
        
        bool hasNext() const {
            return current_idx < orders.size();
        }
        
        const OrderUpdate& peek() const {
            return orders[current_idx];
        }
        
        OrderUpdate next() {
            return orders[current_idx++];
        }
    };
    
    struct OrderComparator {
        bool operator()(const std::pair<OrderUpdate, int>& a,
                       const std::pair<OrderUpdate, int>& b) const {
            return a.first.timestamp > b.first.timestamp;
        }
    };
    
    std::shared_ptr<IKVStore> kv_store_;
    int date_;
    std::vector<std::unique_ptr<OrderSource>> sources_;
    std::priority_queue<std::pair<OrderUpdate, int>, 
                       std::vector<std::pair<OrderUpdate, int>>,
                       OrderComparator> pq_;
    
    void loadOrderData(int skey);
    std::vector<OrderUpdate> parseParquetData(const std::string& data);
    
public:
    OrderReader(std::shared_ptr<IKVStore> kvStore, int date,
                const std::vector<int>& skeyList);
    std::optional<Order> nextOrder();
    
    // For benchmarking
    size_t getTotalOrdersProcessed() const;
    size_t getMemoryUsage() const;
};