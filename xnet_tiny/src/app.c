#include <stdio.h>
#include <windows.h>
#include "xnet_tiny.h"

#define MODE_PING       0
#define MODE_TRACEROUTE 1

int main (void) {
    xnet_init();

    // 目标 IP
    uint8_t dest_ip[4] = {192, 168, 232, 128}; 

    // 选择模式：0=Ping, 1=Traceroute
    int mode = 1;
    
    printf("xnet running. Target: %d.%d.%d.%d\n", 
            dest_ip[0], dest_ip[1], dest_ip[2], dest_ip[3]);
    
    if (mode == MODE_TRACEROUTE) {
        printf("Mode: TRACEROUTE\n\n");
        xicmp_traceroute_reset();  // Initialize traceroute state
    } else {
        printf("Mode: PING\n\n");
    }

    int timer_count = 0;
    uint16_t seq = 0; // Ping 的序列号，每次加 1
    
    // Traceroute state
    uint8_t traceroute_ttl = 1;
    uint8_t traceroute_max_hops = 30;
    uint8_t traceroute_probes_per_hop = 1;  // 每个 TTL 发送的探测包数
    uint8_t probe_count = 0;
    int wait_ticks = 0;
    int max_wait_ticks = 30;  // 等待约 3 秒（30 * 100ms）

    while (1) {
        // 1. 协议栈轮询 (收包、处理 ARP 回复、处理 Ping 回复)
        xnet_poll();

        if (mode == MODE_TRACEROUTE) {
            // Traceroute 模式
            timer_count++;
            
            if (timer_count >= 10) {  // 每 1 秒发送探测
                timer_count = 0;
                
                if (wait_ticks > 0) {
                    wait_ticks--;
                    
                    // 检查是否收到响应或超时
                    if (xicmp_traceroute_is_complete()) {
                        printf("Traceroute complete!\n");
                        mode = MODE_PING;  // 切换到 ping 模式或退出
                        wait_ticks = 0;
                    } else if (wait_ticks == 0) {
                        // 超时，进入下一跳
                        printf("  * Request timed out (TTL=%u)\n", traceroute_ttl);
                        probe_count = 0;
                        traceroute_ttl++;
                        
                        if (traceroute_ttl > traceroute_max_hops) {
                            printf("Max hops reached. Traceroute complete.\n");
                            mode = MODE_PING;
                        }
                    }
                } else {
                    // 发送新的探测包
                    if (traceroute_ttl <= traceroute_max_hops && !xicmp_traceroute_is_complete()) {
                        seq++;
                        printf("Probe: TTL=%u, seq=%u\n", traceroute_ttl, seq);
                        
                        int res = xicmp_traceroute_probe(dest_ip, 1000, seq, traceroute_ttl);
                        
                        if (res == 0) {
                            wait_ticks = max_wait_ticks;
                            probe_count++;
                            
                            if (probe_count >= traceroute_probes_per_hop) {
                                probe_count = 0;
                                traceroute_ttl++;
                            }
                        } else {
                            printf(">> Traceroute probe pending (ARP resolving...)\n");
                        }
                    }
                }
            }
        } else {
            // 2. Ping 模式定时发送逻辑
            // Sleep(100) 意味着循环约 100ms 一次
            // 累加到 50 次 (50 * 100ms = 5秒) 发送一次 Ping
            timer_count++;
            if (timer_count >= 50) {
                timer_count = 0;
                seq++;

                // 调用写好的 xicmp_ping
                // 如果 ARP 未就绪，它返回 -1 (并自动发 ARP 请求)
                // 如果 ARP 就绪，它返回 0 (并发送 ICMP Request)
                int res = xicmp_ping(dest_ip, 1000, seq);

                if (res == 0) {
                    printf(">> Ping sent (seq=%d)\n", seq);
                } else {
                    printf(">> Ping pending (ARP resolving...) seq=%d\n", seq);
                }
            }
        }

        // 3. 休眠防止 CPU 占用过高
        Sleep(100); 
    }
    return 0;
}