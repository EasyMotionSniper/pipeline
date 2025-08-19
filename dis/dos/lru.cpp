#include <fstream>
#include <unordered_map>
#include <list>
#include <mutex>
#include <chrono>
#include <filesystem>
#include <json/json.h>

class LRUPackedS3Cache {
public:
    struct FileMetadata {
        uint64_t offset;          // 在pack文件中的偏移
        uint32_t size;            // 文件大小
        uint32_t checksum;        // 校验和
        uint64_t lastAccess;      // 最后访问时间戳
        uint64_t createTime;      // 创建时间
        uint32_t accessCount;     // 访问次数
        bool isValid;             // 是否有效（用于标记删除）
    };
    
    struct PackHeader {
        uint32_t magic = 0x4C525550;  // "LRUP" (LRU Pack)
        uint32_t version = 1;
        uint32_t totalFiles;
        uint32_t validFiles;           // 有效文件数量
        uint64_t indexOffset;          // 索引偏移
        uint64_t totalSize;
        uint64_t validSize;            // 有效数据大小
        double fragmentationRatio;     // 碎片率
    };
    
    struct CacheStats {
        uint64_t hits = 0;
        uint64_t misses = 0;
        uint64_t evictions = 0;
        uint64_t compactions = 0;
        uint64_t totalAccess = 0;
        
        double getHitRate() const {
            return totalAccess > 0 ? static_cast<double>(hits) / totalAccess : 0.0;
        }
    };

    explicit LRUPackedS3Cache(
        const std::string& cacheDir,
        size_t maxCacheSize = 1000,           // 最大缓存文件数
        size_t maxPackSize = 2ULL * 1024 * 1024 * 1024, // 2GB pack文件大小限制
        double compactionThreshold = 0.3      // 碎片率超过30%时进行整理
    ) : cacheDir_(cacheDir), 
        maxCacheSize_(maxCacheSize),
        maxPackSize_(maxPackSize),
        compactionThreshold_(compactionThreshold) {
        
        std::filesystem::create_directories(cacheDir_);
        
        packFilePath_ = cacheDir_ + "/lru_cache.pack";
        indexFilePath_ = cacheDir_ + "/lru_cache.idx";
        metadataFilePath_ = cacheDir_ + "/lru_metadata.json";
        
        loadMetadata();
        loadIndex();
        
        // 启动后台整理线程
        startBackgroundCompaction();
    }
    
    ~LRUPackedS3Cache() {
        stopBackgroundCompaction();
        saveMetadata();
        saveIndex();
    }
    
    // 添加或更新缓存项
    bool put(const std::string& s3Key, const std::vector<OrderUpdate>& data) {
        std::unique_lock<std::shared_mutex> lock(cacheMutex_);
        
        // 序列化数据
        std::string serializedData = BinaryOrderSerializer::serialize(data);
        
        // 检查是否已存在
        auto it = index_.find(s3Key);
        if (it != index_.end()) {
            // 更新现有项
            updateExistingItem(s3Key, serializedData);
            updateLRU(s3Key);
            return true;
        }
        
        // 检查是否需要驱逐
        while (index_.size() >= maxCacheSize_) {
            if (!evictLRU()) {
                return false; // 驱逐失败
            }
        }
        
        // 添加新项
        return addNewItem(s3Key, serializedData);
    }
    
    // 获取缓存项
    std::vector<OrderUpdate> get(const std::string& s3Key) {
        std::shared_lock<std::shared_mutex> readLock(cacheMutex_);
        
        auto it = index_.find(s3Key);
        if (it == index_.end() || !it->second.isValid) {
            stats_.misses++;
            stats_.totalAccess++;
            return {}; // 缓存未命中
        }
        
        // 读取数据
        std::vector<OrderUpdate> result = readFromPack(s3Key, it->second);
        
        readLock.unlock();
        
        // 更新LRU（需要写锁）
        std::unique_lock<std::shared_mutex> writeLock(cacheMutex_);
        updateLRU(s3Key);
        
        stats_.hits++;
        stats_.totalAccess++;
        
        return result;
    }
    
    // 批量获取
    std::unordered_map<std::string, std::vector<OrderUpdate>> 
    getBatch(const std::vector<std::string>& s3Keys) {
        std::unordered_map<std::string, std::vector<OrderUpdate>> result;
        
        // 按偏移排序以优化IO
        std::vector<std::pair<std::string, FileMetadata>> sortedRequests;
        
        {
            std::shared_lock<std::shared_mutex> lock(cacheMutex_);
            
            for (const auto& key : s3Keys) {
                auto it = index_.find(key);
                if (it != index_.end() && it->second.isValid) {
                    sortedRequests.emplace_back(key, it->second);
                }
            }
        }
        
        std::sort(sortedRequests.begin(), sortedRequests.end(),
                 [](const auto& a, const auto& b) {
                     return a.second.offset < b.second.offset;
                 });
        
        // 批量读取
        std::ifstream packFile(packFilePath_, std::ios::binary);
        if (!packFile.is_open()) return result;
        
        for (const auto& [key, metadata] : sortedRequests) {
            packFile.seekg(metadata.offset);
            
            std::vector<char> buffer(metadata.size);
            packFile.read(buffer.data(), metadata.size);
            
            if (packFile.gcount() == static_cast<std::streamsize>(metadata.size)) {
                // 验证校验和
                uint32_t actualChecksum = calculateChecksum(
                    std::vector<uint8_t>(buffer.begin(), buffer.end())
                );
                
                if (actualChecksum == metadata.checksum) {
                    std::string dataStr(buffer.begin(), buffer.end());
                    result[key] = BinaryOrderSerializer::deserialize(dataStr);
                }
            }
        }
        
        // 批量更新LRU
        {
            std::unique_lock<std::shared_mutex> lock(cacheMutex_);
            for (const auto& [key, metadata] : sortedRequests) {
                updateLRU(key);
            }
            
            stats_.hits += result.size();
            stats_.misses += (s3Keys.size() - result.size());
            stats_.totalAccess += s3Keys.size();
        }
        
        return result;
    }
    
    // 获取缓存统计信息
    CacheStats getStats() const {
        std::shared_lock<std::shared_mutex> lock(cacheMutex_);
        return stats_;
    }
    
    // 手动触发整理
    bool compactCache() {
        std::unique_lock<std::shared_mutex> lock(cacheMutex_);
        return performCompaction();
    }
    
    // 清理过期数据
    size_t cleanupExpired(uint64_t maxAge = 7 * 24 * 3600) { // 默认7天
        std::unique_lock<std::shared_mutex> lock(cacheMutex_);
        
        uint64_t currentTime = getCurrentTimestamp();
        std::vector<std::string> expiredKeys;
        
        for (const auto& [key, metadata] : index_) {
            if (metadata.isValid && (currentTime - metadata.lastAccess) > maxAge) {
                expiredKeys.push_back(key);
            }
        }
        
        for (const auto& key : expiredKeys) {
            markAsDeleted(key);
        }
        
        return expiredKeys.size();
    }

private:
    std::string cacheDir_;
    std::string packFilePath_;
    std::string indexFilePath_;
    std::string metadataFilePath_;
    
    size_t maxCacheSize_;
    size_t maxPackSize_;
    double compactionThreshold_;
    
    mutable std::shared_mutex cacheMutex_;
    std::unordered_map<std::string, FileMetadata> index_;
    std::list<std::string> lruList_;  // 最近使用的在前面
    std::unordered_map<std::string, std::list<std::string>::iterator> lruIndex_;
    
    CacheStats stats_;
    
    // 后台整理线程
    std::unique_ptr<std::thread> compactionThread_;
    std::atomic<bool> stopCompaction_{false};
    
    void startBackgroundCompaction() {
        compactionThread_ = std::make_unique<std::thread>([this]() {
            while (!stopCompaction_) {
                std::this_thread::sleep_for(std::chrono::minutes(30));
                
                if (stopCompaction_) break;
                
                // 检查是否需要整理
                std::shared_lock<std::shared_mutex> lock(cacheMutex_);
                PackHeader header = readPackHeader();
                lock.unlock();
                
                if (header.fragmentationRatio > compactionThreshold_) {
                    std::cout << "触发后台整理，碎片率: " 
                              << (header.fragmentationRatio * 100) << "%" << std::endl;
                    
                    std::unique_lock<std::shared_mutex> writeLock(cacheMutex_);
                    performCompaction();
                }
            }
        });
    }
    
    void stopBackgroundCompaction() {
        stopCompaction_ = true;
        if (compactionThread_ && compactionThread_->joinable()) {
            compactionThread_->join();
        }
    }
    
    void updateLRU(const std::string& s3Key) {
        // 更新访问时间和计数
        auto& metadata = index_[s3Key];
        metadata.lastAccess = getCurrentTimestamp();
        metadata.accessCount++;
        
        // 更新LRU链表
        auto lruIt = lruIndex_.find(s3Key);
        if (lruIt != lruIndex_.end()) {
            lruList_.erase(lruIt->second);
        }
        
        lruList_.push_front(s3Key);
        lruIndex_[s3Key] = lruList_.begin();
    }
    
    bool evictLRU() {
        if (lruList_.empty()) return false;
        
        // 从最后面开始驱逐（最久未使用）
        std::string evictKey = lruList_.back();
        lruList_.pop_back();
        lruIndex_.erase(evictKey);
        
        // 标记为删除（不立即删除，等待整理）
        markAsDeleted(evictKey);
        
        stats_.evictions++;
        
        std::cout << "驱逐LRU项: " << evictKey << std::endl;
        return true;
    }
    
    void markAsDeleted(const std::string& s3Key) {
        auto it = index_.find(s3Key);
        if (it != index_.end()) {
            it->second.isValid = false;
        }
    }
    
    bool addNewItem(const std::string& s3Key, const std::string& data) {
        // 打开pack文件进行追加
        std::fstream packFile(packFilePath_, std::ios::binary | std::ios::in | std::ios::out);
        
        if (!packFile.is_open()) {
            // 创建新文件
            std::ofstream newFile(packFilePath_, std::ios::binary);
            if (!newFile.is_open()) return false;
            
            PackHeader header{};
            newFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
            newFile.close();
            
            packFile.open(packFilePath_, std::ios::binary | std::ios::in | std::ios::out);
            if (!packFile.is_open()) return false;
        }
        
        // 定位到文件末尾
        packFile.seekg(0, std::ios::end);
        uint64_t offset = packFile.tellg();
        
        // 检查文件大小限制
        if (offset + data.size() > maxPackSize_) {
            std::cout << "Pack文件大小超限，需要整理" << std::endl;
            if (!performCompaction()) {
                return false;
            }
            
            // 重新定位
            packFile.seekg(0, std::ios::end);
            offset = packFile.tellg();
        }
        
        // 写入数据
        packFile.write(data.data(), data.size());
        packFile.flush();
        
        // 创建metadata
        FileMetadata metadata{
            .offset = offset,
            .size = static_cast<uint32_t>(data.size()),
            .checksum = calculateChecksum(std::vector<uint8_t>(data.begin(), data.end())),
            .lastAccess = getCurrentTimestamp(),
            .createTime = getCurrentTimestamp(),
            .accessCount = 1,
            .isValid = true
        };
        
        // 更新索引
        index_[s3Key] = metadata;
        updateLRU(s3Key);
        
        return true;
    }
    
    void updateExistingItem(const std::string& s3Key, const std::string& data) {
        // 对于现有项的更新，标记旧数据为无效，追加新数据
        markAsDeleted(s3Key);
        addNewItem(s3Key, data);
    }
    
    std::vector<OrderUpdate> readFromPack(const std::string& s3Key, const FileMetadata& metadata) {
        std::ifstream packFile(packFilePath_, std::ios::binary);
        if (!packFile.is_open()) return {};
        
        packFile.seekg(metadata.offset);
        
        std::vector<char> buffer(metadata.size);
        packFile.read(buffer.data(), metadata.size);
        
        if (packFile.gcount() != static_cast<std::streamsize>(metadata.size)) {
            return {};
        }
        
        // 验证校验和
        uint32_t actualChecksum = calculateChecksum(
            std::vector<uint8_t>(buffer.begin(), buffer.end())
        );
        
        if (actualChecksum != metadata.checksum) {
            std::cerr << "校验和验证失败: " << s3Key << std::endl;
            return {};
        }
        
        std::string dataStr(buffer.begin(), buffer.end());
        return BinaryOrderSerializer::deserialize(dataStr);
    }
    
    bool performCompaction() {
        std::cout << "开始执行缓存整理..." << std::endl;
        
        std::string tempPackPath = packFilePath_ + ".tmp";
        std::ofstream tempPack(tempPackPath, std::ios::binary);
        
        if (!tempPack.is_open()) return false;
        
        // 写入临时头部
        PackHeader tempHeader{};
        tempPack.write(reinterpret_cast<const char*>(&tempHeader), sizeof(tempHeader));
        
        // 收集有效数据并按LRU顺序重写
        std::unordered_map<std::string, FileMetadata> newIndex;
        uint64_t newOffset = sizeof(PackHeader);
        uint32_t validFiles = 0;
        
        std::ifstream oldPack(packFilePath_, std::ios::binary);
        if (!oldPack.is_open()) {
            tempPack.close();
            std::filesystem::remove(tempPackPath);
            return false;
        }
        
        // 按LRU顺序（从最近使用到最久使用）重写数据
        for (const auto& key : lruList_) {
            auto it = index_.find(key);
            if (it != index_.end() && it->second.isValid) {
                const FileMetadata& oldMeta = it->second;
                
                // 从旧文件读取数据
                oldPack.seekg(oldMeta.offset);
                std::vector<char> data(oldMeta.size);
                oldPack.read(data.data(), oldMeta.size);
                
                if (oldPack.gcount() == static_cast<std::streamsize>(oldMeta.size)) {
                    // 写入新位置
                    tempPack.write(data.data(), data.size());
                    
                    // 更新索引
                    FileMetadata newMeta = oldMeta;
                    newMeta.offset = newOffset;
                    newIndex[key] = newMeta;
                    
                    newOffset += data.size();
                    validFiles++;
                }
            }
        }
        
        oldPack.close();
        
        // 写入最终头部
        PackHeader finalHeader{
            .magic = 0x4C525550,
            .version = 1,
            .totalFiles = validFiles,
            .validFiles = validFiles,
            .indexOffset = newOffset,
            .totalSize = newOffset,
            .validSize = newOffset - sizeof(PackHeader),
            .fragmentationRatio = 0.0
        };
        
        tempPack.seekp(0);
        tempPack.write(reinterpret_cast<const char*>(&finalHeader), sizeof(finalHeader));
        tempPack.close();
        
        // 原子替换文件
        if (std::filesystem::exists(packFilePath_)) {
            std::filesystem::remove(packFilePath_);
        }
        std::filesystem::rename(tempPackPath, packFilePath_);
        
        // 更新索引
        index_ = std::move(newIndex);
        
        stats_.compactions++;
        
        std::cout << "整理完成，有效文件: " << validFiles 
                  << ", 新大小: " << formatSize(finalHeader.totalSize) << std::endl;
        
        return true;
    }
    
    void saveMetadata() {
        Json::Value root;
        Json::Value statsJson;
        
        statsJson["hits"] = static_cast<Json::UInt64>(stats_.hits);
        statsJson["misses"] = static_cast<Json::UInt64>(stats_.misses);
        statsJson["evictions"] = static_cast<Json::UInt64>(stats_.evictions);
        statsJson["compactions"] = static_cast<Json::UInt64>(stats_.compactions);
        statsJson["totalAccess"] = static_cast<Json::UInt64>(stats_.totalAccess);
        
        root["stats"] = statsJson;
        root["maxCacheSize"] = static_cast<Json::UInt64>(maxCacheSize_);
        root["maxPackSize"] = static_cast<Json::UInt64>(maxPackSize_);
        root["compactionThreshold"] = compactionThreshold_;
        root["lastSave"] = static_cast<Json::UInt64>(getCurrentTimestamp());
        
        // 保存LRU顺序
        Json::Value lruArray(Json::arrayValue);
        for (const auto& key : lruList_) {
            lruArray.append(key);
        }
        root["lruOrder"] = lruArray;
        
        std::ofstream metaFile(metadataFilePath_);
        if (metaFile.is_open()) {
            Json::StreamWriterBuilder builder;
            std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
            writer->write(root, &metaFile);
        }
    }
    
    void loadMetadata() {
        std::ifstream metaFile(metadataFilePath_);
        if (!metaFile.is_open()) return;
        
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        
        if (Json::parseFromStream(builder, metaFile, &root, &errors)) {
            // 加载统计信息
            if (root.isMember("stats")) {
                const Json::Value& statsJson = root["stats"];
                stats_.hits = statsJson.get("hits", 0).asUInt64();
                stats_.misses = statsJson.get("misses", 0).asUInt64();
                stats_.evictions = statsJson.get("evictions", 0).asUInt64();
                stats_.compactions = statsJson.get("compactions", 0).asUInt64();
                stats_.totalAccess = statsJson.get("totalAccess", 0).asUInt64();
            }
            
            // 加载LRU顺序
            if (root.isMember("lruOrder")) {
                const Json::Value& lruArray = root["lruOrder"];
                for (const auto& item : lruArray) {
                    std::string key = item.asString();
                    lruList_.push_back(key);
                    lruIndex_[key] = std::prev(lruList_.end());
                }
            }
            
            std::cout << "从metadata恢复: " << lruList_.size() << " 个LRU项" << std::endl;
        }
    }
    
    void saveIndex() {
        std::ofstream indexFile(indexFilePath_, std::ios::binary);
        if (!indexFile.is_open()) return;
        
        uint32_t count = static_cast<uint32_t>(index_.size());
        indexFile.write(reinterpret_cast<const char*>(&count), sizeof(count));
        
        for (const auto& [key, metadata] : index_) {
            uint32_t keyLength = static_cast<uint32_t>(key.length());
            indexFile.write(reinterpret_cast<const char*>(&keyLength), sizeof(keyLength));
            indexFile.write(key.data(), key.length());
            indexFile.write(reinterpret_cast<const char*>(&metadata), sizeof(metadata));
        }
    }
    
    void loadIndex() {
        std::ifstream indexFile(indexFilePath_, std::ios::binary);
        if (!indexFile.is_open()) return;
        
        uint32_t count;
        indexFile.read(reinterpret_cast<char*>(&count), sizeof(count));
        
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t keyLength;
            indexFile.read(reinterpret_cast<char*>(&keyLength), sizeof(keyLength));
            
            std::string key(keyLength, '\0');
            indexFile.read(key.data(), keyLength);
            
            FileMetadata metadata;
            indexFile.read(reinterpret_cast<char*>(&metadata), sizeof(metadata));
            
            index_[key] = metadata;
            
            // 如果LRU中没有这个key，添加到末尾
            if (lruIndex_.find(key) == lruIndex_.end()) {
                lruList_.push_back(key);
                lruIndex_[key] = std::prev(lruList_.end());
            }
        }
        
        std::cout << "从索引文件加载: " << index_.size() << " 个缓存项" << std::endl;
    }
    
    PackHeader readPackHeader() {
        std::ifstream packFile(packFilePath_, std::ios::binary);
        PackHeader header{};
        
        if (packFile.is_open()) {
            packFile.read(reinterpret_cast<char*>(&header), sizeof(header));
        }
        
        return header;
    }
    
    uint32_t calculateChecksum(const std::vector<uint8_t>& data) {
        uint32_t checksum = 0;
        for (uint8_t byte : data) {
            checksum = checksum * 31 + byte;
        }
        return checksum;
    }
    
    uint64_t getCurrentTimestamp() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }
    
    std::string formatSize(uint64_t bytes) {
        if (bytes < 1024) return std::to_string(bytes) + "B";
        if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + "KB";
        if (bytes < 1024ULL * 1024 * 1024) return std::to_string(bytes / (1024 * 1024)) + "MB";
        return std::to_string(bytes / (1024ULL * 1024 * 1024)) + "GB";
    }
};