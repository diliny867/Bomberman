
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "config.h"


#define MAX_CLIENTS MAX_PLAYERS
#define PORT 12345


#define OUT_QUEUE_SIZE 1000
#define SHARED_QUEUE_SIZE  1000

typedef struct {
    uint8_t type;
    payload_t payload;
} packet_t;
typedef struct {
    msg_generic_t header;
    packet_t packet;
} packet_queue_entry_t;

int out_queue_count = 0;
packet_queue_entry_t out_queue[OUT_QUEUE_SIZE];

typedef struct {
    uint8_t game_status;

    uint16_t player_speed;
    uint16_t bomb_linger_ticks;
    uint8_t bomb_radius;
    uint16_t bomb_timer_ticks;

    uint8_t map_width;
    uint8_t map_height;
    uint8_t map[MAP_SIZE_MAX];

    uint16_t bombs_count;
    bomb_t bombs[MAP_SIZE_MAX];

    uint16_t player_count;
    player_t players[MAX_PLAYERS];
    int sockets[MAX_PLAYERS];

    int shared_queue_count = 0;
    packet_queue_entry_t shared_queue[SHARED_QUEUE_SIZE];
} shared_state_t;

shared_state_t *ss;


static inline void make_shared_memory() {
    ss = mmap(NULL, sizeof(shared_state_t), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
}


void read_map(char *name, uint8_t *width, uint8_t *height, uint8_t *map,
        uint16_t *player_speed, uint16_t *bomb_linger_ticks, uint8_t *bomb_radius, uint16_t *bomb_timer_ticks) {
    FILE *fin = fopen(name, "r");

    fscanf("%"SCNu8"%"SCNu8, width, height);
    fscanf("%"SCNu16"%"SCNu16"%"SCNu8"%"SCNu16, player_speed, bomb_linger_ticks, bomb_radius, bomb_timer_ticks);

    int size = width * height;
    for(int i = 0; i < size;) {
        int c = fgetc(fin);
        if(c == EOF) break;
        if(c == ' ') continue;
        map[i] = c;
        i++;
    }

    fclose(fin);
}

int id_to_player_i(int id) {
    return id;
}

int get_payload_size(uint8_t type) {
    if(type == MSG_HELLO) {
        return sizeof(payload_hello_t);
    }else if(type == MSG_WELCOME) {
        return sizeof(payload_welcome_t);
    }else if(type == MSG_ERROR) {
        return sizeof(payload_error_t);
    }else if(type == MSG_SET_STATUS) {
        return sizeof(payload_set_status_t);
    }else if(type == MSG_WINNER) {
        return sizeof(payload_winner_t);
    }else if(type == MSG_MAP) {
        return sizeof(payload_map_t);
    }else if(type == MSG_MOVE_ATTEMPT) {
        return sizeof(payload_move_attempt_t);
    }else if(type == MSG_MOVED) {
        return sizeof(payload_moved_t);
    }else if(type == MSG_BOMB_ATTEMPT) {
        return sizeof(payload_bomb_attempt_t);
    }else if(type == MSG_BOMB) {
        return sizeof(payload_bomb_t);
    }else if(type == MSG_EXPLOSION_START) {
        return sizeof(payload_explosion_start_t);
    }else if(type == MSG_EXPLOSION_END) {
        return sizeof(payload_explosion_end_t);
    }else if(type == MSG_DEATH) {
        return sizeof(payload_death_t);
    }else if(type == MSG_BONUS_AVAILABLE) {
        return sizeof(payload_bonus_available_t);
    }else if(type == MSG_BONUS_RETRIEVED) {
        return sizeof(payload_bonus_retrieved_t);
    }else if(type == MSG_BLOCK_DESTROYED) {
        return sizeof(payload_block_destroyed_t);
    }    
    return 0;
}

// extern int *sockets;
extern int server_socket;
void send_packet(uint8_t target_id, packet_t *packet) {
    void *data = &packet->type;
    int size = 1 + get_payload_size(packet->type);
    if(sender_id == 254) {
        for(int i = 0; i < ss->player_count; i++) {
            write(sockets[i], packet, size);
        }
    }else if(sender_id == 255) {
        write(server_socket, packet, size);
    }else {
        write(sockets[id_to_player_i(target_id)], packet, size);
    }
}


static inline int clampi_min(int val, int min) {
    return val < min ? min : val;
}
static inline int clampi_max(int val, int max) {
    return val > max ? max : val;
}
static inline int clampi(int val, int min, int max) {
    return clampi_max(clampi_min(val, min), max);
}
#define GET_I(x, y, width) ((y) * (width) + (x))

void cell_explode(int x, int y) {
    int i = GET_I(x, y, ss->map_width);
    switch(ss->map[i]) {
    case CELL_BOMB:
        for(int j = 0; j < ss->bombs_count; j++) {
            bomb_t *bj = ss->bombs + j;
            if(bj->row == x && bj->col == y && !bj->active) {
                bj->active = true;
                bj->timer_ticks = 0;
            }
        }
        break;
    case CELL_SOFT: case CELL_SPEEDUP: case CELL_RADIUSUP: case CELL_TICKUP:
        ss->map[i] = CELL_EMPTY;
        break; 
    case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': {
        int id = ss->map[i] - '1';

        } break;
    }
}

void tick(){
    for(int i = 0; i < ss->player_count; i++) {
        player_t *player = ss->players + i;
        if(player->move_ticks > 0)
            player->move_ticks--;
    }

    for(int i = 0; i < ss->bombs_count;) {
        bomb_t *bomb = ss->bombs + i;
        if(bomb->timer_ticks > 0) { // just tick
            bomb->timer_ticks--;
        }else {
            if(bomb->active) { // delete that bomb
                ss->map[GET_I(bomb->row, bomb->col, ss->map_width)] = CELL_EMPTY;
                memmove(bombs, bombs + 1, ss->bombs_count - i - 1);
                ss->bombs_count--;
                continue;
            }else {
                bomb->active = true;
                bomb->timer_ticks = ss->players[id_to_player_i(bomb->owner_id)].bomb_timer_ticks;

                // explode cells, also propagate bomb activation to other bombs
                int xs = clampi_min(bomb->row - bomb->radius, 0);
                int xf = clampi_max(bomb->row + bomb->radius, ss->map_width - 1);
                int ys = clampi_min(bomb->col - bomb->radius, 0);
                int yf = clampi_max(bomb->col + bomb->radius, ss->map_height - 1);
                for(int x = xs; x < xf; x++) {
                    if(x == bomb->row) continue;
                    cell_explode(x, bomb->col);
                }
                for(int y = ys; y < yf; y++) {
                    if(y == bomb->col) continue;
                    cell_explode(bomb->row, y);
                }
            }
        }
        i++;
    }

}

void main_loop() {
    printf("Loop:\n");

    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);

    int i = 0;
    while(1) {

        tick();

        for(int i = 0; i < out_queue_count; i++){
            send_packet(out_queue[i].header.target_id, out_queue[i].packet);
        }
        
        next.tv_nsec += 1000000000 / TICKS_PER_SECOND;
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
}

void process_client(int id, int socket) {
    printf("Processing client: %d %d\n", id, socket);
    printf("Client count: %d\n", ss->player_count);

    char in[1];
    char out[100];
    while(1) {
        read(socket, in, 1);
        // if(in[0] >= '0' && in[0] <= '9') {
        //     sprintf(out, "Client %d sum: %d\n", id, shared_data[MAX_CLIENTS + id]);
        //     write(socket, out, strlen(out));
        //     int num = (int)in[0] - '0';
        //     printf("Client %d read number: %d\n", id, num);
        //     shared_data[id] = num;
        // }
    }
}

void main_networking() {
    int main_socket;
    struct sockaddr_in server_address;
    int client_socket;
    struct sockaddr_in client_address;
    int client_address_size = sizeof(client_address);

    main_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(main_socket < 0) {
        printf("Error opening server socket\n");
        exit(1);
    }
    printf("Server socket created\n");

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);

    if(bind(main_socket, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
        printf("Error binding server socket\n");
        exit(1);
    }
    printf("Server socket binded\n");

    if(listen(main_socket, MAX_CLIENTS) < 0) {
        printf("Error listening to server socket\n");
        exit(1);
    }
    printf("Listening to server socket\n");

    while(1) {
        client_socket = accept(main_socket, (struct sockaddr*)&client_address, &client_address_size);
        if(client_socket < 0) {
            printf("Error accepting client: %d\n", errno);
            continue;
        }

        int new_client_id = ss->player_count++;
        int cpid = fork();

        if(cpid == 0) {
            close(main_socket);
            cpid = fork();
            if(cpid == 0) {
                process_client(new_client_id, client_socket);
                exit(0);
            }else{
                wait(NULL);
                printf("Orphaned client: %d\n", new_client_id);
                exit(0);
            }
        }else{
            close(client_socket);
        }
    }

}

void server() {
    make_shared_memory();

    read_map("main.map", &ss->map_width, &ss->map_height, &ss->map, &ss->player_speed, &ss->bomb_linger_ticks, &ss->bomb_radius, &ss->bomb_timer_ticks);

    int pid = fork();
    if(pid == 0) {
        main_networking();
    }else{
        main_loop();
    }    
}

int main(int argc, char **argv) {

    server();

    return 0;
}