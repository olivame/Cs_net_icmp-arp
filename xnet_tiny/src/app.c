#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <conio.h>
#include "xnet_tiny.h"

#define MODE_IDLE       0
#define MODE_PING       1
#define MODE_TRACEROUTE 2
#define MODE_BANDWIDTH  3
#define MODE_JITTER     4

// 非阻塞等待最近一次 RTT，超时返回 -1
static int wait_for_reply(int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        xnet_poll();
        int rtt = xicmp_get_last_rtt();
        if (rtt >= 0) {
            return rtt;
        }
        Sleep(10);
        elapsed += 10;
    }
    return -1;
}

int main (void) {
    xnet_init();

    uint8_t dest_ip[4] = {0};
    char ip_str[32] = {0};
    int mode = MODE_IDLE;
    uint16_t seq = 0;

    printf("=== XNET Tiny ICMP Tools ===\n");
    printf("1. Ping\n");
    printf("2. Traceroute\n");
    printf("3. Bandwidth Estimation\n");
    printf("4. Jitter Measurement\n");
    printf("5. Exit\n");

    printf("\nSelect function (1-5): ");
    int choice = 0;
    if (scanf("%d", &choice) != 1 || choice < 1 || choice > 5) {
        printf("Invalid choice.\n");
        return 0;
    }

    if (choice == 5) {
        return 0;
    }

    printf("Enter Target IP (e.g. 192.168.232.128): ");
    if (scanf("%31s", ip_str) != 1) {
        printf("Invalid input.\n");
        return 0;
    }
    if (sscanf(ip_str, "%hhu.%hhu.%hhu.%hhu",
               &dest_ip[0], &dest_ip[1], &dest_ip[2], &dest_ip[3]) != 4) {
        printf("Invalid IP format.\n");
        return 0;
    }

    mode = choice; // 映射菜单选择
    if (mode == MODE_TRACEROUTE) {
        xicmp_traceroute_reset();
    }

    // 带宽/抖动测量专用变量
    int bw_sizes[] = {64, 256, 512, 1024};
    int bw_stage = 0;

    int jitter_count = 0;
    const int jitter_max_count = 20;
    int last_jitter_rtt = -1;
    double total_jitter = 0;
    int valid_jitter_samples = 0;

    // 通用计时器（毫秒）
    const int LOOP_DELAY_MS = 10;
    int ping_timer_ms = 0;
    int traceroute_timer_ms = 0;
    int traceroute_wait_ms = 0;
    uint8_t traceroute_ttl = 1;
    const uint8_t traceroute_max_hops = 30;
    int jitter_timer_ms = 0;

    printf("\nRunning Mode %d on %d.%d.%d.%d...\n", mode,
           dest_ip[0], dest_ip[1], dest_ip[2], dest_ip[3]);
    printf("Press ESC to exit.\n\n");

    while (1) {
        xnet_poll();

        if (_kbhit()) {
            int c = _getch();
            if (c == 27) break; // ESC 退出
        }

        switch (mode) {
            case MODE_PING:
                ping_timer_ms += LOOP_DELAY_MS;
                if (ping_timer_ms >= 1000) { // 每秒一次
                    ping_timer_ms = 0;
                    seq++;
                    int res = xicmp_ping(dest_ip, 1000, seq, 32);
                    if (res == 0) {
                        printf(">> Ping sent (seq=%u)\n", seq);
                    } else {
                        printf(">> Ping pending (ARP resolving...) seq=%u\n", seq);
                    }
                }
                break;

            case MODE_TRACEROUTE:
                traceroute_timer_ms += LOOP_DELAY_MS;
                if (traceroute_timer_ms >= 100) { // 100ms 级别的状态机
                    traceroute_timer_ms = 0;

                    if (xicmp_traceroute_is_complete()) {
                        printf("Traceroute complete!\n");
                        return 0;
                        mode = MODE_IDLE;
                        traceroute_wait_ms = 0;
                    } else if (xicmp_traceroute_has_hop_reply()) {
                        traceroute_ttl++;
                        traceroute_wait_ms = 0;

                        if (traceroute_ttl > traceroute_max_hops) {
                            printf("Max hops reached. Traceroute complete.\n");
                            mode = MODE_IDLE;
                        }
                    } else if (traceroute_wait_ms > 0) {
                        traceroute_wait_ms -= 100;
                        if (traceroute_wait_ms <= 0) {
                            printf("  * Request timed out (TTL=%u)\n", traceroute_ttl);
                            traceroute_ttl++;

                            if (traceroute_ttl > traceroute_max_hops) {
                                printf("Max hops reached. Traceroute complete.\n");
                                mode = MODE_IDLE;
                            }
                        }
                    } else {
                        if (traceroute_ttl <= traceroute_max_hops) {
                            seq++;
                            printf("Probe: TTL=%u, seq=%u\n", traceroute_ttl, seq);

                            int res = xicmp_traceroute_probe(dest_ip, 1000, seq, traceroute_ttl);
                            if (res == 0) {
                                traceroute_wait_ms = 3000;  // 等待这一跳 3 秒
                            } else {
                                printf(">> Traceroute probe pending (ARP resolving...)\n");
                            }
                        }
                    }
                }
                break;

            case MODE_BANDWIDTH:
                if (bw_stage < (int)(sizeof(bw_sizes) / sizeof(bw_sizes[0]))) {
                    int size = bw_sizes[bw_stage];
                    seq++;

                    xicmp_get_last_rtt(); // 清理旧值
                    int res = xicmp_ping(dest_ip, 2000, seq, (uint16_t)size);
                    if (res == 0) {
                        int rtt = wait_for_reply(2000);

                        if (rtt > 0) {
                            double kbps = (double)(size * 8) / (double)rtt;
                            printf("Payload %4d bytes: RTT=%d ms, Bandwidth=~%.2f kbps\n",
                                   size, rtt, kbps);
                        } else {
                            printf("Payload %4d bytes: Request Timed Out.\n", size);
                        }

                        bw_stage++;
                        Sleep(500); // 稍微间隔
                    } else {
                        Sleep(100); // 等待 ARP
                    }
                } else {
                    printf("\nBandwidth estimation complete.\n");
                    return 0;
                    mode = MODE_IDLE;
                }
                break;

            case MODE_JITTER:
                jitter_timer_ms += LOOP_DELAY_MS;
                if (jitter_timer_ms >= 500) { // 每 0.5 秒发一次
                    jitter_timer_ms = 0;

                    if (jitter_count < jitter_max_count) {
                        seq++;

                        xicmp_get_last_rtt(); // 清理旧值
                        xicmp_ping(dest_ip, 3000, seq, 64);

                        int rtt = wait_for_reply(1000);
                        if (rtt >= 0) {
                            if (last_jitter_rtt != -1) {
                                int diff = abs(rtt - last_jitter_rtt);
                                total_jitter += diff;
                                valid_jitter_samples++;
                                double avg_jitter = total_jitter / valid_jitter_samples;

                                printf("Seq=%d RTT=%d ms | Diff=%d ms | Avg Jitter=%.2f ms\n",
                                       jitter_count + 1, rtt, diff, avg_jitter);
                            } else {
                                printf("Seq=%d RTT=%d ms (First packet)\n",
                                       jitter_count + 1, rtt);
                            }
                            last_jitter_rtt = rtt;
                        } else {
                            printf("Seq=%d Timeout\n", jitter_count + 1);
                        }
                        jitter_count++;
                    } else {
                        printf("\nJitter measurement complete.\n");
                        mode = MODE_IDLE;
                    }
                }
                break;

            case MODE_IDLE:
            default:
                break;
        }

        Sleep(LOOP_DELAY_MS);
    }

    return 0;
}
