#include <stdio.h>
#include <windows.h>
#include "xnet_tiny.h"

int main (void) {
    xnet_init();

    // 目标 IP
    uint8_t dest_ip[4] = {192, 168, 108, 136}; 
    
    printf("xnet running. Target: %d.%d.%d.%d\n", 
            dest_ip[0], dest_ip[1], dest_ip[2], dest_ip[3]);

    int timer_count = 0;
    uint16_t seq = 0; // Ping 的序列号，每次加 1

    while (1) {
        // 1. 协议栈轮询 (收包、处理 ARP 回复、处理 Ping 回复)
        xnet_poll();

        // 2. 定时发送逻辑
        // Sleep(100) 意味着循环约 100ms 一次
        // 累加到 10 次 (10 * 100ms = 1秒) 发送一次 Ping
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

        // 3. 休眠防止 CPU 占用过高
        Sleep(100); 
    }
    return 0;
}