#pragma once

#include <string>
#include <memory>

class IKVStore {
public:
    virtual ~IKVStore() = default;
    
    // 获取key对应的值，成功返回true，失败返回false
    virtual bool get(const std::string& key, std::string& value) = 0;
    
    // 设置key对应的值
    virtual void put(const std::string& key, const std::string& value) = 0;
};
