#include <stdio.h>
#include <windows.h>
#include "xnet_tiny.h"

int main (void) {
    xnet_init();

    uint8_t ip_list[][4] = {
        {192, 168, 108, 132},   // 虚拟机
        //{192, 168, 108, 1},     // 宿主机
    };
    int ip_count = 1;

    printf("xnet running\n");
    while (1) {
        for (int i = 0; i < ip_count; i++) {
            const uint8_t *mac = arp_resolve(ip_list[i]);
            /*if (mac) {
                printf("ARP resolved[%d]: %d.%d.%d.%d -> %02X:%02X:%02X:%02X:%02X:%02X\n",
                       i,
                       ip_list[i][0], ip_list[i][1], ip_list[i][2], ip_list[i][3],
                       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            }*/
        }
        xnet_poll();
        Sleep(100);
    }
}
