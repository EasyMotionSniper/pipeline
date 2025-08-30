// message_protocol.h
#pragma once
#include <cstdint>
#include <cstring>

#pragma pack(push, 1)

enum MessageType : uint16_t {
    MSG_HEARTBEAT = 0x0001,
    MSG_LOGIN = 0x0002,
    MSG_LOGIN_RESP = 0x0003,
    MSG_MARKET_DATA = 0x0004,
    MSG_NACK = 0x0005,
    MSG_LOGOUT = 0x0006,
};

struct MessageHeader {
    uint16_t msg_type;
    uint32_t msg_len;
    uint64_t sequence;
    uint64_t timestamp;
};

struct NetMessage {
    MessageHeader header;
    char data[0];
};

struct HeartbeatMsg {
    MessageHeader header;
    uint64_t last_recv_seq;
};

struct LoginMsg {
    MessageHeader header;
    char subscriber_id[32];
    uint64_t last_recv_seq;  // 断线重连时使用
};

struct LoginRespMsg {
    MessageHeader header;
    uint8_t success;
    uint64_t current_seq;
    char message[64];
};

struct MarketDataMsg {
    MessageHeader header;
    char symbol[16];
    double bid_price;
    double ask_price;
    uint64_t bid_volume;
    uint64_t ask_volume;
    uint64_t timestamp;
};

struct NackMsg {
    MessageHeader header;
    uint64_t start_seq;
    uint64_t end_seq;
};

struct Subscriber {
    char id[32];
    uint32_t ip;
    uint16_t port;
    uint64_t last_recv_seq;
    uint64_t last_heartbeat_time;
    bool active;
};

#pragma pack(pop)

// flow_manager.h
#pragma once
#include <leveldb/db.h>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <atomic>
#include "message_protocol.h"

class FlowManager {
public:
    FlowManager(const std::string& db_path);
    ~FlowManager();
    
    bool Init();
    void Shutdown();
    
    // 存储市场数据
    bool StoreMarketData(uint64_t sequence, const MarketDataMsg& data);
    
    // 查询市场数据（用于NACK重传）
    bool GetMarketData(uint64_t sequence, MarketDataMsg& data);
    
    // 批量查询
    std::vector<MarketDataMsg> GetMarketDataRange(uint64_t start_seq, uint64_t end_seq);
    
    // 获取当前序列号
    uint64_t GetCurrentSequence() const { return current_sequence_; }
    uint64_t GetNextSequence() { return ++current_sequence_; }
    
private:
    std::string db_path_;
    std::unique_ptr<leveldb::DB> db_;
    std::atomic<uint64_t> current_sequence_;
    mutable std::mutex mutex_;
    
    std::string MakeKey(uint64_t sequence);
};

// flow_manager.cpp
#include "flow_manager.h"
#include <leveldb/options.h>
#include <leveldb/write_batch.h>
#include <sstream>
#include <iomanip>

FlowManager::FlowManager(const std::string& db_path) 
    : db_path_(db_path), current_sequence_(0) {
}

FlowManager::~FlowManager() {
    Shutdown();
}

bool FlowManager::Init() {
    leveldb::Options options;
    options.create_if_missing = true;
    options.compression = leveldb::kSnappyCompression;
    options.write_buffer_size = 64 * 1024 * 1024;  // 64MB写缓冲
    options.max_open_files = 1000;
    
    leveldb::DB* db;
    leveldb::Status status = leveldb::DB::Open(options, db_path_, &db);
    
    if (!status.ok()) {
        return false;
    }
    
    db_.reset(db);
    
    // 恢复序列号
    leveldb::Iterator* it = db_->NewIterator(leveldb::ReadOptions());
    it->SeekToLast();
    if (it->Valid()) {
        std::string key = it->key().ToString();
        current_sequence_ = std::stoull(key);
    }
    delete it;
    
    return true;
}

void FlowManager::Shutdown() {
    if (db_) {
        db_.reset();
    }
}

std::string FlowManager::MakeKey(uint64_t sequence) {
    std::stringstream ss;
    ss << std::setw(20) << std::setfill('0') << sequence;
    return ss.str();
}

bool FlowManager::StoreMarketData(uint64_t sequence, const MarketDataMsg& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string key = MakeKey(sequence);
    std::string value(reinterpret_cast<const char*>(&data), sizeof(data));
    
    leveldb::Status status = db_->Put(leveldb::WriteOptions(), key, value);
    return status.ok();
}

bool FlowManager::GetMarketData(uint64_t sequence, MarketDataMsg& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string key = MakeKey(sequence);
    std::string value;
    
    leveldb::Status status = db_->Get(leveldb::ReadOptions(), key, &value);
    if (status.ok() && value.size() == sizeof(MarketDataMsg)) {
        memcpy(&data, value.data(), sizeof(MarketDataMsg));
        return true;
    }
    
    return false;
}

std::vector<MarketDataMsg> FlowManager::GetMarketDataRange(uint64_t start_seq, uint64_t end_seq) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<MarketDataMsg> result;
    
    leveldb::Iterator* it = db_->NewIterator(leveldb::ReadOptions());
    std::string start_key = MakeKey(start_seq);
    
    for (it->Seek(start_key); it->Valid(); it->Next()) {
        std::string key = it->key().ToString();
        uint64_t seq = std::stoull(key);
        
        if (seq > end_seq) {
            break;
        }
        
        std::string value = it->value().ToString();
        if (value.size() == sizeof(MarketDataMsg)) {
            MarketDataMsg data;
            memcpy(&data, value.data(), sizeof(MarketDataMsg));
            result.push_back(data);
        }
    }
    
    delete it;
    return result;
}

// channel.h
#pragma once
#include <memory>
#include <vector>
#include <set>
#include <mutex>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <queue>
#include <condition_variable>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "message_protocol.h"
#include "flow_manager.h"

struct SubscriberSession {
    Subscriber info;
    uint64_t next_send_seq;      // 下一个要发送的序列号
    uint64_t target_seq;          // 目标序列号（最新的）
    bool catching_up;             // 是否正在追赶历史数据
    uint64_t last_send_time;      // 上次发送时间
    uint64_t last_nack_time;      // 上次NACK时间
    int nack_count;               // NACK计数（用于判断网络质量）
    int priority;                 // 优先级（0=普通，1=高，2=VIP）
    std::string group_id;         // 当前所属分组ID
};

// 订阅者分组
struct SubscriberGroup {
    std::string group_id;
    std::set<std::string> subscriber_ids;  // 组内订阅者ID
    uint64_t min_seq;                       // 组内最小序列号（最落后的）
    uint64_t max_seq;                       // 组内最大序列号（最领先的）
    uint64_t current_send_seq;              // 当前发送序列号
    bool is_multicast;                      // 是否使用组播
    uint32_t multicast_ip;                  // 组播地址（如果启用）
    uint16_t multicast_port;                // 组播端口
    uint64_t last_update_time;              // 上次更新时间
};

class Channel {
public:
    Channel(int socket_fd, FlowManager* flow_manager);
    ~Channel();
    
    void Start();
    void Stop();
    
    void AddSubscriber(const Subscriber& sub);
    void RemoveSubscriber(const std::string& sub_id);
    void UpdateSubscriber(const std::string& sub_id, uint64_t last_recv_seq);
    
    // 处理NACK并触发重新分组
    void HandleNack(const std::string& sub_id, uint64_t start_seq, uint64_t end_seq);
    
    // 通知有新数据
    void NotifyNewData(uint64_t sequence);
    
    // 发送数据到特定订阅者或地址
    void SendTo(const std::string& sub_id, const void* data, size_t len);
    void SendToAddress(uint32_t ip, uint16_t port, const void* data, size_t len);
    void SendToGroup(const std::string& group_id, const void* data, size_t len);
    
    // 获取订阅者信息
    Subscriber* GetSubscriber(const std::string& sub_id);
    std::vector<Subscriber> GetAllSubscribers();
    
    // 获取分组信息（用于测试）
    std::vector<SubscriberGroup> GetAllGroups();
    size_t GetGroupCount() const;
    
    // 心跳检查
    void CheckHeartbeat();
    
    // 设置分组阈值
    void SetGroupThreshold(uint64_t threshold) { group_threshold_ = threshold; }
    uint64_t GetGroupThreshold() const { return group_threshold_; }
    
private:
    void SendThread();  // 独立的发送线程
    void GroupingThread();  // 分组管理线程
    
    // 分组策略
    void RegroupSubscribers();  // 重新分组
    void RegroupByProgress();   // 基于进度分组
    void RegroupByNetwork();    // 基于网络位置分组
    void RegroupByPriority();   // 基于优先级分组
    bool ShouldRegroup();        // 判断是否需要重新分组
    
    // 处理分组发送
    void ProcessGroup(SubscriberGroup& group);
    void ProcessIndividualSubscriber(SubscriberSession& session);
    
    // 计算两个订阅者的进度差
    uint64_t CalculateProgressDiff(const SubscriberSession& s1, const SubscriberSession& s2);
    
    // 判断是否可以加入分组
    bool CanJoinGroup(const SubscriberSession& session, const SubscriberGroup& group);
    
    int socket_fd_;
    FlowManager* flow_manager_;
    
    // 订阅者管理
    std::unordered_map<std::string, SubscriberSession> subscribers_;
    
    // 分组管理
    std::unordered_map<std::string, SubscriberGroup> groups_;
    uint64_t group_threshold_;  // 分组阈值（进度差）
    uint64_t last_regroup_time_;  // 上次重新分组时间
    
    mutable std::mutex mutex_;
    mutable std::mutex group_mutex_;
    
    std::atomic<bool> running_;
    std::atomic<bool> need_regroup_;  // 是否需要重新分组
    
    std::thread send_thread_;
    std::thread grouping_thread_;
    std::condition_variable send_cv_;
    std::condition_variable group_cv_;
    
    // 配置参数
    const int MAX_SEND_RATE = 1000;      // 每个订阅者最大发送速率
    const int BATCH_SIZE = 10;           // 批量发送大小
    const int MIN_GROUP_SIZE = 2;        // 最小分组大小
    const int MAX_GROUP_SIZE = 50;       // 最大分组大小
    const uint64_t REGROUP_INTERVAL = 10000000000LL;  // 重新分组间隔（10秒）
    const uint64_t DEFAULT_THRESHOLD = 100;  // 默认分组阈值
};

// channel.cpp
#include "channel.h"
#include <chrono>
#include <cstring>
#include <unistd.h>
#include <iostream>
#include <algorithm>
#include <sstream>

Channel::Channel(int socket_fd, FlowManager* flow_manager) 
    : socket_fd_(socket_fd), flow_manager_(flow_manager), 
      running_(false), need_regroup_(false),
      group_threshold_(DEFAULT_THRESHOLD), last_regroup_time_(0) {
}

Channel::~Channel() {
    Stop();
}

void Channel::Start() {
    running_ = true;
    send_thread_ = std::thread(&Channel::SendThread, this);
    grouping_thread_ = std::thread(&Channel::GroupingThread, this);
}

void Channel::Stop() {
    running_ = false;
    send_cv_.notify_all();
    group_cv_.notify_all();
    
    if (send_thread_.joinable()) {
        send_thread_.join();
    }
    if (grouping_thread_.joinable()) {
        grouping_thread_.join();
    }
}

void Channel::AddSubscriber(const Subscriber& sub) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    SubscriberSession session;
    session.info = sub;
    
    // 设置起始序列号
    if (sub.last_recv_seq > 0) {
        session.next_send_seq = sub.last_recv_seq + 1;
    } else {
        session.next_send_seq = 1;
    }
    
    session.target_seq = flow_manager_->GetCurrentSequence();
    session.catching_up = (session.next_send_seq <= session.target_seq);
    session.last_send_time = 0;
    session.last_nack_time = 0;
    session.nack_count = 0;
    session.priority = 0;  // 默认普通优先级
    session.group_id = "";  // 初始无分组
    
    subscribers_[std::string(sub.id)] = session;
    
    std::cout << "Added subscriber " << sub.id 
              << ", will start from seq " << session.next_send_seq 
              << ", target seq " << session.target_seq << std::endl;
    
    // 触发重新分组
    need_regroup_ = true;
    group_cv_.notify_one();
    send_cv_.notify_one();
}

void Channel::RemoveSubscriber(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = subscribers_.find(sub_id);
    if (it != subscribers_.end()) {
        // 从分组中移除
        if (!it->second.group_id.empty()) {
            std::lock_guard<std::mutex> glock(group_mutex_);
            auto git = groups_.find(it->second.group_id);
            if (git != groups_.end()) {
                git->second.subscriber_ids.erase(sub_id);
                if (git->second.subscriber_ids.empty()) {
                    groups_.erase(git);
                }
            }
        }
        
        subscribers_.erase(it);
        need_regroup_ = true;
        group_cv_.notify_one();
    }
}

void Channel::UpdateSubscriber(const std::string& sub_id, uint64_t last_recv_seq) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscribers_.find(sub_id);
    if (it != subscribers_.end()) {
        it->second.info.last_recv_seq = last_recv_seq;
        it->second.info.last_heartbeat_time = std::chrono::steady_clock::now().time_since_epoch().count();
        
        // 更新发送进度
        if (last_recv_seq >= it->second.next_send_seq - 1) {
            it->second.next_send_seq = last_recv_seq + 1;
        }
    }
}

void Channel::HandleNack(const std::string& sub_id, uint64_t start_seq, uint64_t end_seq) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = subscribers_.find(sub_id);
    if (it != subscribers_.end()) {
        uint64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
        it->second.last_nack_time = now;
        it->second.nack_count++;
        
        // 调整发送序列号
        if (start_seq < it->second.next_send_seq) {
            it->second.next_send_seq = start_seq;
        }
        
        // NACK频繁可能需要重新分组
        if (it->second.nack_count > 5) {
            need_regroup_ = true;
            group_cv_.notify_one();
        }
    }
}

void Channel::NotifyNewData(uint64_t sequence) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 更新所有订阅者的目标序列号
    for (auto& [id, session] : subscribers_) {
        if (session.target_seq < sequence) {
            session.target_seq = sequence;
            if (!session.catching_up && session.next_send_seq <= sequence) {
                session.catching_up = true;
            }
        }
    }
    
    // 唤醒发送线程
    send_cv_.notify_one();
}

void Channel::SendThread() {
    while (running_) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 等待有数据需要发送
        send_cv_.wait_for(lock, std::chrono::milliseconds(10), [this] {
            if (!running_) return true;
            
            for (const auto& [id, session] : subscribers_) {
                if (session.info.active && session.catching_up) {
                    return true;
                }
            }
            return false;
        });
        
        if (!running_) break;
        
        // 处理分组发送
        std::vector<std::string> processed_subs;
        {
            std::lock_guard<std::mutex> glock(group_mutex_);
            for (auto& [group_id, group] : groups_) {
                ProcessGroup(group);
                processed_subs.insert(processed_subs.end(), 
                                    group.subscriber_ids.begin(), 
                                    group.subscriber_ids.end());
            }
        }
        
        // 处理未分组的订阅者
        for (auto& [id, session] : subscribers_) {
            if (session.info.active && 
                std::find(processed_subs.begin(), processed_subs.end(), id) == processed_subs.end()) {
                ProcessIndividualSubscriber(session);
            }
        }
    }
}

void Channel::GroupingThread() {
    while (running_) {
        std::unique_lock<std::mutex> lock(group_mutex_);
        
        // 等待重新分组信号或定期检查
        group_cv_.wait_for(lock, std::chrono::seconds(10), [this] {
            return !running_ || need_regroup_ || ShouldRegroup();
        });
        
        if (!running_) break;
        
        if (need_regroup_ || ShouldRegroup()) {
            RegroupSubscribers();
            need_regroup_ = false;
        }
    }
}

void Channel::RegroupSubscribers() {
    std::cout << "Regrouping subscribers..." << std::endl;
    
    // 清空现有分组
    groups_.clear();
    
    // 重置所有订阅者的分组信息
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, session] : subscribers_) {
            session.group_id = "";
        }
    }
    
    // 执行多种分组策略
    RegroupByPriority();   // 首先按优先级分组
    RegroupByProgress();   // 然后按进度分组
    RegroupByNetwork();    // 最后考虑网络位置
    
    last_regroup_time_ = std::chrono::steady_clock::now().time_since_epoch().count();
    
    std::cout << "Regrouping complete. Total groups: " << groups_.size() << std::endl;
}

void Channel::RegroupByProgress() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 收集未分组的订阅者
    std::vector<std::string> ungrouped;
    for (const auto& [id, session] : subscribers_) {
        if (session.group_id.empty() && session.info.active) {
            ungrouped.push_back(id);
        }
    }
    
    // 按进度排序
    std::sort(ungrouped.begin(), ungrouped.end(), [this](const std::string& a, const std::string& b) {
        return subscribers_[a].next_send_seq < subscribers_[b].next_send_seq;
    });
    
    // 创建进度分组
    std::string current_group_id;
    SubscriberGroup* current_group = nullptr;
    
    for (const auto& sub_id : ungrouped) {
        auto& session = subscribers_[sub_id];
        
        if (!current_group || !CanJoinGroup(session, *current_group)) {
            // 创建新分组
            std::stringstream ss;
            ss << "progress_" << groups_.size();
            current_group_id = ss.str();
            
            SubscriberGroup new_group;
            new_group.group_id = current_group_id;
            new_group.min_seq = session.next_send_seq;
            new_group.max_seq = session.next_send_seq;
            new_group.current_send_seq = session.next_send_seq;
            new_group.is_multicast = false;
            new_group.last_update_time = std::chrono::steady_clock::now().time_since_epoch().count();
            
            groups_[current_group_id] = new_group;
            current_group = &groups_[current_group_id];
        }
        
        // 加入分组
        session.group_id = current_group_id;
        current_group->subscriber_ids.insert(sub_id);
        current_group->min_seq = std::min(current_group->min_seq, session.next_send_seq);
        current_group->max_seq = std::max(current_group->max_seq, session.next_send_seq);
        
        // 检查分组大小
        if (current_group->subscriber_ids.size() >= MAX_GROUP_SIZE) {
            current_group = nullptr;
        }
    }
}

void Channel::RegroupByNetwork() {
    // 基于网络位置的分组（如同一子网）
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::unordered_map<uint32_t, std::vector<std::string>> subnet_map;
    
    // 按子网分组（使用/24掩码）
    for (auto& [id, session] : subscribers_) {
        if (session.group_id.empty() && session.info.active) {
            uint32_t subnet = session.info.ip & 0xFFFFFF00;
            subnet_map[subnet].push_back(id);
        }
    }
    
    // 为同一子网的订阅者创建分组
    for (const auto& [subnet, sub_ids] : subnet_map) {
        if (sub_ids.size() >= MIN_GROUP_SIZE) {
            std::stringstream ss;
            ss << "network_" << std::hex << subnet;
            std::string group_id = ss.str();
            
            SubscriberGroup new_group;
            new_group.group_id = group_id;
            new_group.is_multicast = true;  // 同一子网可以使用组播
            new_group.multicast_ip = (239 << 24) | (subnet & 0x00FFFFFF);  // 组播地址
            new_group.multicast_port = 5000 + (subnet & 0xFF);
            
            uint64_t min_seq = UINT64_MAX;
            uint64_t max_seq = 0;
            
            for (const auto& sub_id : sub_ids) {
                auto& session = subscribers_[sub_id];
                session.group_id = group_id;
                new_group.subscriber_ids.insert(sub_id);
                min_seq = std::min(min_seq, session.next_send_seq);
                max_seq = std::max(max_seq, session.next_send_seq);
            }
            
            new_group.min_seq = min_seq;
            new_group.max_seq = max_seq;
            new_group.current_send_seq = min_seq;
            new_group.last_update_time = std::chrono::steady_clock::now().time_since_epoch().count();
            
            groups_[group_id] = new_group;
        }
    }
}

void Channel::RegroupByPriority() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // VIP用户单独分组
    std::vector<std::string> vip_subs;
    std::vector<std::string> high_priority_subs;
    
    for (auto& [id, session] : subscribers_) {
        if (session.info.active && session.group_id.empty()) {
            if (session.priority == 2) {
                vip_subs.push_back(id);
            } else if (session.priority == 1) {
                high_priority_subs.push_back(id);
            }
        }
    }
    
    // VIP分组（每个VIP单独一组，保证最佳服务）
    for (const auto& sub_id : vip_subs) {
        std::stringstream ss;
        ss << "vip_" << sub_id;
        std::string group_id = ss.str();
        
        auto& session = subscribers_[sub_id];
        session.group_id = group_id;
        
        SubscriberGroup new_group;
        new_group.group_id = group_id;
        new_group.subscriber_ids.insert(sub_id);
        new_group.min_seq = session.next_send_seq;
        new_group.max_seq = session.next_send_seq;
        new_group.current_send_seq = session.next_send_seq;
        new_group.is_multicast = false;
        new_group.last_update_time = std::chrono::steady_clock::now().time_since_epoch().count();
        
        groups_[group_id] = new_group;
    }
    
    // 高优先级分组
    if (!high_priority_subs.empty()) {
        std::string group_id = "high_priority";
        
        SubscriberGroup new_group;
        new_group.group_id = group_id;
        new_group.is_multicast = false;
        
        uint64_t min_seq = UINT64_MAX;
        uint64_t max_seq = 0;
        
        for (const auto& sub_id : high_priority_subs) {
            auto& session = subscribers_[sub_id];
            session.group_id = group_id;
            new_group.subscriber_ids.insert(sub_id);
            min_seq = std::min(min_seq, session.next_send_seq);
            max_seq = std::max(max_seq, session.next_send_seq);
        }
        
        new_group.min_seq = min_seq;
        new_group.max_seq = max_seq;
        new_group.current_send_seq = min_seq;
        new_group.last_update_time = std::chrono::steady_clock::now().time_since_epoch().count();
        
        groups_[group_id] = new_group;
    }
}

bool Channel::ShouldRegroup() {
    uint64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
    
    // 定期重新分组
    if (now - last_regroup_time_ > REGROUP_INTERVAL) {
        return true;
    }
    
    // 检查是否有分组进度差异过大
    std::lock_guard<std::mutex> glock(group_mutex_);
    for (const auto& [group_id, group] : groups_) {
        if (group.max_seq - group.min_seq > group_threshold_ * 2) {
            return true;
        }
    }
    
    return false;
}

void Channel::ProcessGroup(SubscriberGroup& group) {
    if (group.subscriber_ids.empty()) return;
    
    // 更新组内最小序列号
    uint64_t min_seq = UINT64_MAX;
    uint64_t max_seq = 0;
    bool all_active = true;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& sub_id : group.subscriber_ids) {
            auto it = subscribers_.find(sub_id);
            if (it != subscribers_.end()) {
                min_seq = std::min(min_seq, it->second.next_send_seq);
                max_seq = std::max(max_seq, it->second.target_seq);
                all_active = all_active && it->second.info.active;
            }
        }
    }
    
    if (!all_active) return;
    
    group.min_seq = min_seq;
    group.current_send_seq = min_seq;
    
    // 批量发送数据
    int sent_count = 0;
    while (sent_count < BATCH_SIZE && group.current_send_seq <= max_seq) {
        MarketDataMsg data;
        if (flow_manager_->GetMarketData(group.current_send_seq, data)) {
            if (group.is_multicast) {
                // 使用组播发送
                struct sockaddr_in mcast_addr;
                mcast_addr.sin_family = AF_INET;
                mcast_addr.sin_addr.s_addr = htonl(group.multicast_ip);
                mcast_addr.sin_port = htons(group.multicast_port);
                
                sendto(socket_fd_, &data, sizeof(data), 0,
                      (struct sockaddr*)&mcast_addr, sizeof(mcast_addr));
            } else {
                // 单播到每个成员
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& sub_id : group.subscriber_ids) {
                    auto it = subscribers_.find(sub_id);
                    if (it != subscribers_.end() && 
                        it->second.next_send_seq <= group.current_send_seq) {
                        SendTo(sub_id, &data, sizeof(data));
                    }
                }
            }
            
            sent_count++;
        }
        
        group.current_send_seq++;
        
        // 更新各订阅者的进度
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& sub_id : group.subscriber_ids) {
                auto it = subscribers_.find(sub_id);
                if (it != subscribers_.end() && 
                    it->second.next_send_seq <= group.current_send_seq) {
                    it->second.next_send_seq = group.current_send_seq;
                    it->second.last_send_time = std::chrono::steady_clock::now().time_since_epoch().count();
                    
                    // 检查是否追赶完成
                    if (it->second.next_send_seq > it->second.target_seq) {
                        it->second.catching_up = false;
                    }
                }
            }
        }
        
        usleep(1000);  // 速率控制
    }
}

void Channel::ProcessIndividualSubscriber(SubscriberSession& session) {
    if (!session.catching_up || session.next_send_seq > session.target_seq) {
        session.catching_up = false;
        return;
    }
    
    // 速率控制
    uint64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
    if (session.last_send_time > 0) {
        uint64_t elapsed_us = now - session.last_send_time;
        if (elapsed_us < 1000) {
            return;
        }
    }
    
    // 批量发送数据
    int sent_count = 0;
    uint64_t current_seq = session.next_send_seq;
    
    while (sent_count < BATCH_SIZE && current_seq <= session.target_seq) {
        MarketDataMsg data;
        if (flow_manager_->GetMarketData(current_seq, data)) {
            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = session.info.ip;
            addr.sin_port = htons(session.info.port);
            
            int n = sendto(socket_fd_, &data, sizeof(data), 0, 
                          (struct sockaddr*)&addr, sizeof(addr));
            
            if (n > 0) {
                sent_count++;
                session.next_send_seq = current_seq + 1;
                session.last_send_time = now;
            } else {
                break;
            }
        } else {
            session.next_send_seq = current_seq + 1;
        }
        
        current_seq++;
    }
    
    // 检查是否追赶完成
    if (session.next_send_seq > session.target_seq) {
        session.catching_up = false;
    }
}

bool Channel::CanJoinGroup(const SubscriberSession& session, const SubscriberGroup& group) {
    // 检查分组大小
    if (group.subscriber_ids.size() >= MAX_GROUP_SIZE) {
        return false;
    }
    
    // 检查进度差
    uint64_t seq_diff = 0;
    if (session.next_send_seq > group.min_seq) {
        seq_diff = session.next_send_seq - group.min_seq;
    } else {
        seq_diff = group.max_seq - session.next_send_seq;
    }
    
    return seq_diff <= group_threshold_;
}

uint64_t Channel::CalculateProgressDiff(const SubscriberSession& s1, const SubscriberSession& s2) {
    if (s1.next_send_seq > s2.next_send_seq) {
        return s1.next_send_seq - s2.next_send_seq;
    }
    return s2.next_send_seq - s1.next_send_seq;
}

void Channel::SendTo(const std::string& sub_id, const void* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = subscribers_.find(sub_id);
    if (it != subscribers_.end() && it->second.info.active) {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = it->second.info.ip;
        addr.sin_port = htons(it->second.info.port);
        
        sendto(socket_fd_, data, len, 0, 
               (struct sockaddr*)&addr, sizeof(addr));
    }
}

void Channel::SendToAddress(uint32_t ip, uint16_t port, const void* data, size_t len) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip;
    addr.sin_port = htons(port);
    
    sendto(socket_fd_, data, len, 0, 
           (struct sockaddr*)&addr, sizeof(addr));
}

void Channel::SendToGroup(const std::string& group_id, const void* data, size_t len) {
    std::lock_guard<std::mutex> glock(group_mutex_);
    
    auto it = groups_.find(group_id);
    if (it != groups_.end()) {
        if (it->second.is_multicast) {
            struct sockaddr_in mcast_addr;
            mcast_addr.sin_family = AF_INET;
            mcast_addr.sin_addr.s_addr = htonl(it->second.multicast_ip);
            mcast_addr.sin_port = htons(it->second.multicast_port);
            
            sendto(socket_fd_, data, len, 0,
                  (struct sockaddr*)&mcast_addr, sizeof(mcast_addr));
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& sub_id : it->second.subscriber_ids) {
                SendTo(sub_id, data, len);
            }
        }
    }
}

Subscriber* Channel::GetSubscriber(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscribers_.find(sub_id);
    return it != subscribers_.end() ? &it->second.info : nullptr;
}

std::vector<Subscriber> Channel::GetAllSubscribers() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Subscriber> result;
    for (const auto& [id, session] : subscribers_) {
        result.push_back(session.info);
    }
    return result;
}

std::vector<SubscriberGroup> Channel::GetAllGroups() {
    std::lock_guard<std::mutex> lock(group_mutex_);
    std::vector<SubscriberGroup> result;
    for (const auto& [id, group] : groups_) {
        result.push_back(group);
    }
    return result;
}

size_t Channel::GetGroupCount() const {
    std::lock_guard<std::mutex> lock(group_mutex_);
    return groups_.size();
}

void Channel::CheckHeartbeat() {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
    const uint64_t timeout = 30 * 1000000000LL; // 30秒
    
    for (auto& [id, session] : subscribers_) {
        if (now - session.info.last_heartbeat_time > timeout) {
            session.info.active = false;
            std::cout << "Subscriber " << id << " timeout" << std::endl;
            
            // 触发重新分组
            need_regroup_ = true;
        }
    }
    
    if (need_regroup_) {
        group_cv_.notify_one();
    }
}

// channel_manager.h
#pragma once
#include <memory>
#include <unordered_map>
#include <mutex>
#include "channel.h"

class ChannelManager {
public:
    ChannelManager();
    ~ChannelManager();
    
    void AddChannel(const std::string& channel_id, std::shared_ptr<Channel> channel);
    void RemoveChannel(const std::string& channel_id);
    std::shared_ptr<Channel> GetChannel(const std::string& channel_id);
    
    // 通知所有频道有新数据
    void NotifyAllNewData(uint64_t sequence);
    
    // 心跳检查所有频道
    void CheckAllHeartbeats();
    
private:
    std::unordered_map<std::string, std::shared_ptr<Channel>> channels_;
    mutable std::mutex mutex_;
};

// channel_manager.cpp
#include "channel_manager.h"

ChannelManager::ChannelManager() {
}

ChannelManager::~ChannelManager() {
}

void ChannelManager::AddChannel(const std::string& channel_id, std::shared_ptr<Channel> channel) {
    std::lock_guard<std::mutex> lock(mutex_);
    channels_[channel_id] = channel;
    channel->Start();  // 启动channel的发送线程
}

void ChannelManager::RemoveChannel(const std::string& channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = channels_.find(channel_id);
    if (it != channels_.end()) {
        it->second->Stop();  // 停止channel的发送线程
        channels_.erase(it);
    }
}

std::shared_ptr<Channel> ChannelManager::GetChannel(const std::string& channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = channels_.find(channel_id);
    return it != channels_.end() ? it->second : nullptr;
}

void ChannelManager::NotifyAllNewData(uint64_t sequence) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, channel] : channels_) {
        channel->NotifyNewData(sequence);
    }
}

void ChannelManager::CheckAllHeartbeats() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, channel] : channels_) {
        channel->CheckHeartbeat();
    }
}

// md_udp_puber.h
#pragma once
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include "message_protocol.h"
#include "flow_manager.h"
#include "channel_manager.h"

class MdUdpPuber {
public:
    MdUdpPuber(const std::string& bind_ip, uint16_t bind_port, const std::string& db_path);
    ~MdUdpPuber();
    
    bool Start();
    void Stop();
    
    // 发布市场数据
    void PublishMarketData(const MarketDataMsg& data);
    
    // 处理订阅者请求
    void ProcessSubscriberRequest();
    
private:
    void RecvThread();
    void HeartbeatThread();
    void HandleLogin(const LoginMsg& msg, uint32_t ip, uint16_t port);
    void HandleNack(const NackMsg& msg, uint32_t ip, uint16_t port);
    void HandleHeartbeat(const HeartbeatMsg& msg, uint32_t ip, uint16_t port);
    
    std::string bind_ip_;
    uint16_t bind_port_;
    int socket_fd_;
    
    std::unique_ptr<FlowManager> flow_manager_;
    std::unique_ptr<ChannelManager> channel_manager_;
    
    std::atomic<bool> running_;
    std::thread recv_thread_;
    std::thread heartbeat_thread_;
};

// md_udp_puber.cpp
#include "md_udp_puber.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>

MdUdpPuber::MdUdpPuber(const std::string& bind_ip, uint16_t bind_port, const std::string& db_path)
    : bind_ip_(bind_ip), bind_port_(bind_port), socket_fd_(-1), running_(false) {
    flow_manager_ = std::make_unique<FlowManager>(db_path);
    channel_manager_ = std::make_unique<ChannelManager>();
}

MdUdpPuber::~MdUdpPuber() {
    Stop();
}

bool MdUdpPuber::Start() {
    // 初始化FlowManager
    if (!flow_manager_->Init()) {
        std::cerr << "Failed to init flow manager" << std::endl;
        return false;
    }
    
    // 创建UDP socket
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }
    
    // 设置非阻塞
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
    
    // 设置socket缓冲区大小
    int send_buf_size = 4 * 1024 * 1024;  // 4MB
    setsockopt(socket_fd_, SOL_SOCKET, SO_SNDBUF, &send_buf_size, sizeof(send_buf_size));
    
    // 绑定地址
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(bind_ip_.c_str());
    addr.sin_port = htons(bind_port_);
    
    if (bind(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind socket" << std::endl;
        close(socket_fd_);
        return false;
    }
    
    // 创建默认channel，传入flow_manager
    auto default_channel = std::make_shared<Channel>(socket_fd_, flow_manager_.get());
    channel_manager_->AddChannel("default", default_channel);
    
    running_ = true;
    recv_thread_ = std::thread(&MdUdpPuber::RecvThread, this);
    heartbeat_thread_ = std::thread(&MdUdpPuber::HeartbeatThread, this);
    
    std::cout << "Publisher started on " << bind_ip_ << ":" << bind_port_ << std::endl;
    
    return true;
}

void MdUdpPuber::Stop() {
    running_ = false;
    
    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }
    
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    
    flow_manager_->Shutdown();
}

void MdUdpPuber::PublishMarketData(const MarketDataMsg& data) {
    MarketDataMsg msg = data;
    msg.header.msg_type = MSG_MARKET_DATA;
    msg.header.msg_len = sizeof(MarketDataMsg);
    msg.header.sequence = flow_manager_->GetNextSequence();
    msg.header.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    
    // 存储到LevelDB
    flow_manager_->StoreMarketData(msg.header.sequence, msg);
    
    // 通知所有channel有新数据（不直接发送，由channel管理发送进度）
    channel_manager_->NotifyAllNewData(msg.header.sequence);
    
    std::cout << "Published data seq=" << msg.header.sequence 
              << " symbol=" << msg.symbol << std::endl;
}

void MdUdpPuber::RecvThread() {
    char buffer[4096];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    while (running_) {
        int n = recvfrom(socket_fd_, buffer, sizeof(buffer), 0,
                        (struct sockaddr*)&client_addr, &addr_len);
        
        if (n <= 0) {
            usleep(1000); // 1ms
            continue;
        }
        
        if (n < sizeof(MessageHeader)) {
            continue;
        }
        
        MessageHeader* header = (MessageHeader*)buffer;
        
        switch (header->msg_type) {
            case MSG_LOGIN:
                if (n >= sizeof(LoginMsg)) {
                    HandleLogin(*(LoginMsg*)buffer, client_addr.sin_addr.s_addr, ntohs(client_addr.sin_port));
                }
                break;
                
            case MSG_NACK:
                if (n >= sizeof(NackMsg)) {
                    HandleNack(*(NackMsg*)buffer, client_addr.sin_addr.s_addr, ntohs(client_addr.sin_port));
                }
                break;
                
            case MSG_HEARTBEAT:
                if (n >= sizeof(HeartbeatMsg)) {
                    HandleHeartbeat(*(HeartbeatMsg*)buffer, client_addr.sin_addr.s_addr, ntohs(client_addr.sin_port));
                }
                break;
                
            default:
                break;
        }
    }
}

void MdUdpPuber::HeartbeatThread() {
    while (running_) {
        // 发送心跳到所有活跃的订阅者
        auto channel = channel_manager_->GetChannel("default");
        if (channel) {
            HeartbeatMsg hb;
            hb.header.msg_type = MSG_HEARTBEAT;
            hb.header.msg_len = sizeof(HeartbeatMsg);
            hb.header.sequence = 0;
            hb.header.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
            hb.last_recv_seq = flow_manager_->GetCurrentSequence();
            
            auto subs = channel->GetAllSubscribers();
            for (const auto& sub : subs) {
                if (sub.active) {
                    channel->SendToAddress(sub.ip, sub.port, &hb, sizeof(hb));
                }
            }
        }
        
        // 检查订阅者心跳
        channel_manager_->CheckAllHeartbeats();
        
        sleep(5); // 5秒一次心跳
    }
}

void MdUdpPuber::HandleLogin(const LoginMsg& msg, uint32_t ip, uint16_t port) {
    auto channel = channel_manager_->GetChannel("default");
    if (!channel) {
        return;
    }
    
    std::cout << "Login request from " << msg.subscriber_id 
              << ", last_recv_seq=" << msg.last_recv_seq << std::endl;
    
    // 创建订阅者
    Subscriber sub;
    strncpy(sub.id, msg.subscriber_id, sizeof(sub.id));
    sub.ip = ip;
    sub.port = port;
    sub.last_recv_seq = msg.last_recv_seq;
    sub.last_heartbeat_time = std::chrono::steady_clock::now().time_since_epoch().count();
    sub.active = true;
    
    // 添加订阅者（Channel会自动处理从哪里开始发送）
    channel->AddSubscriber(sub);
    
    // 发送登录响应
    LoginRespMsg resp;
    resp.header.msg_type = MSG_LOGIN_RESP;
    resp.header.msg_len = sizeof(LoginRespMsg);
    resp.header.sequence = 0;
    resp.header.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    resp.success = 1;
    resp.current_seq = flow_manager_->GetCurrentSequence();
    strcpy(resp.message, "Login successful");
    
    channel->SendToAddress(ip, port, &resp, sizeof(resp));
    
    // 数据发送由Channel的SendThread自动处理
}

void MdUdpPuber::HandleNack(const NackMsg& msg, uint32_t ip, uint16_t port) {
    auto channel = channel_manager_->GetChannel("default");
    if (!channel) {
        return;
    }
    
    std::cout << "NACK received for seq " << msg.start_seq 
              << " to " << msg.end_seq << std::endl;
    
    // 查找对应的订阅者
    auto subs = channel->GetAllSubscribers();
    std::string sub_id;
    for (const auto& sub : subs) {
        if (sub.ip == ip && sub.port == port) {
            sub_id = sub.id;
            break;
        }
    }
    
    if (!sub_id.empty()) {
        // 通知Channel处理NACK（可能触发重新分组）
        channel->HandleNack(sub_id, msg.start_seq, msg.end_seq);
    }
    
    // 重传请求的数据
    auto data_range = flow_manager_->GetMarketDataRange(msg.start_seq, msg.end_seq);
    for (const auto& data : data_range) {
        channel->SendToAddress(ip, port, &data, sizeof(data));
        usleep(100); // 避免发送过快
    }
}

void MdUdpPuber::HandleHeartbeat(const HeartbeatMsg& msg, uint32_t ip, uint16_t port) {
    // 更新订阅者心跳时间和接收进度
    auto channel = channel_manager_->GetChannel("default");
    if (!channel) {
        return;
    }
    
    auto subs = channel->GetAllSubscribers();
    for (const auto& sub : subs) {
        if (sub.ip == ip && sub.port == port) {
            channel->UpdateSubscriber(sub.id, msg.last_recv_seq);
            break;
        }
    }
}

// md_udp_suber.h
#pragma once
#include <functional>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include "message_protocol.h"

using MarketDataCallback = std::function<void(const MarketDataMsg&)>;

class MdUdpSuber {
public:
    MdUdpSuber(const std::string& subscriber_id, const std::string& server_ip, uint16_t server_port);
    ~MdUdpSuber();
    
    // 注册市场数据回调
    void RegisterCallback(MarketDataCallback callback);
    
    // 启动订阅者
    bool Start();
    void Stop();
    
    // 获取状态
    bool IsConnected() const { return connected_; }
    uint64_t GetLastRecvSeq() const { return last_recv_seq_; }
    
private:
    void RecvThread();
    void HeartbeatThread();
    void ProcessThread();
    
    bool Login();
    void SendNack(uint64_t start_seq, uint64_t end_seq);
    void SendHeartbeat();
    
    std::string subscriber_id_;
    std::string server_ip_;
    uint16_t server_port_;
    int socket_fd_;
    
    std::atomic<bool> running_;
    std::atomic<bool> connected_;
    std::atomic<uint64_t> last_recv_seq_;
    std::atomic<uint64_t> expected_seq_;
    
    MarketDataCallback callback_;
    
    std::thread recv_thread_;
    std::thread heartbeat_thread_;
    std::thread process_thread_;
    
    // 消息队列
    std::queue<MarketDataMsg> msg_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
};

// md_udp_suber.cpp
#include "md_udp_suber.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

MdUdpSuber::MdUdpSuber(const std::string& subscriber_id, const std::string& server_ip, uint16_t server_port)
    : subscriber_id_(subscriber_id), server_ip_(server_ip), server_port_(server_port),
      socket_fd_(-1), running_(false), connected_(false), last_recv_seq_(0), expected_seq_(1) {
}

MdUdpSuber::~MdUdpSuber() {
    Stop();
}

void MdUdpSuber::RegisterCallback(MarketDataCallback callback) {
    callback_ = callback;
}

bool MdUdpSuber::Start() {
    // 创建UDP socket
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }
    
    // 设置非阻塞
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
    
    // 设置接收超时
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    running_ = true;
    
    // 启动线程
    recv_thread_ = std::thread(&MdUdpSuber::RecvThread, this);
    heartbeat_thread_ = std::thread(&MdUdpSuber::HeartbeatThread, this);
    process_thread_ = std::thread(&MdUdpSuber::ProcessThread, this);
    
    // 登录
    if (!Login()) {
        Stop();
        return false;
    }
    
    return true;
}

void MdUdpSuber::Stop() {
    running_ = false;
    connected_ = false;
    
    queue_cv_.notify_all();
    
    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }
    
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    
    if (process_thread_.joinable()) {
        process_thread_.join();
    }
    
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

bool MdUdpSuber::Login() {
    LoginMsg login;
    login.header.msg_type = MSG_LOGIN;
    login.header.msg_len = sizeof(LoginMsg);
    login.header.sequence = 0;
    login.header.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    strncpy(login.subscriber_id, subscriber_id_.c_str(), sizeof(login.subscriber_id));
    login.last_recv_seq = last_recv_seq_;
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip_.c_str());
    server_addr.sin_port = htons(server_port_);
    
    // 发送登录请求
    sendto(socket_fd_, &login, sizeof(login), 0,
           (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    // 等待登录响应
    char buffer[4096];
    struct sockaddr_in from_addr;
    socklen_t addr_len = sizeof(from_addr);
    
    for (int i = 0; i < 10; i++) { // 重试10次
        int n = recvfrom(socket_fd_, buffer, sizeof(buffer), 0,
                        (struct sockaddr*)&from_addr, &addr_len);
        
        if (n >= sizeof(LoginRespMsg)) {
            MessageHeader* header = (MessageHeader*)buffer;
            if (header->msg_type == MSG_LOGIN_RESP) {
                LoginRespMsg* resp = (LoginRespMsg*)buffer;
                if (resp->success) {
                    connected_ = true;
                    expected_seq_ = last_recv_seq_ + 1;
                    std::cout << "Login successful: " << resp->message << std::endl;
                    return true;
                }
            }
        }
        
        usleep(100000); // 100ms
    }
    
    return false;
}

void MdUdpSuber::RecvThread() {
    char buffer[4096];
    struct sockaddr_in from_addr;
    socklen_t addr_len = sizeof(from_addr);
    
    while (running_) {
        int n = recvfrom(socket_fd_, buffer, sizeof(buffer), 0,
                        (struct sockaddr*)&from_addr, &addr_len);
        
        if (n <= 0) {
            continue;
        }
        
        if (n < sizeof(MessageHeader)) {
            continue;
        }
        
        MessageHeader* header = (MessageHeader*)buffer;
        
        switch (header->msg_type) {
            case MSG_MARKET_DATA:
                if (n >= sizeof(MarketDataMsg)) {
                    MarketDataMsg* msg = (MarketDataMsg*)buffer;
                    
                    // 检查序列号
                    if (msg->header.sequence == expected_seq_) {
                        // 序列号正确
                        {
                            std::lock_guard<std::mutex> lock(queue_mutex_);
                            msg_queue_.push(*msg);
                        }
                        queue_cv_.notify_one();
                        
                        last_recv_seq_ = msg->header.sequence;
                        expected_seq_ = last_recv_seq_ + 1;
                    } else if (msg->header.sequence > expected_seq_) {
                        // 有数据丢失，发送NACK
                        SendNack(expected_seq_, msg->header.sequence - 1);
                        
                        // 保存当前消息
                        {
                            std::lock_guard<std::mutex> lock(queue_mutex_);
                            msg_queue_.push(*msg);
                        }
                        queue_cv_.notify_one();
                        
                        last_recv_seq_ = msg->header.sequence;
                        expected_seq_ = last_recv_seq_ + 1;
                    }
                    // 忽略重复或过期的消息
                }
                break;
                
            case MSG_HEARTBEAT:
                // 收到心跳，更新连接状态
                connected_ = true;
                break;
                
            default:
                break;
        }
    }
}

void MdUdpSuber::HeartbeatThread() {
    while (running_) {
        if (connected_) {
            SendHeartbeat();
        } else {
            // 尝试重新登录
            Login();
        }
        
        sleep(5); // 5秒一次心跳
    }
}

void MdUdpSuber::ProcessThread() {
    while (running_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this] { return !msg_queue_.empty() || !running_; });
        
        while (!msg_queue_.empty()) {
            MarketDataMsg msg = msg_queue_.front();
            msg_queue_.pop();
            lock.unlock();
            
            // 调用回调函数
            if (callback_) {
                callback_(msg);
            }
            
            lock.lock();
        }
    }
}

void MdUdpSuber::SendNack(uint64_t start_seq, uint64_t end_seq) {
    NackMsg nack;
    nack.header.msg_type = MSG_NACK;
    nack.header.msg_len = sizeof(NackMsg);
    nack.header.sequence = 0;
    nack.header.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    nack.start_seq = start_seq;
    nack.end_seq = end_seq;
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip_.c_str());
    server_addr.sin_port = htons(server_port_);
    
    sendto(socket_fd_, &nack, sizeof(nack), 0,
           (struct sockaddr*)&server_addr, sizeof(server_addr));
}

void MdUdpSuber::SendHeartbeat() {
    HeartbeatMsg hb;
    hb.header.msg_type = MSG_HEARTBEAT;
    hb.header.msg_len = sizeof(HeartbeatMsg);
    hb.header.sequence = 0;
    hb.header.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    hb.last_recv_seq = last_recv_seq_;
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip_.c_str());
    server_addr.sin_port = htons(server_port_);
    
    sendto(socket_fd_, &hb, sizeof(hb), 0,
           (struct sockaddr*)&server_addr, sizeof(server_addr));
}

// example_usage.cpp
#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>
#include <atomic>
#include <cstring>
#include "md_udp_puber.h"
#include "md_udp_suber.h"

std::atomic<bool> g_running(true);

void signal_handler(int sig) {
    g_running = false;
}

// Publisher示例
void publisher_example() {
    MdUdpPuber puber("0.0.0.0", 8888, "./market_data.db");
    
    if (!puber.Start()) {
        std::cerr << "Failed to start publisher" << std::endl;
        return;
    }
    
    std::cout << "Publisher started on port 8888" << std::endl;
    
    // 模拟发布市场数据
    uint64_t count = 0;
    while (g_running) {
        MarketDataMsg data;
        strcpy(data.symbol, "BTCUSDT");
        data.bid_price = 40000.0 + (count % 1000);
        data.ask_price = data.bid_price + 10;
        data.bid_volume = 100;
        data.ask_volume = 150;
        data.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        
        puber.PublishMarketData(data);
        
        count++;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    puber.Stop();
}

// Subscriber示例
void subscriber_example() {
    MdUdpSuber suber("subscriber_001", "127.0.0.1", 8888);
    
    // 注册回调函数
    suber.RegisterCallback([](const MarketDataMsg& data) {
        std::cout << "Received: " << data.symbol 
                  << " Bid: " << data.bid_price 
                  << " Ask: " << data.ask_price 
                  << " Seq: " << data.header.sequence << std::endl;
    });
    
    if (!suber.Start()) {
        std::cerr << "Failed to start subscriber" << std::endl;
        return;
    }
    
    std::cout << "Subscriber started" << std::endl;
    
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    suber.Stop();
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " [pub|sub]" << std::endl;
        return 1;
    }
    
    if (strcmp(argv[1], "pub") == 0) {
        publisher_example();
    } else if (strcmp(argv[1], "sub") == 0) {
        subscriber_example();
    } else {
        std::cout << "Invalid option. Use 'pub' or 'sub'" << std::endl;
        return 1;
    }
    
    return 0;
}

// Makefile
CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2 -pthread
LDFLAGS = -lleveldb -lpthread

SRCS = flow_manager.cpp channel.cpp channel_manager.cpp md_udp_puber.cpp md_udp_suber.cpp example_usage.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = mdudp

# Test related variables
TEST_DIR = tests
TEST_SRCS = $(TEST_DIR)/unit_tests.cpp $(TEST_DIR)/integration_tests.cpp \
            $(TEST_DIR)/performance_tests.cpp $(TEST_DIR)/main.cpp
TEST_OBJS = $(TEST_SRCS:.cpp=.o)
TEST_TARGET = test_runner

# Library objects for tests (exclude example_usage.cpp)
LIB_SRCS = flow_manager.cpp channel.cpp channel_manager.cpp md_udp_puber.cpp md_udp_suber.cpp
LIB_OBJS_FOR_TEST = $(LIB_SRCS:.cpp=_test.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Test compilation rules
test: $(TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJS) $(LIB_OBJS_FOR_TEST)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_DIR)/%.o: $(TEST_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -I. -c $< -o $@

%_test.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run_test: test
	./$(TEST_TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
	rm -f $(TEST_OBJS) $(LIB_OBJS_FOR_TEST) $(TEST_TARGET)
	rm -rf test_*_db market_data.db

.PHONY: all clean test run_test