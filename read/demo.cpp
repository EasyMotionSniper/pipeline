#include <cstdint>

#pragma pack(push, 1)

enum class MsgType : uint16_t {
    DATA = 1,
    NACK = 2,
    HEARTBEAT = 3,
    RECOVERY_REQ = 4,
    ACK = 5,
};

struct MsgHeader {
    MsgType type;
};

struct DataMsg {
    uint64_t seqno;
    char payload[128];
};

struct NackMsg {
    uint64_t subscriber_id;
    uint64_t missing_seqnos[8];
    uint32_t missing_count;
};

struct HeartBeatMsg {
    uint64_t publisher_id;
    uint64_t latest_seqno;
};

struct RecoveryReqMsg {
    uint64_t subscriber_id;
    uint64_t last_seqno_received;
};

struct AckMsg {
    uint64_t subscriber_id;
    uint64_t acked_seqno;
};

struct NetMessage {
    MsgHeader header;
    union {
        DataMsg data;
        NackMsg nack;
        HeartBeatMsg hb;
        RecoveryReqMsg recovery;
        AckMsg ack;
    } body;
};

#pragma pack(pop)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

int main() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(9000);  // 绑定到9000端口
    local_addr.sin_addr.s_addr = INADDR_ANY;  // 监听所有本地IP
    if (bind(sock, (sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        perror("绑定端口失败");
        close(sock);
        return -1;
    }

    
// 目标地址（可以是本地9000端口，也可以是其他地址）
    sockaddr_in dest_addr{};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(9000);
    dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // 构造并发送NACK消息
    NetMessage msg{};
    msg.header.type = MsgType::NACK;
    msg.body.nack.subscriber_id = 42;
    msg.body.nack.missing_count = 2;
    msg.body.nack.missing_seqnos[0] = 100;
    msg.body.nack.missing_seqnos[1] = 101;

    sendto(sock, &msg, sizeof(msg), 0, (sockaddr*)&dest_addr, sizeof(dest_addr));
    std::cout << "已发送NACK消息" << std::endl;

    // 接收消息（此时监听9000端口，能收到发送到该端口的消息）
    NetMessage msg2;
    sockaddr_in src_addr{};
    socklen_t src_len = sizeof(src_addr);
    ssize_t n = recvfrom(sock, &msg2, sizeof(msg2), 0, (sockaddr*)&src_addr, &src_len);

    if (n > 0) {
        if (msg2.header.type == MsgType::NACK) {
            std::cout << "收到NACK - 订阅者ID: " << msg2.body.nack.subscriber_id
                      << ", 丢失数量: " << msg2.body.nack.missing_count << std::endl;
        }
    } else {
        perror("接收失败");
    }

    close(sock);
    return 0;
}
