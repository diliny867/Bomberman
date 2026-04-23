#include "client.h"

#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <arpa/inet.h>


static uint8_t my_id = 0;
static int server_socket = 0;

static uint8_t map_width = 0;
static uint8_t map_height = 0;
static uint8_t map[MAP_SIZE_MAX];

static uint8_t game_status = GAME_LOBBY;

static int player_count = 0;
static player_t players[MAX_PLAYERS] = { 0 };

static uint8_t plis[256] = { 0 };
#define pli(id) plis[id]


void client_send_simple(uint8_t msg_type) {
    send_simple(server_socket, msg_type, my_id, 255);
}

void send_try_bomb(uint16_t coord) {
    packet_t p;
    p.header = make_header(MSG_BOMB_ATTEMPT, my_id, 255);
    p.bomb_attempt.coord = coord;
    send_packet_simple(server_socket, &p);
}

void send_try_move(uint8_t direction) {
    packet_t p;
    p.header = make_header(MSG_MOVE_ATTEMPT, my_id, 255);
    p.move_attempt.direction = direction;
    send_packet_simple(server_socket, &p);
}

int handle_packet(uint8_t msg_type, uint8_t sender_id, uint8_t target_id, payload_t *payload) {
    my_id = target_id;
    plis[my_id] = 0;

    switch(msg_type) {
    case MSG_DISCONNECT: return 1;
    case MSG_PING: { 
        send_ping(server_socket, sender_id, my_id, true);
    } break;
    case MSG_PONG: { 

    } break;
    case MSG_WELCOME: {
        payload_welcome_t *p = (payload_welcome_t*)payload;
        game_status = p->game_status;
        player_count = 1;
        for(int i = 0; i < p->player_count) {
            plis[p->players[i].id] = player_count++;
            int index = pli(p->players[i].id);
            players[index].id = p->players[i].id;
            players[index].ready = p->players[i].ready;
            strcpy(players[index].name, p->players[i].name);
        }
    } break;
    case MSG_LEAVE: {
        for(int i = 0; i < MAX_PLAYERS; i++) {
            if(players[i].id == sender_id) {
                players[i].id = 0;
                player_count--;
                break;
            }
        }
    } break;
    case MSG_ERROR: {
        payload_error_t *p = (payload_error_t*)payload;

    } break;
    case MSG_MAP: {
        payload_map_t *p = (payload_map_t*)payload;
        map_width  = p->width;
        map_height = p->height;
        memcpy(map, p->map, map_width * map_height * sizeof(uint8_t));
    } break;
    case MSG_SET_READY : {
        client_send_simple(MSG_SET_READY);
    } break;
    case MSG_SET_STATUS : {
        payload_set_status_t *p = (payload_set_status_t*)payload;
        game_status = p->game_status;
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

void random_str(char *name, int count) {
    assert(count < 30);
    name[count] = '\0';
    for(int i = 0; i < count; i++) {
        name[i] = '0' + rand() % ('z' - '0');
    }
}

void client(char *name, char *ip, int port) {
    srand(time(0));

    char _tmp_name[30];
    if(name == NULL) {
        name = _tmp_name;
        random_str(name, 20);
    }

    log("name: %s version: %s\n", name, VERSION);

    struct sockaddr_in server_addr;

    // 1. Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        return;
    }

    // 2. Setup address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(server_socket);
        return;
    }

    // 3. Connect
    if (connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(server_socket);
        return;
    }

    log("Connected to server %s:%d\n", ip, port);

    packet_t p;
    p.header = make_header(MSG_HELLO, my_id, 255);
    strcpy(p.payload.hello.version, VERSION);
    strcpy(p.payload.hello.name, name);
    send_packet_simple(server_socket, &p);

    uint8_t msg_type, sender_id, target_id;
    payload_t *payload;
    assert(sizeof(packet_t) <= 100000);
    char in[100000]; // Something bad will happen on too long error text
    // 4. Receive loop
    while (1) {
        // Handle Input

        


        // wait for socket or timeouts
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(server_socket, &rfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 1000 };
        int ready = select(server_socket + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) return;
        if (ready == 0) continue;  // timeout


        log("Client read:\n");
        ssize_t n = read(server_socket, in, sizeof(in));
        if (n <= 0) break;
        log("Read from server %ld bytes\n", n);

        msg_type  = in[0];
        sender_id = in[1];
        target_id = in[2];
        payload   = (payload_t*)(in + sizeof(msg_generic_t));
        log("msg_type: %hhu sender_id: %hhu target_id: %hhu\n", msg_type, sender_id, target_id);

        if(handle_packet(msg_type, sender_id, target_id, payload)) break;
    }

    log("Client exit\n");

    close(server_socket);
}
