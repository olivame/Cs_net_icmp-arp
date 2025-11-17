#ifndef XNET_TINY_H
#define XNET_TINY_H

#include <stdint.h>

#define XNET_CFG_PACKET_MAX_SIZE        1516        // 收发数据包的最大大小

#pragma pack(1)

#define XNET_MAC_ADDR_SIZE              6           // MAC地址长度

/**
 * 以太网数据帧格式：RFC894
 */
typedef struct _xether_hdr_t {
    uint8_t dest[XNET_MAC_ADDR_SIZE];           // 目标mac地址
    uint8_t src[XNET_MAC_ADDR_SIZE];            // 源mac地址
    uint16_t protocol;                          // 协议/长度
}xether_hdr_t;

#pragma pack()

typedef enum _xnet_err_t {
    XNET_ERR_OK = 0,
    XNET_ERR_IO = -1,
}xnet_err_t;

/**
 * 网络数据结构
 */
typedef struct _xnet_packet_t{
    uint16_t size;                              // 包中有效数据大小
    uint8_t * data;                             // 包的数据起始地址
    uint8_t payload[XNET_CFG_PACKET_MAX_SIZE];  // 最大负载数据量
}xnet_packet_t;

xnet_packet_t * xnet_alloc_for_send(uint16_t data_size);
xnet_packet_t * xnet_alloc_for_read(uint16_t data_size);

xnet_err_t xnet_driver_open (uint8_t * mac_addr);
xnet_err_t xnet_driver_send (xnet_packet_t * packet);
xnet_err_t xnet_driver_read (xnet_packet_t ** packet);

typedef enum _xnet_protocol_t {
    XNET_PROTOCOL_ARP = 0x0806,     // ARP协议
    XNET_PROTOCOL_IP = 0x0800,      // IP协议
}xnet_protocol_t;

void xnet_init (void);
void xnet_poll(void);

#endif // XNET_TINY_H

