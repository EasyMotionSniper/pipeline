class BinaryOrderSerializer {
public:
    static std::string serialize(const std::vector<OrderUpdate>& orders) {
        std::string result;
        size_t total_size = sizeof(size_t); // vector size
        total_size += orders.size() * sizeof(OrderUpdate);
        
        result.reserve(total_size);
        
        // 写入vector大小
        size_t count = orders.size();
        result.append(reinterpret_cast<const char*>(&count), sizeof(count));
        
        // 写入数据
        for (const auto& order : orders) {
            result.append(reinterpret_cast<const char*>(&order), sizeof(order));
        }
        
        return result;
    }
    
    static std::vector<OrderUpdate> deserialize(const std::string& data) {
        if (data.size() < sizeof(size_t)) {
            return {};
        }
        
        const char* ptr = data.data();
        
        // 读取vector大小
        size_t count;
        std::memcpy(&count, ptr, sizeof(count));
        ptr += sizeof(count);
        
        std::vector<OrderUpdate> orders;
        orders.reserve(count);
        
        // 读取数据
        for (size_t i = 0; i < count; ++i) {
            OrderUpdate order;
            std::memcpy(&order, ptr, sizeof(order));
            ptr += sizeof(order);
            orders.push_back(order);
        }
        
        return orders;
    }
};