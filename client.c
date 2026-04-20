#include "client.h"
#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

void client() {
    int sock;
    struct sockaddr_in server_addr;

    // 1. Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return;
    }

    // 2. Setup address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return;
    }

    // 3. Connect
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        return;
    }

    printf("Connected to server on port %d\n", PORT);

    // 🔹 OPTIONAL: send version / handshake
    send(sock, VERSION, strlen(VERSION), 0);

    // 4. Receive loop
    while (1) {
        uint8_t header[2];

        int r = recv(sock, header, 2, MSG_WAITALL);
        if (r <= 0) break;

        uint8_t type = header[0];
        uint8_t size = header[1];

        uint8_t payload[1024];

        if (size > 0) {
            recv(sock, payload, size, MSG_WAITALL);
        }

        printf("Received packet type=%d size=%d\n", type, size);
    }

    close(sock);
}
