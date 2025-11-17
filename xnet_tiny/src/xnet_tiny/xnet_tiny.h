#ifndef XNET_TINY_H
#define XNET_TINY_H

#include <stdint.h>

// 收发数据包的最大大小
#define XNET_CFG_PACKET_MAX_SIZE        1516

#pragma pack(1)

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

void xnet_init (void);
void xnet_poll(void);

#endif // XNET_TINY_H

