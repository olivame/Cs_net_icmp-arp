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
    double bw_results[4] = {0};
    int bw_stage = 0;

    int jitter_count = 0;
    const int jitter_max_count = 20;
    int jitter_seqs[20] = {0};
    int jitter_rtts[20] = {0};
    double jitter_vals[20] = {0};
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
                            bw_results[bw_stage] = kbps;
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

                    FILE *fp = fopen("plot_bw.py", "w");
                    if (fp) {
                        fprintf(fp, "import matplotlib.pyplot as plt\n");
                        fprintf(fp, "sizes = ['64', '256', '512', '1024']\n");
                        fprintf(fp, "kbps = [%.2f, %.2f, %.2f, %.2f]\n", bw_results[0], bw_results[1], bw_results[2], bw_results[3]);
                        fprintf(fp, "plt.figure(figsize=(10, 6))\n");
                        fprintf(fp, "plt.bar(sizes, kbps, color='skyblue', edgecolor='black')\n");
                        fprintf(fp, "plt.title('ICMP Bandwidth Estimation')\n");
                        fprintf(fp, "plt.xlabel('Packet Size (bytes)')\n");
                        fprintf(fp, "plt.ylabel('Bandwidth (kbps)')\n");
                        fprintf(fp, "for i, v in enumerate(kbps):\n");
                        fprintf(fp, "    plt.text(i, v + 1, f'{v:.2f} kbps', ha='center')\n");
                        fprintf(fp, "plt.show()\n");
                        fclose(fp);
                        system("python plot_bw.py");
                        remove("plot_bw.py");
                    }

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
                        jitter_seqs[jitter_count] = jitter_count + 1;
                        if (rtt >= 0) {
                            jitter_rtts[jitter_count] = rtt;
                            if (last_jitter_rtt != -1) {
                                int diff = abs(rtt - last_jitter_rtt);
                                total_jitter += diff;
                                valid_jitter_samples++;
                                double avg_jitter = total_jitter / valid_jitter_samples;

                                printf("Seq=%d RTT=%d ms | Diff=%d ms | Avg Jitter=%.2f ms\n",
                                       jitter_count + 1, rtt, diff, avg_jitter);
                                jitter_vals[jitter_count] = (double)diff;
                            } else {
                                printf("Seq=%d RTT=%d ms (First packet)\n",
                                       jitter_count + 1, rtt);
                                jitter_vals[jitter_count] = 0.0;
                            }
                            last_jitter_rtt = rtt;
                        } else {
                            printf("Seq=%d Timeout\n", jitter_count + 1);
                            jitter_rtts[jitter_count] = 0;
                            jitter_vals[jitter_count] = 0.0;
                        }
                        jitter_count++;
                    } else {
                        printf("\nJitter measurement complete.\n");

                        FILE *fp = fopen("plot_jitter.py", "w");
                        if (fp) {
                            fprintf(fp, "import matplotlib.pyplot as plt\n");
                            
                            fprintf(fp, "seqs = [");
                            for(int i=0; i<jitter_count; i++) fprintf(fp, "%d,", jitter_seqs[i]);
                            fprintf(fp, "]\n");
                            
                            fprintf(fp, "rtts = [");
                            for(int i=0; i<jitter_count; i++) fprintf(fp, "%d,", jitter_rtts[i]);
                            fprintf(fp, "]\n");
                            
                            fprintf(fp, "jitters = [");
                            for(int i=0; i<jitter_count; i++) fprintf(fp, "%.2f,", jitter_vals[i]);
                            fprintf(fp, "]\n");

                            fprintf(fp, "plt.figure(figsize=(10, 8))\n");
                            
                            fprintf(fp, "plt.subplot(2, 1, 1)\n");
                            fprintf(fp, "plt.plot(seqs, rtts, marker='o', color='blue', label='RTT')\n");
                            fprintf(fp, "if len(rtts) > 0: plt.axhline(y=sum(rtts)/len(rtts), color='r', linestyle='--', label=f'Avg RTT: {sum(rtts)/len(rtts):.2f} ms')\n");
                            fprintf(fp, "plt.title('ICMP Network Quality - RTT Variation')\n");
                            fprintf(fp, "plt.ylabel('Round Trip Time (ms)')\n");
                            fprintf(fp, "plt.legend()\n");
                            fprintf(fp, "plt.grid(True, linestyle='--', alpha=0.7)\n");
                            
                            fprintf(fp, "plt.subplot(2, 1, 2)\n");
                            fprintf(fp, "plt.bar(seqs, jitters, color='orange', alpha=0.7, label='Jitter')\n");
                            fprintf(fp, "if len(jitters) > 0: plt.axhline(y=sum(jitters)/len(jitters), color='purple', linestyle='--', label=f'Avg Jitter: {sum(jitters)/len(jitters):.2f} ms')\n");
                            fprintf(fp, "plt.title('Jitter Variation')\n");
                            fprintf(fp, "plt.xlabel('Packet Sequence')\n");
                            fprintf(fp, "plt.ylabel('Jitter (ms)')\n");
                            fprintf(fp, "plt.legend()\n");
                            fprintf(fp, "plt.grid(True, linestyle='--', alpha=0.7)\n");
                            
                            fprintf(fp, "plt.tight_layout()\n");
                            fprintf(fp, "plt.show()\n");
                            
                            fclose(fp);
                            system("python plot_jitter.py");
                            remove("plot_jitter.py");
                        }

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
