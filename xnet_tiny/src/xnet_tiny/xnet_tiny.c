#include <string.h>
#include "xnet_tiny.h"

#define min(a, b)               ((a) > (b) ? (b) : (a))
#define swap_order16(v)         ((((v) & 0xFF) << 8) | (((v) >> 8) & 0xFF))

static uint8_t netif_mac[XNET_MAC_ADDR_SIZE];               // 本机 MAC 地址
static uint8_t netif_ip[4];                                 // 本机 IP 地址（网络字节序）
static xnet_packet_t tx_packet, rx_packet;                  // 收发缓冲区
static const uint8_t broadcast_mac[XNET_MAC_ADDR_SIZE] = {  // 以太网广播 MAC
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/**
 * 分配一个发送用的数据包
 */
xnet_packet_t * xnet_alloc_for_send(uint16_t data_size) {
    tx_packet.data = tx_packet.payload + XNET_CFG_PACKET_MAX_SIZE - data_size;
    tx_packet.size = data_size;
    return &tx_packet;
}

/**
 * 分配一个接收用的数据包
 */
xnet_packet_t * xnet_alloc_for_read(uint16_t data_size) {
    rx_packet.data = rx_packet.payload;
    rx_packet.size = data_size;
    return &rx_packet;
}

/**
 * 为数据包增加一个头部
 */
static void add_header(xnet_packet_t *packet, uint16_t header_size) {
    packet->data -= header_size;
    packet->size += header_size;
}

/**
 * 去掉数据包前面的一个头部
 */
static void remove_header(xnet_packet_t *packet, uint16_t header_size) {
    packet->data += header_size;
    packet->size -= header_size;
}

/**
 * 将数据包长度裁剪为不超过 size
 */
static void truncate_packet(xnet_packet_t *packet, uint16_t size) {
    packet->size = min(packet->size, size);
}

/**
 * 以太网层初始化
 */
static xnet_err_t ethernet_init (void) {
    xnet_err_t err = xnet_driver_open(netif_mac);
    if (err < 0) return err;

    return XNET_ERR_OK;
}

/**
 * ARP 模块初始化：设置协议栈自己的 IP
 * 当前实验写死为：192.168.108.200
 */
static void arp_init(void) {
    netif_ip[0] = 192;
    netif_ip[1] = 168;
    netif_ip[2] = 108;
    netif_ip[3] = 200;
}

/**
 * 发送一个以太网帧
 */
static xnet_err_t ethernet_out_to(xnet_protocol_t protocol,
                                  const uint8_t *mac_addr,
                                  xnet_packet_t * packet) {
    xether_hdr_t* ether_hdr;

    add_header(packet, sizeof(xether_hdr_t));
    ether_hdr = (xether_hdr_t*)packet->data;
    memcpy(ether_hdr->dest, mac_addr, XNET_MAC_ADDR_SIZE);
    memcpy(ether_hdr->src, netif_mac, XNET_MAC_ADDR_SIZE);
    ether_hdr->protocol = swap_order16(protocol);

    return xnet_driver_send(packet);
}

/**
 * 发送一次“无回报 ARP”（gratuitous ARP）
 * 报文形式：ARP Request，sender IP = target IP = 本机 IP，目标 MAC 为空，目的 MAC 为广播
 */
static void arp_send_gratuitous(void) {
    xnet_packet_t *packet = xnet_alloc_for_send((uint16_t)sizeof(xarp_packet_t));
    xarp_packet_t *arp = (xarp_packet_t *)packet->data;

    arp->hw_type    = swap_order16(1);                 // 以太网
    arp->proto_type = swap_order16(XNET_PROTOCOL_IP);  // IPv4
    arp->hw_len     = XNET_MAC_ADDR_SIZE;
    arp->proto_len  = 4;
    arp->opcode     = swap_order16(XARP_OPCODE_REQUEST);

    memcpy(arp->sender_mac, netif_mac, XNET_MAC_ADDR_SIZE);
    memcpy(arp->sender_ip,  netif_ip,  4);
    memset(arp->target_mac, 0,         XNET_MAC_ADDR_SIZE);
    memcpy(arp->target_ip,  netif_ip,  4);

    ethernet_out_to(XNET_PROTOCOL_ARP, broadcast_mac, packet);
}

/**
 * ARP 报文输入处理
 */
static void arp_in(xnet_packet_t *packet) {
    if (packet->size < sizeof(xarp_packet_t)) {
        return;
    }

    truncate_packet(packet, sizeof(xarp_packet_t));
    xarp_packet_t *arp = (xarp_packet_t*)packet->data;

    uint16_t hw_type    = swap_order16(arp->hw_type);
    uint16_t proto_type = swap_order16(arp->proto_type);
    uint16_t opcode     = swap_order16(arp->opcode);

    if ((hw_type != 1) ||
        (proto_type != XNET_PROTOCOL_IP) ||
        (arp->hw_len != XNET_MAC_ADDR_SIZE) ||
        (arp->proto_len != 4)) {
        return;
    }

    if (memcmp(arp->target_ip, netif_ip, 4) != 0) {
        return;
    }

    if (opcode == XARP_OPCODE_REQUEST) {
        uint8_t reply_target_mac[XNET_MAC_ADDR_SIZE];
        uint8_t reply_target_ip[4];

        memcpy(reply_target_mac, arp->sender_mac, XNET_MAC_ADDR_SIZE);
        memcpy(reply_target_ip,  arp->sender_ip,  4);

        arp->hw_type    = swap_order16(1);
        arp->proto_type = swap_order16(XNET_PROTOCOL_IP);
        arp->hw_len     = XNET_MAC_ADDR_SIZE;
        arp->proto_len  = 4;
        arp->opcode     = swap_order16(XARP_OPCODE_REPLY);

        memcpy(arp->sender_mac, netif_mac,        XNET_MAC_ADDR_SIZE);
        memcpy(arp->sender_ip,  netif_ip,         4);
        memcpy(arp->target_mac, reply_target_mac, XNET_MAC_ADDR_SIZE);
        memcpy(arp->target_ip,  reply_target_ip,  4);

        ethernet_out_to(XNET_PROTOCOL_ARP, reply_target_mac, packet);
    } else if (opcode == XARP_OPCODE_REPLY) {
        // 拓展任务：这里可以更新 ARP 表（目前不做）
    }
}

/**
 * 以太网帧输入处理
 */
static void ethernet_in (xnet_packet_t * packet) {
    if (packet->size <= sizeof(xether_hdr_t)) {
        return;
    }

    xether_hdr_t* hdr = (xether_hdr_t*)packet->data;
    switch (swap_order16(hdr->protocol)) {
        case XNET_PROTOCOL_ARP:
            remove_header(packet, sizeof(xether_hdr_t));
            arp_in(packet);
            break;
        case XNET_PROTOCOL_IP:
            // 预留给后续 IP 实验
            break;
        default:
            break;
    }
}

/**
 * 轮询底层网卡
 */
static void ethernet_poll (void) {
    xnet_packet_t * packet;

    if (xnet_driver_read(&packet) == XNET_ERR_OK) {
        ethernet_in(packet);
    }
}

void xnet_init (void) {
    ethernet_init();
    arp_init();
    arp_send_gratuitous();      // 启动时主动发送一次无回报 ARP
}

void xnet_poll(void) {
    ethernet_poll();
}

