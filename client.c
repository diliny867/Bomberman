#include "client.h"

#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <arpa/inet.h>


uint8_t my_id = 0;
int server_socket = 0;

uint8_t map_width = 0;
uint8_t map_height = 0;
uint8_t map[MAP_SIZE_MAX];


int handle_packet(uint8_t msg_type, uint8_t sender_id, uint8_t target_id, payload_t *payload) {
    my_id = target_id;

    switch(msg_type) {
    case MSG_DISCONNECT: return 1;
    case MSG_PING: { 
        send_ping(server_socket, sender_id, my_id, true);
    } break;
    case MSG_PONG: { 

    } break;
    case MSG_WELCOME: {
        payload_welcome_t *p = (payload_welcome_t*)payload;

    } break;
    case MSG_LEAVE: {

    } break;
    case MSG_ERROR: {
        payload_error_t *p = (payload_error_t*)payload;

    } break;
    case MSG_MAP: {
        payload_map_t *p = (payload_map_t*)payload;
        
    } break;
    case MSG_SET_READY : {

    } break;
    case MSG_SET_STATUS : {
        payload_set_status_t *p = (payload_set_status_t*)payload;

    } break;
    case MSG_WINNER : {
        payload_winner_t *p = (payload_winner_t*)payload;

    } break;
    case MSG_MOVED : {
        payload_moved_t *p = (payload_moved_t*)payload;

    } break;
    case MSG_BOMB : {
        payload_bomb_t *p = (payload_bomb_t*)payload;

    } break;
    case MSG_EXPLOSION_START : {
        payload_explosion_start_t *p = (payload_explosion_start_t*)payload;

    } break;
    case MSG_EXPLOSION_END : {
        payload_explosion_end_t *p = (payload_explosion_end_t*)payload;

    } break;
    case MSG_DEATH : {
        payload_death_t *p = (payload_death_t*)payload;

    } break;
    case MSG_BONUS_AVAILABLE : {
        payload_bonus_available_t *p = (payload_bonus_available_t*)payload;

    } break;
    case MSG_BONUS_RETRIEVED : {
        payload_bonus_retrieved_t *p = (payload_bonus_retrieved_t*)payload;

    } break;
    case MSG_BLOCK_DESTROYED : {
        payload_block_destroyed_t *p = (payload_block_destroyed_t*)payload;

    } break;
    // case MSG_HELLO: case MSG_MOVE_ATTEMPT: case MSG_BOMB_ATTEMPT: client ignores these
    default: break;
    }

    return 0;
}


void client(const char *ip, int port) {
    struct sockaddr_in server_addr;

    // 1. Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return;
    }

    // 2. Setup address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
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

    printf("Connected to server %s:%d\n", ip, port);

    send_simple(server_socket, MSG_HELLO, my_id, 255);

    uint8_t msg_type, sender_id, target_id;
    payload_t *payload;
    assert(1024 >= sizeof(packet_t));
    char in[1024]; // Something bad will happen on too long error text
    // 4. Receive loop
    while (1) {
        ssize_t n = recv(sock, in, sizeof(in), MSG_WAITALL);
        if (n <= 0) break;

        msg_type  = in[0];
        sender_id = in[1];
        target_id = in[2];
        payload   = (payload_t*)(in + sizeof(msg_generic_t));
        printf("msg_type: %hhu sender_id: %hhu target_id: %hhu\n", msg_type, sender_id, target_id);

        if(handle_packet(msg_type, sender_id, target_id, payload)) break;

        // Handle Input

    }

    close(sock);
}
