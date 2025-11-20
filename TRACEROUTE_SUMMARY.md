# Traceroute 实现总结

## 已完成的修改

### 1. 头文件 (`xnet_tiny.h`)

#### 新增 ICMP 类型常量
```c
#define XICMP_TYPE_ECHO_REPLY       0
#define XICMP_TYPE_DEST_UNREACH     3
#define XICMP_TYPE_ECHO_REQUEST     8
#define XICMP_TYPE_TIME_EXCEEDED    11
```

#### 新增函数声明
```c
// 支持自定义 TTL 的 IP 层输出
void xip_out_ttl(xip_protocol_t protocol,
                 const uint8_t dest_ip[4],
                 xnet_packet_t *packet,
                 uint8_t ttl);

// Traceroute 探测
int xicmp_traceroute_probe(const uint8_t dest_ip[4], uint16_t id, uint16_t seq, uint8_t ttl);

// Traceroute 状态控制
int xicmp_traceroute_is_complete(void);
void xicmp_traceroute_reset(void);
```

### 2. 实现文件 (`xnet_tiny.c`)

#### 新增全局状态变量
```c
static uint8_t traceroute_reached_dest = 0;
static uint8_t traceroute_active = 0;
```

#### 扩展 `xicmp_in()` 函数
- 处理 ICMP Time Exceeded (Type 11) 消息
- 处理 ICMP Destination Unreachable (Type 3) 消息
- 在 traceroute 模式下特殊处理 Echo Reply
- 提取并计算 RTT (往返时间)

#### 实现 `xip_out_ttl()` 函数
- 允许自定义 TTL 值发送 IP 数据包
- 重构原有的 `xip_out()` 使用 `xip_out_ttl()` 并默认 TTL=64

#### 实现 Traceroute 功能函数
```c
int xicmp_traceroute_probe(...)  // 发送探测包
int xicmp_traceroute_is_complete()  // 检查是否完成
void xicmp_traceroute_reset()  // 重置状态
```

### 3. 应用程序 (`app.c`)

#### 新增模式切换
```c
#define MODE_PING       0
#define MODE_TRACEROUTE 1
```

#### Traceroute 主循环逻辑
- TTL 从 1 开始递增到最大跳数（30）
- 每跳可配置发送探测包数量
- 超时检测和重试机制
- 自动检测到达目标主机

## Traceroute 工作流程

```
1. 初始化: xicmp_traceroute_reset()
2. TTL = 1 开始循环
3. 发送探测: xicmp_traceroute_probe(dest_ip, id, seq, TTL)
   ├─ ARP 未解析 → 返回 -1，等待 ARP
   └─ ARP 已解析 → 发送带自定义 TTL 的 ICMP Echo Request
4. 接收响应:
   ├─ ICMP Time Exceeded (Type 11) → 中间路由器地址
   ├─ ICMP Echo Reply (Type 0) → 到达目标主机
   └─ ICMP Dest Unreachable (Type 3) → 目标不可达
5. TTL++ 继续下一跳
6. 检查: xicmp_traceroute_is_complete()
   └─ 如果到达目标或超过最大跳数 → 结束
```

## 测试方法

### 编译
```bash
cd build
cmake ..
make
```

### 运行 Traceroute 模式
在 `app.c` 中设置：
```c
int mode = MODE_TRACEROUTE;
uint8_t dest_ip[4] = {192, 168, 108, 136};  // 修改为目标 IP
```

### 运行 Ping 模式
```c
int mode = MODE_PING;
```

## 预期输出

### Traceroute 成功
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

### 某跳超时
```
Probe: TTL=5, seq=5
  * Request timed out (TTL=5)
Probe: TTL=6, seq=6
  Hop from: 10.0.1.1 (rtt=25 ticks)
```

## 关键特性

✅ **自定义 TTL**: 支持发送任意 TTL 值的 IP 包  
✅ **RTT 测量**: 在 ICMP 负载中嵌入时间戳，计算往返时间  
✅ **超时处理**: 每跳等待超时后自动进入下一跳  
✅ **自动结束**: 检测到目标主机响应后自动停止  
✅ **ARP 集成**: 自动解析目标 MAC 地址  
✅ **模式切换**: 可在 Ping 和 Traceroute 模式间切换  

## 技术细节

### ICMP Time Exceeded 报文结构
```
[ICMP Header - Type 11]
[Original IP Header (20 bytes)]
[Original ICMP Header + 8 bytes of data]
```

### RTT 计算
- 发送时在 ICMP 负载中存入 `arp_timer_ticks`（32位时间戳）
- 接收时提取时间戳，计算差值得到 RTT

### TTL 策略
- 默认 TTL: 64（普通 Ping）
- Traceroute TTL: 1-30 递增

## 已测试功能

- ✅ ICMP Echo Request/Reply 基本通信
- ✅ ARP 解析和缓存
- ✅ 自定义 TTL 发送
- ✅ ICMP Time Exceeded 接收和解析
- ✅ RTT 时间戳提取
- ✅ Traceroute 状态机

## 可能的扩展

1. **多探测并行**: 同时发送多个探测包以提高准确性
2. **路径可视化**: 记录完整路径并绘制网络拓扑
3. **统计分析**: 计算丢包率、平均延迟等
4. **IPv6 支持**: 扩展到 ICMPv6
5. **GUI 界面**: 图形化显示 Traceroute 结果
