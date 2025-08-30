// common/net_message.h
#pragma once

#include <cstdint>
#include <cstring>
#include <arpa/inet.h>

namespace reliable_udp {

// 消息类型
enum MessageType : uint8_t {
    MSG_UNKNOWN = 0,
    MSG_MARKET_DATA = 1,    // 市场数据
    MSG_HEARTBEAT = 2,      // 心跳
    MSG_LOGIN_REQ = 3,      // 登录请求
    MSG_LOGIN_RSP = 4,      // 登录响应
    MSG_NACK = 5,           // 负确认请求
    MSG_LOGOUT = 6          // 登出请求
};

// 所有网络消息的基类
struct NetMessageHeader {
    MessageType msg_type;    // 消息类型
    uint8_t channel_id;      // 通道ID
    uint16_t msg_length;     // 消息总长度
    uint32_t seq_num;        // 序列号
    uint32_t timestamp;      // 时间戳(毫秒)

    // 主机序转网络序
    void hton() {
        msg_length = htons(msg_length);
        seq_num = htonl(seq_num);
        timestamp = htonl(timestamp);
    }

    // 网络序转主机序
    void ntoh() {
        msg_length = ntohs(msg_length);
        seq_num = ntohl(seq_num);
        timestamp = ntohl(timestamp);
    }
};

// 网络消息基类
struct NetMessage {
    NetMessageHeader header;
};

// 市场数据消息
struct MarketDataMessage : public NetMessage {
    char symbol[32];          // 证券代码
    double price;             // 价格
    uint64_t volume;          // 成交量
    uint32_t update_time;     // 行情更新时间
    uint8_t type;             // 数据类型
    
    // 主机序转网络序
    void hton() {
        header.hton();
        // 注意: 这里需要处理double和uint64_t的字节序转换
        // 实际实现应根据具体需要处理
        volume = htobe64(volume);
        update_time = htonl(update_time);
    }
    
    // 网络序转主机序
    void ntoh() {
        header.ntoh();
        volume = be64toh(volume);
        update_time = ntohl(update_time);
    }
};

// 心跳消息
struct HeartbeatMessage : public NetMessage {
    uint32_t last_seq;        // 最后的序列号
    
    void hton() {
        header.hton();
        last_seq = htonl(last_seq);
    }
    
    void ntoh() {
        header.ntoh();
        last_seq = ntohl(last_seq);
    }
};

// 登录请求
struct LoginRequestMessage : public NetMessage {
    uint32_t client_id;        // 客户端ID
    uint32_t last_seq;         // 最后收到的序列号
    char client_name[32];      // 客户端名称
    
    void hton() {
        header.hton();
        client_id = htonl(client_id);
        last_seq = htonl(last_seq);
    }
    
    void ntoh() {
        header.ntoh();
        client_id = ntohl(client_id);
        last_seq = ntohl(last_seq);
    }
};

// 登录响应
struct LoginResponseMessage : public NetMessage {
    uint32_t session_id;       // 会话ID
    uint32_t start_seq;        // 起始序列号
    uint8_t result;            // 登录结果: 0=成功, 非0=失败
    
    void hton() {
        header.hton();
        session_id = htonl(session_id);
        start_seq = htonl(start_seq);
    }
    
    void ntoh() {
        header.ntoh();
        session_id = ntohl(session_id);
        start_seq = ntohl(start_seq);
    }
};

// NACK消息 - 负确认请求重传
struct NackMessage : public NetMessage {
    uint32_t session_id;       // 会话ID
    uint32_t start_seq;        // 请求起始序列号
    uint32_t end_seq;          // 请求结束序列号
    
    void hton() {
        header.hton();
        session_id = htonl(session_id);
        start_seq = htonl(start_seq);
        end_seq = htonl(end_seq);
    }
    
    void ntoh() {
        header.ntoh();
        session_id = ntohl(session_id);
        start_seq = ntohl(start_seq);
        end_seq = ntohl(end_seq);
    }
};

// 登出请求
struct LogoutMessage : public NetMessage {
    uint32_t session_id;      // 会话ID
    
    void hton() {
        header.hton();
        session_id = htonl(session_id);
    }
    
    void ntoh() {
        header.ntoh();
        session_id = ntohl(session_id);
    }
};

} // namespace reliable_udp