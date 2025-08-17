#include <nlohmann/json.hpp>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>
#include <unordered_map>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <fstream>
#include <filesystem>
#include <sstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

// 基础数据类型枚举
enum class DataType {
    ORDER_UPDATE,
    TRADE_UPDATE
};

// LRU缓存实现
template<typename KeyType, typename ValueType>
class LRUCache {
private:
    using ListItem = std::pair<KeyType, ValueType>;
    using ListIter = typename std::list<ListItem>::iterator;
    
    size_t capacity_;
    std::list<ListItem> items_;
    std::unordered_map<KeyType, ListIter> index_;
    mutable std::shared_mutex mutex_;
    
public:
    explicit LRUCache(size_t capacity) : capacity_(capacity) {}
    
    std::optional<ValueType> get(const KeyType& key) {
        std::unique_lock lock(mutex_);
        
        auto it = index_.find(key);
        if (it == index_.end()) {
            return std::nullopt;
        }
        
        // 移动到最前面
        items_.splice(items_.begin(), items_, it->second);
        return it->second->second;
    }
    
    void put(const KeyType& key, const ValueType& value) {
        std::unique_lock lock(mutex_);
        
        auto it = index_.find(key);
        if (it != index_.end()) {
            // 更新现有项
            it->second->second = value;
            items_.splice(items_.begin(), items_, it->second);
            return;
        }
        
        // 添加新项
        if (items_.size() >= capacity_) {
            // 删除最久未使用的项
            index_.erase(items_.back().first);
            items_.pop_back();
        }
        
        items_.emplace_front(key, value);
        index_[key] = items_.begin();
    }
    
    void clear() {
        std::unique_lock lock(mutex_);
        items_.clear();
        index_.clear();
    }
};

// 通用的数据更新基类
class DataUpdateBase {
protected:
    std::shared_ptr<Aws::S3::S3Client> s3_client_;
    std::string bucket_name_;
    std::string local_cache_dir_;
    LRUCache<std::string, std::shared_ptr<arrow::Table>> memory_cache_;
    json config_;
    
    // 从S3读取Parquet文件
    std::shared_ptr<arrow::Table> readFromS3(const std::string& key) {
        Aws::S3::Model::GetObjectRequest request;
        request.SetBucket(bucket_name_);
        request.SetKey(key);
        
        auto outcome = s3_client_->GetObject(request);
        if (!outcome.IsSuccess()) {
            throw std::runtime_error("Failed to get object from S3: " + 
                                   outcome.GetError().GetMessage());
        }
        
        auto& stream = outcome.GetResult().GetBody();
        std::ostringstream ss;
        ss << stream.rdbuf();
        std::string content = ss.str();
        
        auto buffer = arrow::Buffer::FromString(content);
        auto input = std::make_shared<arrow::io::BufferReader>(buffer);
        
        std::unique_ptr<parquet::arrow::FileReader> reader;
        PARQUET_THROW_NOT_OK(parquet::arrow::OpenFile(input, 
                                                      arrow::default_memory_pool(), 
                                                      &reader));
        
        std::shared_ptr<arrow::Table> table;
        PARQUET_THROW_NOT_OK(reader->ReadTable(&table));
        return table;
    }
    
    // 从本地读取Parquet文件
    std::shared_ptr<arrow::Table> readFromLocal(const std::string& path) {
        std::shared_ptr<arrow::io::ReadableFile> input;
        PARQUET_ASSIGN_OR_THROW(input, arrow::io::ReadableFile::Open(path));
        
        std::unique_ptr<parquet::arrow::FileReader> reader;
        PARQUET_THROW_NOT_OK(parquet::arrow::OpenFile(input, 
                                                      arrow::default_memory_pool(), 
                                                      &reader));
        
        std::shared_ptr<arrow::Table> table;
        PARQUET_THROW_NOT_OK(reader->ReadTable(&table));
        return table;
    }
    
    // 保存到本地缓存
    void saveToLocalCache(const std::string& cache_path, 
                         std::shared_ptr<arrow::Table> table) {
        fs::create_directories(fs::path(cache_path).parent_path());
        
        std::shared_ptr<arrow::io::FileOutputStream> output;
        PARQUET_ASSIGN_OR_THROW(output, 
                               arrow::io::FileOutputStream::Open(cache_path));
        
        PARQUET_THROW_NOT_OK(parquet::arrow::WriteTable(*table, 
                                                        arrow::default_memory_pool(), 
                                                        output));
    }
    
    // 获取S3 key的抽象方法
    virtual std::string getS3Key(const std::string& date, 
                                 const std::string& sec_id) const = 0;
    
public:
    DataUpdateBase(std::shared_ptr<Aws::S3::S3Client> client,
                   const std::string& bucket,
                   const std::string& cache_dir,
                   size_t cache_size)
        : s3_client_(client)
        , bucket_name_(bucket)
        , local_cache_dir_(cache_dir)
        , memory_cache_(cache_size) {
        
        // 加载配置
        loadConfig();
    }
    
    void loadConfig() {
        std::string config_path = local_cache_dir_ + "/config.json";
        if (fs::exists(config_path)) {
            std::ifstream file(config_path);
            file >> config_;
        }
    }
    
    void saveConfig() {
        std::string config_path = local_cache_dir_ + "/config.json";
        fs::create_directories(fs::path(config_path).parent_path());
        std::ofstream file(config_path);
        file << config_.dump(4);
    }
    
    // 通用的数据获取方法
    std::shared_ptr<arrow::Table> getData(const std::string& date,
                                         const std::string& sec_id,
                                         int64_t start_idx = 0,
                                         int64_t end_idx = -1) {
        std::string cache_key = date + "/" + sec_id;
        
        // 1. 检查内存缓存
        auto cached = memory_cache_.get(cache_key);
        if (cached.has_value()) {
            return sliceTable(cached.value(), start_idx, end_idx);
        }
        
        // 2. 检查本地缓存
        std::string local_path = local_cache_dir_ + "/" + cache_key + ".parquet";
        std::shared_ptr<arrow::Table> table;
        
        if (fs::exists(local_path)) {
            // 检查是否需要更新
            if (needsUpdate(date, sec_id)) {
                table = updateData(date, sec_id);
            } else {
                table = readFromLocal(local_path);
            }
        } else {
            // 3. 从S3读取
            table = updateData(date, sec_id);
        }
        
        // 更新内存缓存
        memory_cache_.put(cache_key, table);
        
        return sliceTable(table, start_idx, end_idx);
    }
    
    // 检查是否需要更新
    bool needsUpdate(const std::string& date, const std::string& sec_id) {
        std::string key = date + "/" + sec_id;
        
        if (!config_.contains("last_update")) {
            return true;
        }
        
        auto& last_update = config_["last_update"];
        if (!last_update.contains(key)) {
            return true;
        }
        
        // 检查更新时间（这里简化为每天更新一次）
        auto last_time = last_update[key].get<std::string>();
        auto now = std::chrono::system_clock::now();
        auto now_time = std::chrono::system_clock::to_time_t(now);
        
        // 比较日期
        struct tm* tm_now = std::localtime(&now_time);
        char buffer[11];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d", tm_now);
        
        return std::string(buffer) != last_time;
    }
    
    // 更新数据
    std::shared_ptr<arrow::Table> updateData(const std::string& date,
                                            const std::string& sec_id) {
        std::string s3_key = getS3Key(date, sec_id);
        auto table = readFromS3(s3_key);
        
        // 保存到本地缓存
        std::string cache_key = date + "/" + sec_id;
        std::string local_path = local_cache_dir_ + "/" + cache_key + ".parquet";
        saveToLocalCache(local_path, table);
        
        // 更新配置
        auto now = std::chrono::system_clock::now();
        auto now_time = std::chrono::system_clock::to_time_t(now);
        struct tm* tm_now = std::localtime(&now_time);
        char buffer[11];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d", tm_now);
        
        config_["last_update"][cache_key] = std::string(buffer);
        saveConfig();
        
        return table;
    }
    
    // 切片处理
    std::shared_ptr<arrow::Table> sliceTable(std::shared_ptr<arrow::Table> table,
                                            int64_t start_idx,
                                            int64_t end_idx) {
        if (start_idx == 0 && end_idx == -1) {
            return table;
        }
        
        int64_t num_rows = table->num_rows();
        if (end_idx == -1 || end_idx > num_rows) {
            end_idx = num_rows;
        }
        
        if (start_idx < 0) {
            start_idx = 0;
        }
        
        if (start_idx >= end_idx) {
            // 返回空表
            return table->Slice(0, 0);
        }
        
        return table->Slice(start_idx, end_idx - start_idx);
    }
    
    // 清理缓存
    void clearCache() {
        memory_cache_.clear();
    }
    
    // 预加载数据
    void preload(const std::vector<std::pair<std::string, std::string>>& items) {
        for (const auto& [date, sec_id] : items) {
            getData(date, sec_id);
        }
    }
};

// OrderUpdate实现
class OrderUpdate : public DataUpdateBase {
protected:
    std::string getS3Key(const std::string& date, 
                        const std::string& sec_id) const override {
        return "mdl/mbd/order/" + date + "/" + sec_id + ".parquet";
    }
    
public:
    using DataUpdateBase::DataUpdateBase;
};

// TradeUpdate实现
class TradeUpdate : public DataUpdateBase {
protected:
    std::string getS3Key(const std::string& date, 
                        const std::string& sec_id) const override {
        return "mdl/mbd/trade/" + date + "/" + sec_id + ".parquet";
    }
    
public:
    using DataUpdateBase::DataUpdateBase;
};

// 工厂类用于创建不同类型的数据更新对象
class DataUpdateFactory {
public:
    static std::unique_ptr<DataUpdateBase> create(
        DataType type,
        std::shared_ptr<Aws::S3::S3Client> client,
        const std::string& bucket,
        const std::string& cache_dir,
        size_t cache_size = 100) {
        
        switch (type) {
            case DataType::ORDER_UPDATE:
                return std::make_unique<OrderUpdate>(client, bucket, 
                                                    cache_dir + "/order", 
                                                    cache_size);
            case DataType::TRADE_UPDATE:
                return std::make_unique<TradeUpdate>(client, bucket, 
                                                    cache_dir + "/trade", 
                                                    cache_size);
            default:
                throw std::invalid_argument("Unknown data type");
        }
    }
};

// 使用示例
class DataManager {
private:
    std::unordered_map<DataType, std::unique_ptr<DataUpdateBase>> handlers_;
    std::shared_ptr<Aws::S3::S3Client> s3_client_;
    
public:
    DataManager(std::shared_ptr<Aws::S3::S3Client> client,
                const std::string& bucket,
                const std::string& cache_dir) 
        : s3_client_(client) {
        
        // 初始化不同类型的处理器
        handlers_[DataType::ORDER_UPDATE] = 
            DataUpdateFactory::create(DataType::ORDER_UPDATE, client, bucket, cache_dir);
        handlers_[DataType::TRADE_UPDATE] = 
            DataUpdateFactory::create(DataType::TRADE_UPDATE, client, bucket, cache_dir);
    }
    
    // 获取订单数据
    std::shared_ptr<arrow::Table> getOrderData(const std::string& date,
                                              const std::string& sec_id,
                                              int64_t start_idx = 0,
                                              int64_t end_idx = -1) {
        return handlers_[DataType::ORDER_UPDATE]->getData(date, sec_id, start_idx, end_idx);
    }
    
    // 获取交易数据
    std::shared_ptr<arrow::Table> getTradeData(const std::string& date,
                                              const std::string& sec_id,
                                              int64_t start_idx = 0,
                                              int64_t end_idx = -1) {
        return handlers_[DataType::TRADE_UPDATE]->getData(date, sec_id, start_idx, end_idx);
    }
    
    // 预加载数据
    void preloadOrderData(const std::vector<std::pair<std::string, std::string>>& items) {
        handlers_[DataType::ORDER_UPDATE]->preload(items);
    }
    
    void preloadTradeData(const std::vector<std::pair<std::string, std::string>>& items) {
        handlers_[DataType::TRADE_UPDATE]->preload(items);
    }
    
    // 清理缓存
    void clearAllCaches() {
        for (auto& [type, handler] : handlers_) {
            handler->clearCache();
        }
    }
};

// 使用示例
int main() {
    // 初始化AWS S3客户端
    Aws::SDKOptions options;
    Aws::InitAPI(options);
    
    auto s3_client = std::make_shared<Aws::S3::S3Client>();
    
    // 创建数据管理器
    DataManager manager(s3_client, "your-bucket", "/tmp/cache");
    
    try {
        // 获取订单数据
        auto order_table = manager.getOrderData("2024-01-15", "AAPL", 0, 1000);
        std::cout << "Order rows: " << order_table->num_rows() << std::endl;
        
        // 获取交易数据
        auto trade_table = manager.getTradeData("2024-01-15", "AAPL", 0, 500);
        std::cout << "Trade rows: " << trade_table->num_rows() << std::endl;
        
        // 预加载数据
        std::vector<std::pair<std::string, std::string>> preload_list = {
            {"2024-01-15", "AAPL"},
            {"2024-01-15", "GOOGL"},
            {"2024-01-15", "MSFT"}
        };
        manager.preloadOrderData(preload_list);
        manager.preloadTradeData(preload_list);
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    
    Aws::ShutdownAPI(options);
    return 0;
}