# Traceroute Implementation

## 概述

本实现在现有的网络协议栈基础上添加了 Traceroute 功能。Traceroute 通过发送递增 TTL 值的 ICMP Echo Request 报文来追踪到目标主机的网络路径。

## 工作原理

1. **TTL 递增**: 从 TTL=1 开始，每次发送 ICMP Echo Request
2. **中间路由器响应**: 当 TTL 减到 0 时，路由器返回 ICMP Time Exceeded (Type 11) 消息
3. **目标主机响应**: 当报文到达目标主机时，返回 ICMP Echo Reply (Type 0) 消息
4. **路径追踪**: 通过接收的响应，可以识别路径上的每一跳

## 实现的功能

### 1. ICMP 消息处理扩展

- **ICMP Time Exceeded (Type 11)**: 处理中间路由器的 TTL 超时响应
- **ICMP Destination Unreachable (Type 3)**: 处理目标不可达消息
- **ICMP Echo Reply (Type 0)**: 处理目标主机的响应

### 2. 新增函数

#### `xip_out_ttl()`
```c
void xip_out_ttl(xip_protocol_t protocol,
                 const uint8_t dest_ip[4],
                 xnet_packet_t *packet,
                 uint8_t ttl);
```
发送自定义 TTL 的 IP 数据包。

#### `xicmp_traceroute_probe()`
```c
int xicmp_traceroute_probe(const uint8_t dest_ip[4], 
                           uint16_t id, 
                           uint16_t seq, 
                           uint8_t ttl);
```
发送 traceroute 探测包，指定 TTL 值。

#### `xicmp_traceroute_reset()`
```c
void xicmp_traceroute_reset(void);
```
重置 traceroute 状态，在开始新的 traceroute 会话时调用。

#### `xicmp_traceroute_is_complete()`
```c
int xicmp_traceroute_is_complete(void);
```
检查是否已到达目标主机。

## 使用方法

### 在 `app.c` 中切换模式

```c
#define MODE_PING       0
#define MODE_TRACEROUTE 1

int main (void) {
    xnet_init();
    
    uint8_t dest_ip[4] = {192, 168, 108, 136};
    
    // 选择 Traceroute 模式
    int mode = MODE_TRACEROUTE;
    
    if (mode == MODE_TRACEROUTE) {
        xicmp_traceroute_reset();  // 初始化 traceroute 状态
        
        // 发送探测包，TTL 从 1 开始递增
        for (uint8_t ttl = 1; ttl <= 30; ttl++) {
            xicmp_traceroute_probe(dest_ip, 1000, ttl, ttl);
            
            // 等待响应...
            Sleep(1000);
            
            if (xicmp_traceroute_is_complete()) {
                printf("Traceroute complete!\n");
                break;
            }
        }
    }
    
    return 0;
}
```

## 输出示例

```
xnet running. Target: 192.168.108.136
Mode: TRACEROUTE

Probe: TTL=1, seq=1
  Hop from: 192.168.108.1 (rtt=5 ticks)
Probe: TTL=2, seq=2
  Hop from: 10.0.0.1 (rtt=12 ticks)
Probe: TTL=3, seq=3
  Traceroute reached destination: 192.168.108.136 (rtt=18 ticks)
Traceroute complete!
```

## 关键参数

- **traceroute_max_hops**: 最大跳数 (默认 30)
- **max_wait_ticks**: 每个探测的最大等待时间 (默认 30 ticks = 3 秒)
- **traceroute_probes_per_hop**: 每跳发送的探测包数量 (默认 1)

## 注意事项

1. **ARP 解析**: 在发送探测包前，必须先解析目标 IP 的 MAC 地址
2. **防火墙**: 某些防火墙可能阻止 ICMP 消息，导致无法收到响应
3. **网络环境**: 需要在支持 ICMP 的网络环境中测试
4. **超时处理**: 如果某一跳无响应，会显示 "* Request timed out"

## 编译和运行

```bash
cd build
cmake ..
make
./xnet_app
```

## 切换回 Ping 模式

在 `app.c` 中修改：
```c
int mode = MODE_PING;  // 使用 Ping 模式
```
