#include "order_reader.h"
#include "dos_kv_client.h"
#include <iostream>
#include <vector>

int main() {
    try {
        // 创建DOSKVClient实例，连接到指定的S3桶
        auto kv_store = std::make_shared<DOSKVClient>(
            "xxx",  // S3桶名
            50,     // 内存缓存容量
            "./s3_cache",  // 磁盘缓存路径
            1024 * 1024 * 1024  // 磁盘缓存大小1GB
        );
        
        // 设置日期和skey列表
        std::string date = "2024-08-15";
        std::vector<int> skey_list = {1001, 1002, 1003, 1004, 1005};
        
        // 创建OrderReader
        OrderReader reader(kv_store, date, skey_list);
        
        // 读取并处理归并后的订单数据
        size_t count = 0;
        std::optional<Order> order;
        while ((order = reader.nextOrder()).has_value()) {
            // 处理订单数据
            // std::cout << "Order " << ++count << ": "
            //           << "skey=" << order->skey << ", "
            //           << "timestamp=" << order->timestamp << ", "
            //           << "bidPrice=" << order->bidPrice << ", "
            //           << "askPrice=" << order->askPrice << std::endl;
            
            // 每处理1000个订单输出一次进度
            if (++count % 1000 == 0) {
                std::cout << "Processed " << count << " orders..." << std::endl;
            }
        }
        
        std::cout << "Processing complete. Total orders: " << count << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
