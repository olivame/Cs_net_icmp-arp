#include <string.h>
#include <stdio.h> 
#include "xnet_tiny.h"

#define min(a, b)               ((a) > (b) ? (b) : (a))
#define swap_order16(v)         ((((v) & 0xFF) << 8) | (((v) >> 8) & 0xFF))
static void arp_send_request(const uint8_t ip[4]);
static void xicmp_in(const uint8_t src_ip[4], xnet_packet_t *packet);
static uint8_t netif_mac[XNET_MAC_ADDR_SIZE];               // 本机 MAC 地址
static uint8_t netif_ip[4];                                 // 本机 IP 地址（网络字节序）
static xnet_packet_t tx_packet, rx_packet;                  // 收发缓冲区
static const uint8_t broadcast_mac[XNET_MAC_ADDR_SIZE] = {  // 以太网广播 MAC
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static xarp_entry_t arp_table[XARP_TABLE_SIZE];
static uint32_t arp_timer_ticks = 0;

// Traceroute state
static uint8_t traceroute_reached_dest = 0;
static uint8_t traceroute_active = 0;

// Ping id/seq generator could be kept externally; provide helper function below

static uint16_t ip_checksum16(const void *buf, uint16_t len);
static uint16_t icmp_checksum16(const void *buf, uint16_t len);

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
 * 当前实验写死为：192.168.232.200
 */
static void arp_init(void) {
    netif_ip[0] = 192;
    netif_ip[1] = 168;
    netif_ip[2] = 232;
    netif_ip[3] = 200;
}

static xarp_entry_t * arp_table_find(const uint8_t ip[4]) {
    for (int i = 0; i < XARP_TABLE_SIZE; i++) {
        if (arp_table[i].state != XARP_ENTRY_FREE &&
            memcmp(arp_table[i].ip, ip, 4) == 0) {
            return &arp_table[i];
        }
    }
    return 0;
}

static xarp_entry_t * arp_table_alloc(const uint8_t ip[4]) {
    for (int i = 0; i < XARP_TABLE_SIZE; i++) {
        if (arp_table[i].state == XARP_ENTRY_FREE) {
            xarp_entry_t *e = &arp_table[i];
            memset(e, 0, sizeof(*e));
            memcpy(e->ip, ip, 4);
            return e;
        }
    }
    return 0;
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

    // 【新增修复】处理以太网最小帧长限制 (60 bytes)
    if (packet->size < 60) {
        // 计算需要填充的字节数
        uint16_t padding_size = 60 - packet->size;
        // 确保不超过最大缓冲区
        if (packet->size + padding_size <= XNET_CFG_PACKET_MAX_SIZE) {
            // 将 packet->data 及其后面的 padding 区域清零（通常 ARP 后面填 0 即可）
            memset(packet->data + packet->size, 0, padding_size);
            // 更新包的大小
            packet->size += padding_size;
        }
    }

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
        xarp_entry_t *e = arp_table_find(arp->sender_ip);
        if (e == 0) {
            e = arp_table_alloc(arp->sender_ip);
        }
        if (e) {
            int index = (int)(e - arp_table);  // 新建 index 变量

            memcpy(e->mac, arp->sender_mac, XNET_MAC_ADDR_SIZE);
            e->state = XARP_ENTRY_OK;
            e->ttl = 1000;        // 例如 1000 个“tick”后过期
            e->retry = 0;

            printf("ARP update[%d]: %d.%d.%d.%d -> %02X:%02X:%02X:%02X:%02X:%02X\n",
               index,
               arp->sender_ip[0], arp->sender_ip[1], arp->sender_ip[2], arp->sender_ip[3],
               arp->sender_mac[0], arp->sender_mac[1], arp->sender_mac[2],
               arp->sender_mac[3], arp->sender_mac[4], arp->sender_mac[5]);
        }
    }
}

const uint8_t * arp_resolve(const uint8_t ip[4]) {
    xarp_entry_t *e = arp_table_find(ip);
    if (e && e->state == XARP_ENTRY_OK) {
        printf("ARP hit: %d.%d.%d.%d -> %02X:%02X:%02X:%02X:%02X:%02X\n",
        ip[0], ip[1], ip[2], ip[3],
        e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5]);
        return e->mac;
    }

    if (e == 0) {
        
        e = arp_table_alloc(ip);
    }
    if (e && e->state == XARP_ENTRY_FREE) {
        // 填初始信息，发送第一次 ARP Request
        e->state = XARP_ENTRY_PENDING;
        e->retry = 3;       // 最多重发 3 次
        e->ttl = 5;        // 等待 5 个 tick
        // 构造并发送一次 ARP Request（和你现在回复用的结构类似）
        // target_ip = ip, target_mac 全 0, dst MAC = 广播
        // 可以写一个小函数 arp_send_request(ip) 复用上面的打包逻辑
        arp_send_request(ip); 
    }

    return 0;   // 现在还不知道 MAC，上层需要等
}


void arp_table_timer(void) {
    for (int i = 0; i < XARP_TABLE_SIZE; i++) {
        xarp_entry_t *e = &arp_table[i];
        if (e->state == XARP_ENTRY_FREE) continue;

        if (e->ttl > 0) {
            e->ttl--;
        }

        if (e->state == XARP_ENTRY_PENDING && e->ttl == 0) {
            if (e->retry > 0) {
                e->retry--;
                e->ttl = 50;
                printf("ARP retry[%d]: %d.%d.%d.%d, left=%d\n",
                       i, e->ip[0], e->ip[1], e->ip[2], e->ip[3], e->retry);
                arp_send_request(e->ip);
            } else {
                printf("ARP timeout free[%d]: %d.%d.%d.%d\n",
                       i, e->ip[0], e->ip[1], e->ip[2], e->ip[3]);
                e->state = XARP_ENTRY_FREE;
            }
        } else if (e->state == XARP_ENTRY_OK && e->ttl == 0) {
            printf("ARP entry expired[%d]: %d.%d.%d.%d\n",
                   i, e->ip[0], e->ip[1], e->ip[2], e->ip[3]);
            e->state = XARP_ENTRY_FREE;
        }
    }
}


static void arp_send_request(const uint8_t ip[4]) {
    xnet_packet_t *packet = xnet_alloc_for_send((uint16_t)sizeof(xarp_packet_t));
    xarp_packet_t *arp = (xarp_packet_t *)packet->data;

    arp->hw_type    = swap_order16(1);                 // 以太网
    arp->proto_type = swap_order16(XNET_PROTOCOL_IP);  // IPv4
    arp->hw_len     = XNET_MAC_ADDR_SIZE;
    arp->proto_len  = 4;
    arp->opcode     = swap_order16(XARP_OPCODE_REQUEST);

    memcpy(arp->sender_mac, netif_mac, XNET_MAC_ADDR_SIZE);
    memcpy(arp->sender_ip,  netif_ip,  4);             // 192.168.108.200
    memset(arp->target_mac, 0,         XNET_MAC_ADDR_SIZE);
    memcpy(arp->target_ip,  ip,        4);             // 要查询的 IP

    ethernet_out_to(XNET_PROTOCOL_ARP, broadcast_mac, packet);
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
            remove_header(packet, sizeof(xether_hdr_t));
            xip_in(packet);        // 把 IP 数据包交给 IP 层
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
    static uint32_t tick = 0;
    ethernet_poll();

    if (++tick >= 10) {     // 每调用 10 次 poll 当作 1 个 tick
        tick = 0;
        arp_table_timer();
        arp_timer_ticks++;   // increase global tick counter used for ping timestamps
    }
}

void xip_in(xnet_packet_t *packet) {
    if (packet->size < sizeof(xip_hdr_t)) return;

    xip_hdr_t *ip = (xip_hdr_t *)packet->data;

    uint8_t ver  = ip->ver_hdrlen >> 4;
    uint8_t ihl  = ip->ver_hdrlen & 0x0F;
    uint16_t hdr_len = ihl * 4;
    if (ver != 4 || hdr_len < sizeof(xip_hdr_t) || packet->size < hdr_len) return;

    uint16_t chk = ip->hdr_checksum;
    ip->hdr_checksum = 0;
    if (ip_checksum16(ip, hdr_len) != chk) return;
    ip->hdr_checksum = chk;

    if (memcmp(ip->dest_ip, netif_ip, 4) != 0) return;

    // 关键：先把对方 IP 拷出来，避免后面覆盖
    uint8_t src_ip[4];
    memcpy(src_ip, ip->src_ip, 4);

    remove_header(packet, hdr_len);

    if (ip->protocol == XIP_PROTOCOL_ICMP) {
        xicmp_in(src_ip, packet);   // 传临时数组，而不是 ip->src_ip 指针
    }
}


static void xicmp_in(const uint8_t src_ip[4], xnet_packet_t *packet) {
    if (packet->size < sizeof(xicmp_hdr_t)) return;

    xicmp_hdr_t *icmp = (xicmp_hdr_t *)packet->data;

    uint16_t recv_sum = icmp->checksum;
    icmp->checksum = 0;
    if (icmp_checksum16(icmp, packet->size) != recv_sum) return;

    if (icmp->type == 8 && icmp->code == 0) {  // Echo Request
        // 直接在原报文上构造 Reply
        icmp->type = 0;
        icmp->checksum = 0;
        icmp->checksum = icmp_checksum16(icmp, packet->size);

        // 通过 IP 层发回去：src_ip 是对方 IP
        xip_out(XIP_PROTOCOL_ICMP, src_ip, packet);
    } else if (icmp->type == 0 && icmp->code == 0) {
        // Echo Reply: print information and RTT if timestamp present
        uint16_t id = icmp->id;
        uint16_t seq = icmp->seq;
        // payload may contain a 32-bit timestamp (arp_timer_ticks) placed by sender
        uint32_t rtt_ticks = 0;
        if (packet->size >= sizeof(xicmp_hdr_t) + 4) {
            // timestamp stored in network byte order (we used host uint32 directly), read as little-endian
            uint8_t *pdata = packet->data + sizeof(xicmp_hdr_t);
            // reconstruct uint32 (host endian assumed little-endian on target)
            rtt_ticks = (uint32_t)pdata[0] | ((uint32_t)pdata[1] << 8) | ((uint32_t)pdata[2] << 16) | ((uint32_t)pdata[3] << 24);
            uint32_t now = arp_timer_ticks;
            uint32_t diff = (now >= rtt_ticks) ? (now - rtt_ticks) : 0;
            if (traceroute_active) {
                printf("  Traceroute reached destination: %d.%d.%d.%d (rtt=%u ticks)\n",
                       src_ip[0], src_ip[1], src_ip[2], src_ip[3], diff);
                traceroute_reached_dest = 1;
            } else {
                printf("PING reply: %d.%d.%d.%d id=%u seq=%u rtt=%u ticks\n",
                       src_ip[0], src_ip[1], src_ip[2], src_ip[3], id, seq, diff);
            }
        } else {
            if (traceroute_active) {
                printf("  Traceroute reached destination: %d.%d.%d.%d\n",
                       src_ip[0], src_ip[1], src_ip[2], src_ip[3]);
                traceroute_reached_dest = 1;
            } else {
                printf("PING reply: %d.%d.%d.%d id=%u seq=%u\n",
                       src_ip[0], src_ip[1], src_ip[2], src_ip[3], id, seq);
            }
        }
    } else if (icmp->type == 11) {  // Time Exceeded
        // This is sent by a router when TTL reaches 0
        if (traceroute_active) {
            // Extract timestamp from the encapsulated packet if present
            uint32_t rtt_ticks = 0;
            // Time Exceeded includes original IP header + 8 bytes of original data
            // Skip to the ICMP echo request data (after 2nd IP + ICMP headers)
            if (packet->size >= sizeof(xicmp_hdr_t) + sizeof(xip_hdr_t) + sizeof(xicmp_hdr_t) + 4) {
                uint8_t *encap_data = packet->data + sizeof(xicmp_hdr_t) + sizeof(xip_hdr_t) + sizeof(xicmp_hdr_t);
                rtt_ticks = (uint32_t)encap_data[0] | ((uint32_t)encap_data[1] << 8) | 
                            ((uint32_t)encap_data[2] << 16) | ((uint32_t)encap_data[3] << 24);
                uint32_t now = arp_timer_ticks;
                uint32_t diff = (now >= rtt_ticks) ? (now - rtt_ticks) : 0;
                printf("  Hop from: %d.%d.%d.%d (rtt=%u ticks)\n",
                       src_ip[0], src_ip[1], src_ip[2], src_ip[3], diff);
            } else {
                printf("  Hop from: %d.%d.%d.%d\n",
                       src_ip[0], src_ip[1], src_ip[2], src_ip[3]);
            }
        }
    } else if (icmp->type == 3) {  // Destination Unreachable
        if (traceroute_active) {
            printf("  Destination unreachable from: %d.%d.%d.%d (code=%u)\n",
                   src_ip[0], src_ip[1], src_ip[2], src_ip[3], icmp->code);
            traceroute_reached_dest = 1;  // Consider this as end
        }
    }
}

// Send one ICMP Echo Request to dest_ip. Returns 0 if packet sent, -1 if ARP unresolved
int xicmp_ping(const uint8_t dest_ip[4], uint16_t id, uint16_t seq) {
    // Check ARP cache first
    const uint8_t *mac = arp_resolve(dest_ip);
    if (!mac) {
        // ARP in progress; arp_resolve has initiated request if needed
        return -1;
    }

    // Build ICMP Echo Request with small payload (4-byte timestamp)
    const uint16_t payload_len = 4; // store 32-bit tick
    xnet_packet_t *packet = xnet_alloc_for_send((uint16_t)(sizeof(xicmp_hdr_t) + payload_len));
    xicmp_hdr_t *icmp = (xicmp_hdr_t *)packet->data;

    icmp->type = 8; // Echo Request
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = id;
    icmp->seq = seq;

    // store timestamp (arp_timer_ticks) in payload (little-endian)
    uint8_t *pdata = packet->data + sizeof(xicmp_hdr_t);
    uint32_t ts = arp_timer_ticks;
    pdata[0] = (uint8_t)(ts & 0xFF);
    pdata[1] = (uint8_t)((ts >> 8) & 0xFF);
    pdata[2] = (uint8_t)((ts >> 16) & 0xFF);
    pdata[3] = (uint8_t)((ts >> 24) & 0xFF);

    // compute checksum
    icmp->checksum = icmp_checksum16(icmp, packet->size);

    // send via IP layer
    xip_out(XIP_PROTOCOL_ICMP, dest_ip, packet);
    return 0;
}

void xip_out_ttl(xip_protocol_t protocol,
                 const uint8_t dest_ip[4],
                 xnet_packet_t *packet,
                 uint8_t ttl) {
    // 先查 ARP：拿到对方 MAC（目前我们已经有 arp_resolve）
    const uint8_t *mac = arp_resolve(dest_ip);
    if (!mac) {
        // 还没解析到 MAC，先等 ARP 表更新
        return;
    }

    // 在 ICMP 前面加 IP 头
    add_header(packet, sizeof(xip_hdr_t));
    xip_hdr_t *ip = (xip_hdr_t *)packet->data;

    ip->ver_hdrlen     = 0x45;
    ip->tos            = 0;
    ip->total_len      = swap_order16(packet->size);
    ip->id             = 0;
    ip->flags_fragment = 0;
    ip->ttl            = ttl;  // Use custom TTL
    ip->protocol       = protocol;
    memcpy(ip->src_ip,  netif_ip, 4);
    memcpy(ip->dest_ip, dest_ip, 4);
    ip->hdr_checksum   = 0;
    ip->hdr_checksum   = ip_checksum16(ip, sizeof(xip_hdr_t));

    // 交给以太网层发送
    ethernet_out_to(XNET_PROTOCOL_IP, mac, packet);
}

void xip_out(xip_protocol_t protocol,
             const uint8_t dest_ip[4],
             xnet_packet_t *packet) {
    xip_out_ttl(protocol, dest_ip, packet, 64);  // Default TTL=64
}

static uint16_t checksum16(const void *buf, uint16_t len) {
    const uint16_t *data = buf;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *data++;
        len -= 2;
    }
    if (len) {
        sum += *(const uint8_t *)data;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static uint16_t ip_checksum16(const void *buf, uint16_t len)   { return checksum16(buf, len); }
static uint16_t icmp_checksum16(const void *buf, uint16_t len) { return checksum16(buf, len); }

// Traceroute implementation
int xicmp_traceroute_probe(const uint8_t dest_ip[4], uint16_t id, uint16_t seq, uint8_t ttl) {
    // Check ARP cache first
    const uint8_t *mac = arp_resolve(dest_ip);
    if (!mac) {
        return -1;  // ARP in progress
    }

    // Build ICMP Echo Request with timestamp payload
    const uint16_t payload_len = 4;
    xnet_packet_t *packet = xnet_alloc_for_send((uint16_t)(sizeof(xicmp_hdr_t) + payload_len));
    xicmp_hdr_t *icmp = (xicmp_hdr_t *)packet->data;

    icmp->type = 8;  // Echo Request
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = id;
    icmp->seq = seq;

    // Store timestamp
    uint8_t *pdata = packet->data + sizeof(xicmp_hdr_t);
    uint32_t ts = arp_timer_ticks;
    pdata[0] = (uint8_t)(ts & 0xFF);
    pdata[1] = (uint8_t)((ts >> 8) & 0xFF);
    pdata[2] = (uint8_t)((ts >> 16) & 0xFF);
    pdata[3] = (uint8_t)((ts >> 24) & 0xFF);

    icmp->checksum = icmp_checksum16(icmp, packet->size);

    // Send with custom TTL
    xip_out_ttl(XIP_PROTOCOL_ICMP, dest_ip, packet, ttl);
    return 0;
}

int xicmp_traceroute_is_complete(void) {
    return traceroute_reached_dest;
}

void xicmp_traceroute_reset(void) {
    traceroute_reached_dest = 0;
    traceroute_active = 1;
}
