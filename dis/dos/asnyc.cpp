#include <unordered_map>
#include <list>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <chrono>

template<typename KeyType, typename ValueType>
class AsyncLruCache {
private:
    // LRU节点结构
    struct CacheNode {
        KeyType key;
        ValueType value;
        bool isLoading = false;
        std::shared_ptr<std::promise<ValueType>> loadingPromise;
        
        CacheNode(const KeyType& k, const ValueType& v) 
            : key(k), value(v) {}
    };
    
    using NodeIterator = typename std::list<CacheNode>::iterator;
    
    // 成员变量
    size_t maxCapacity;
    std::list<CacheNode> cacheList;
    std::unordered_map<KeyType, NodeIterator> cacheMap;
    std::function<std::future<ValueType>(const KeyType&)> dataLoader;
    mutable std::mutex cacheMutex;
    
    // 移动节点到链表头部
    void moveToFront(NodeIterator iter) {
        if (iter != cacheList.begin()) {
            cacheList.splice(cacheList.begin(), cacheList, iter);
        }
    }
    
    // 移除最久未使用的节点
    void evictLeastRecentlyUsed() {
        if (!cacheList.empty()) {
            auto lastNode = cacheList.back();
            cacheMap.erase(lastNode.key);
            cacheList.pop_back();
        }
    }
    
    // 异步加载数据的内部方法
    void loadDataAsync(const KeyType& key) {
        // 创建加载中的占位节点
        cacheList.emplace_front(key, ValueType{});
        auto newNodeIter = cacheList.begin();
        newNodeIter->isLoading = true;
        newNodeIter->loadingPromise = std::make_shared<std::promise<ValueType>>();
        
        cacheMap[key] = newNodeIter;
        
        // 如果超过容量，移除最久未使用的节点
        if (cacheMap.size() > maxCapacity) {
            evictLeastRecentlyUsed();
        }
        
        // 启动异步加载任务
        std::thread([this, key, newNodeIter]() {
            try {
                auto futureValue = dataLoader(key);
                auto loadedValue = futureValue.get();
                
                std::lock_guard<std::mutex> lock(cacheMutex);
                
                // 检查节点是否仍在缓存中（可能已被驱逐）
                auto mapIter = cacheMap.find(key);
                if (mapIter != cacheMap.end() && mapIter->second == newNodeIter) {
                    newNodeIter->value = loadedValue;
                    newNodeIter->isLoading = false;
                    newNodeIter->loadingPromise->set_value(loadedValue);
                }
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(cacheMutex);
                auto mapIter = cacheMap.find(key);
                if (mapIter != cacheMap.end() && mapIter->second == newNodeIter) {
                    newNodeIter->loadingPromise->set_exception(std::current_exception());
                    // 移除加载失败的节点
                    cacheMap.erase(key);
                    cacheList.erase(newNodeIter);
                }
            }
        }).detach();
    }

public:
    // 构造函数
    AsyncLruCache(size_t capacity, 
                  std::function<std::future<ValueType>(const KeyType&)> loader)
        : maxCapacity(capacity), dataLoader(std::move(loader)) {}
    
    // 获取数据（异步触发加载）
    std::optional<ValueType> get(const KeyType& key) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        
        auto mapIter = cacheMap.find(key);
        if (mapIter != cacheMap.end()) {
            auto nodeIter = mapIter->second;
            
            if (nodeIter->isLoading) {
                // 数据正在加载中，返回空
                return std::nullopt;
            } else {
                // 数据已加载，移动到前面并返回
                moveToFront(nodeIter);
                return nodeIter->value;
            }
        } else {
            // 缓存未命中，异步触发加载
            loadDataAsync(key);
            return std::nullopt;
        }
    }
    
    // 阻塞式获取数据（等待加载完成）
    ValueType getBlocking(const KeyType& key) {
        std::unique_lock<std::mutex> lock(cacheMutex);
        
        auto mapIter = cacheMap.find(key);
        if (mapIter != cacheMap.end()) {
            auto nodeIter = mapIter->second;
            
            if (nodeIter->isLoading) {
                // 等待加载完成
                auto promise = nodeIter->loadingPromise;
                lock.unlock();
                
                auto futureValue = promise->get_future();
                auto result = futureValue.get();
                
                lock.lock();
                moveToFront(nodeIter);
                return result;
            } else {
                // 数据已存在
                moveToFront(nodeIter);
                return nodeIter->value;
            }
        } else {
            // 缓存未命中，同步加载
            loadDataAsync(key);
            auto nodeIter = cacheMap[key];
            auto promise = nodeIter->loadingPromise;
            lock.unlock();
            
            auto futureValue = promise->get_future();
            auto result = futureValue.get();
            
            lock.lock();
            moveToFront(nodeIter);
            return result;
        }
    }
    
    // 手动插入数据
    void put(const KeyType& key, const ValueType& value) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        
        auto mapIter = cacheMap.find(key);
        if (mapIter != cacheMap.end()) {
            // 更新已存在的键
            auto nodeIter = mapIter->second;
            nodeIter->value = value;
            nodeIter->isLoading = false;
            moveToFront(nodeIter);
        } else {
            // 插入新键值对
            cacheList.emplace_front(key, value);
            cacheMap[key] = cacheList.begin();
            
            if (cacheMap.size() > maxCapacity) {
                evictLeastRecentlyUsed();
            }
        }
    }
    
    // 检查键是否存在
    bool contains(const KeyType& key) const {
        std::lock_guard<std::mutex> lock(cacheMutex);
        return cacheMap.find(key) != cacheMap.end();
    }
    
    // 检查键是否正在加载
    bool isLoading(const KeyType& key) const {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto mapIter = cacheMap.find(key);
        return mapIter != cacheMap.end() && mapIter->second->isLoading;
    }
    
    // 移除指定键
    void remove(const KeyType& key) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto mapIter = cacheMap.find(key);
        if (mapIter != cacheMap.end()) {
            cacheList.erase(mapIter->second);
            cacheMap.erase(mapIter);
        }
    }
    
    // 清空缓存
    void clear() {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cacheMap.clear();
        cacheList.clear();
    }
    
    // 获取当前缓存大小
    size_t size() const {
        std::lock_guard<std::mutex> lock(cacheMutex);
        return cacheMap.size();
    }
    
    // 获取最大容量
    size_t getMaxCapacity() const {
        return maxCapacity;
    }
    
    // 设置新的最大容量
    void setMaxCapacity(size_t newCapacity) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        maxCapacity = newCapacity;
        
        // 如果新容量小于当前大小，移除多余节点
        while (cacheMap.size() > maxCapacity) {
            evictLeastRecentlyUsed();
        }
    }
};

// 使用示例
#include <iostream>
#include <string>

// 模拟数据加载器
std::future<std::string> simulateDataLoader(const int& key) {
    return std::async(std::launch::async, [key]() {
        // 模拟网络请求或数据库查询延迟
        std::this_thread::sleep_for(std::chrono::milliseconds(100 + key % 500));
        return "Data for key: " + std::to_string(key);
    });
}

int main() {
    // 创建异步LRU缓存
    AsyncLruCache<int, std::string> asyncCache(3, simulateDataLoader);
    
    std::cout << "=== 异步LRU缓存测试 ===" << std::endl;
    
    // 测试异步获取
    std::cout << "\n1. 异步获取测试:" << std::endl;
    for (int i = 1; i <= 3; ++i) {
        auto result = asyncCache.get(i);
        if (result) {
            std::cout << "Key " << i << " (缓存命中): " << *result << std::endl;
        } else {
            std::cout << "Key " << i << " (缓存未命中，触发异步加载)" << std::endl;
        }
    }
    
    // 等待一段时间让异步加载完成
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    
    std::cout << "\n2. 再次获取（应该命中缓存）:" << std::endl;
    for (int i = 1; i <= 3; ++i) {
        auto result = asyncCache.get(i);
        if (result) {
            std::cout << "Key " << i << " (缓存命中): " << *result << std::endl;
        } else {
            std::cout << "Key " << i << " (缓存未命中)" << std::endl;
        }
    }
    
    // 测试阻塞式获取
    std::cout << "\n3. 阻塞式获取测试:" << std::endl;
    auto blockingResult = asyncCache.getBlocking(4);
    std::cout << "Key 4 (阻塞获取): " << blockingResult << std::endl;
    
    std::cout << "\n4. 缓存状态:" << std::endl;
    std::cout << "缓存大小: " << asyncCache.size() << std::endl;
    std::cout << "最大容量: " << asyncCache.getMaxCapacity() << std::endl;
    
    return 0;
}