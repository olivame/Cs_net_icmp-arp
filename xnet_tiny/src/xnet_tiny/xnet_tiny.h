#ifndef XNET_TINY_H
#define XNET_TINY_H

#include <stdint.h>

// 收发数据包的最大大小
#define XNET_CFG_PACKET_MAX_SIZE        1516

#pragma pack(1)

#define XNET_IP_ADDR_SIZE 4

typedef struct _xip_hdr_t {
    uint8_t  ver_hdrlen;      // 版本(4) + 头长(4) -> 固定 0x45
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_fragment;
    uint8_t  ttl;
    uint8_t  protocol;        // 1 = ICMP
    uint16_t hdr_checksum;
    uint8_t  src_ip[XNET_IP_ADDR_SIZE];
    uint8_t  dest_ip[XNET_IP_ADDR_SIZE];
} xip_hdr_t;

typedef struct _xicmp_hdr_t {
    uint8_t  type;       // 8=Request, 0=Reply, 11=Time Exceeded, 3=Dest Unreachable
    uint8_t  code;       // Echo 固定为 0; Time Exceeded: 0=TTL expired
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
    // 后面紧跟 data
} xicmp_hdr_t;

#define XICMP_TYPE_ECHO_REPLY       0
#define XICMP_TYPE_DEST_UNREACH     3
#define XICMP_TYPE_ECHO_REQUEST     8
#define XICMP_TYPE_TIME_EXCEEDED    11

typedef enum _xip_protocol_t {
    XIP_PROTOCOL_ICMP = 1,
} xip_protocol_t;


// MAC 地址长度
#define XNET_MAC_ADDR_SIZE              6

/**
 * ARP 报文格式（以太网 + IPv4）
 */
typedef struct _xarp_packet_t {
    uint16_t hw_type;                              // 硬件类型，1 表示以太网
    uint16_t proto_type;                           // 上层协议类型，0x0800 表示 IPv4
    uint8_t  hw_len;                               // 硬件地址长度，MAC 为 6
    uint8_t  proto_len;                            // 协议地址长度，IPv4 为 4
    uint16_t opcode;                               // 操作码：1-请求，2-应答
    uint8_t  sender_mac[XNET_MAC_ADDR_SIZE];       // 发送方 MAC
    uint8_t  sender_ip[4];                         // 发送方 IP
    uint8_t  target_mac[XNET_MAC_ADDR_SIZE];       // 目标 MAC
    uint8_t  target_ip[4];                         // 目标 IP
} xarp_packet_t;

#define XARP_TABLE_SIZE 8

typedef enum _xarp_entry_state_t {
    XARP_ENTRY_FREE = 0,
    XARP_ENTRY_PENDING,
    XARP_ENTRY_OK,
} xarp_entry_state_t;

typedef struct _xarp_entry_t {
    uint8_t ip[4];
    uint8_t mac[XNET_MAC_ADDR_SIZE];
    xarp_entry_state_t state;
    uint8_t retry;      // 已重发次数
    uint16_t ttl;       // 剩余“生存时间”（轮询计数）
} xarp_entry_t;

typedef enum _xarp_opcode_t {
    XARP_OPCODE_REQUEST = 1,                       // ARP 请求
    XARP_OPCODE_REPLY   = 2,                       // ARP 应答
} xarp_opcode_t;

/**
 * 以太网帧头格式，参考 RFC894
 */
typedef struct _xether_hdr_t {
    uint8_t dest[XNET_MAC_ADDR_SIZE];              // 目的 MAC 地址
    uint8_t src[XNET_MAC_ADDR_SIZE];               // 源 MAC 地址
    uint16_t protocol;                             // 上层协议类型
} xether_hdr_t;

#pragma pack()

typedef enum _xnet_err_t {
    XNET_ERR_OK = 0,
    XNET_ERR_IO = -1,
} xnet_err_t;

/**
 * 网络数据包结构
 */
typedef struct _xnet_packet_t{
    uint16_t size;                                 // 当前有效数据长度
    uint8_t * data;                                // 当前数据起始地址
    uint8_t payload[XNET_CFG_PACKET_MAX_SIZE];     // 最大负载空间
} xnet_packet_t;

xnet_packet_t * xnet_alloc_for_send(uint16_t data_size);
xnet_packet_t * xnet_alloc_for_read(uint16_t data_size);

xnet_err_t xnet_driver_open (uint8_t * mac_addr);
xnet_err_t xnet_driver_send (xnet_packet_t * packet);
xnet_err_t xnet_driver_read (xnet_packet_t ** packet);

typedef enum _xnet_protocol_t {
    XNET_PROTOCOL_ARP = 0x0806,                    // ARP 协议
    XNET_PROTOCOL_IP  = 0x0800,                    // IP 协议
} xnet_protocol_t;

const uint8_t * arp_resolve(const uint8_t ip[4]);
void arp_table_timer(void);

void xnet_init (void);
void xnet_poll(void);

void xip_in(xnet_packet_t *packet);
void xip_out(xip_protocol_t protocol,
             const uint8_t dest_ip[4],
             xnet_packet_t *packet);

void xip_out_ttl(xip_protocol_t protocol,
                 const uint8_t dest_ip[4],
                 xnet_packet_t *packet,
                 uint8_t ttl);

// Send a single ICMP Echo Request (ping) with configurable payload size
// Returns 0 on success (packet sent), -1 if destination MAC unknown (ARP in progress)
int xicmp_ping(const uint8_t dest_ip[4], uint16_t id, uint16_t seq, uint16_t data_size);

// Get RTT (ms) of the last received ICMP Echo Reply; returns -1 if none pending
int xicmp_get_last_rtt(void);

// Traceroute: send ICMP Echo with specific TTL
// Returns 0 on success, -1 if ARP unresolved
int xicmp_traceroute_probe(const uint8_t dest_ip[4], uint16_t id, uint16_t seq, uint8_t ttl);

// Check if traceroute has reached destination
int xicmp_traceroute_is_complete(void);

// 当前 TTL 是否已经收到 Time Exceeded（中间路由器回复）
// 返回非 0 表示已经收到，读一次后会自动清零
int xicmp_traceroute_has_hop_reply(void);

// Get traceroute hop information
void xicmp_traceroute_reset(void);

             
#endif // XNET_TINY_H
