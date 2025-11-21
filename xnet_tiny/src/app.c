#include <stdio.h>
#include <windows.h>
#include "xnet_tiny.h"

#define MODE_PING       0
#define MODE_TRACEROUTE 1

int main (void) {
    xnet_init();

    // 目标 IP（虚拟机）
    uint8_t dest_ip[4] = {192, 168, 232, 128}; 

    // 选择模式：0=Ping, 1=Traceroute
    int mode = MODE_TRACEROUTE;
    
    printf("xnet running. Target: %d.%d.%d.%d\n", 
           dest_ip[0], dest_ip[1], dest_ip[2], dest_ip[3]);
    
    if (mode == MODE_TRACEROUTE) {
        printf("Mode: TRACEROUTE\n\n");
        xicmp_traceroute_reset();  // 初始化 traceroute 状态
    } else {
        printf("Mode: PING\n\n");
    }

    int timer_count = 0;
    uint16_t seq = 0;        // ICMP 序列号

    // Traceroute 状态
    uint8_t traceroute_ttl = 1;
    const uint8_t traceroute_max_hops = 30;
    int wait_ticks = 0;
    const int max_wait_ticks = 30;  // 约 3 秒（30 * 100ms）

    while (1) {
        // 1. 协议栈轮询 (收包、处理 ARP / ICMP 等)
        xnet_poll();

        if (mode == MODE_TRACEROUTE) {
            // ===== Traceroute 模式 =====
            timer_count++;

            // 每 1 秒驱动一次 traceroute 状态机
            if (timer_count >= 10) {
                timer_count = 0;

                // 1) 整个 traceroute 是否已经完成（到达目的主机或不可达）
                if (xicmp_traceroute_is_complete()) {
                    printf("Traceroute complete!\n");
                    return 0;   // 退出程序
                    mode = MODE_PING;       // 结束后切回 Ping
                    wait_ticks = 0;
                } else {
                    // 2) 当前这一跳是否已经收到 Time Exceeded
                    if (xicmp_traceroute_has_hop_reply()) {
                        // 本 TTL 已经有路由器回应，切到下一跳
                        traceroute_ttl++;
                        wait_ticks = 0;

                        if (traceroute_ttl > traceroute_max_hops) {
                            printf("Max hops reached. Traceroute complete.\n");
                            mode = MODE_PING;
                        }
                    }
                    // 3) 还在等待当前 TTL 的回应
                    else if (wait_ticks > 0) {
                        wait_ticks--;

                        if (wait_ticks == 0) {
                            // 等待超时：这一跳一个包都没回来
                            printf("  * Request timed out (TTL=%u)\n", traceroute_ttl);
                            traceroute_ttl++;

                            if (traceroute_ttl > traceroute_max_hops) {
                                printf("Max hops reached. Traceroute complete.\n");
                                mode = MODE_PING;
                            }
                        }
                    }
                    // 4) 不在等待任何 TTL，说明可以发新的探测包
                    else {
                        if (traceroute_ttl <= traceroute_max_hops) {
                            seq++;
                            printf("Probe: TTL=%u, seq=%u\n", traceroute_ttl, seq);

                            int res = xicmp_traceroute_probe(dest_ip, 1000, seq, traceroute_ttl);
                            if (res == 0) {
                                // 发送成功，开始等待这一跳的回复（Time Exceeded 或 Echo Reply）
                                wait_ticks = max_wait_ticks;
                            } else {
                                // ARP 还没好，等下一轮再发
                                printf(">> Traceroute probe pending (ARP resolving...)\n");
                            }
                        }
                    }
                }
            }
        } else {
            // ===== Ping 模式：每 5 秒发一个 Echo Request =====
            timer_count++;
            if (timer_count >= 50) {            // 50 * 100ms = 5 秒
                timer_count = 0;
                seq++;

                int res = xicmp_ping(dest_ip, 1000, seq);

                if (res == 0) {
                    printf(">> Ping sent (seq=%d)\n", seq);
                } else {
                    printf(">> Ping pending (ARP resolving...) seq=%d\n", seq);
                }
            }
        }

        // 3. 休眠，降低 CPU 占用
        Sleep(100); 
    }

    return 0;
}
