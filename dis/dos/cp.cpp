#include <vector>
#include <string>
#include <cstring>
#include <span>

// OrderUpdate必须满足这些条件
struct OrderUpdate {
    uint64_t orderId;
    uint32_t symbol;
    double price;
    uint64_t quantity;
    uint32_t side;
    uint64_t timestamp;
    
    // 确保是标准布局和平凡可复制的
    static_assert(std::is_standard_layout_v<OrderUpdate>);
    static_assert(std::is_trivially_copyable_v<OrderUpdate>);
    static_assert(std::is_trivial_v<OrderUpdate>);
} __attribute__((packed)); // 确保没有填充字节

class ZeroCopyOrderSerializer {
public:
    // 序列化：直接拷贝内存
    static std::string serialize(const std::vector<OrderUpdate>& orders) {
        if (orders.empty()) {
            return {};
        }
        
        const size_t dataSize = orders.size() * sizeof(OrderUpdate);
        std::string result(dataSize, '\0');
        
        std::memcpy(result.data(), orders.data(), dataSize);
        return result;
    }
    
    // 反序列化：直接内存映射（只读视图）
    static std::span<const OrderUpdate> deserializeView(const std::string& data) {
        if (data.empty()) {
            return {};
        }
        
        // 验证大小对齐
        if (data.size() % sizeof(OrderUpdate) != 0) {
            throw std::invalid_argument("Data size not aligned to OrderUpdate size");
        }
        
        const size_t count = data.size() / sizeof(OrderUpdate);
        const OrderUpdate* ptr = reinterpret_cast<const OrderUpdate*>(data.data());
        
        return std::span<const OrderUpdate>(ptr, count);
    }
    
    // 反序列化：拷贝到vector（如果需要修改）
    static std::vector<OrderUpdate> deserializeCopy(const std::string& data) {
        auto view = deserializeView(data);
        return std::vector<OrderUpdate>(view.begin(), view.end());
    }
};