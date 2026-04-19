
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


#define OUT_QUEUE_SIZE     100
#define SHARED_QUEUE_SIZE  100

typedef struct {
    msg_generic_t header;
    payload_t payload;
} packet_t;

int out_queue_count = 0;
packet_t out_queue[OUT_QUEUE_SIZE];

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

    int shared_queue_count;
    packet_t shared_queue[SHARED_QUEUE_SIZE];

    int start_timeout;
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

static inline int celli_to_id(int i){
    return ss->map[i] - '1';
}
static inline int id_to_celli(int id){
    return id + '1';
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

void _push_payload(packet_t *queue, int *queue_count, int max_size, uint8_t msg_type, uint8_t sender_id, uint8_t target_id, payload_t *payload, int payload_size) {
    if(*queue_count >= max_size) return;
    queue[*queue_count].header.msg_type  = msg_type;
    queue[*queue_count].header.sender_id = sender_id;
    queue[*queue_count].header.target_id = target_id;
    memcpy(&queue[*queue_count].payload, payload, payload_size);
    (*queue_count)++;
}
void push_payload(uint8_t msg_type, uint8_t sender_id, uint8_t target_id, payload_t *payload) {
    _push_payload(out_queue, out_queue_count, OUT_QUEUE_SIZE, msg_type, sender_id, target_id, payload, get_payload_size(msg_type));
}
void push_shared(uint8_t msg_type, uint8_t sender_id, uint8_t target_id, payload_t *payload) {
    _push_payload(shared_queue, shared_queue_count, SHARED_QUEUE_SIZE, msg_type, sender_id, target_id, payload, get_payload_size(msg_type));
}

// extern int *sockets;
extern int server_socket;
void send_packet(packet_t *packet) {
    void *data = packet;
    int size = 1 + get_payload_size(packet->header.msg_type);
    if(packet->header.target_id == 254) {
        for(int i = 0; i < ss->player_count; i++) {
            write(sockets[i], packet, size);
        }
    }else if(packet->header.target_id == 255) {
        write(server_socket, packet, size);
    }else {
        write(sockets[packet->header.target_id], packet, size);
    }
}

void send_error(int target_id, char *text){
    packet_t packet;
    packet.header.msg_type = MSG_ERROR;
    packet.header.sender_id = 255;
    packet.header.target_id = target_id;
    packet.payload.error = text;
    send_packet(packet.header.target_id, packet);
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

                payload_explosion_start_t p;
                p.radius = bj->radius;
                p.coord = GET_I(bj->row, bj->col, ss->map_width);
                push_payload(MSG_EXPLOSION_START, 255, 254, &p);
            }
        }
        break;
    case CELL_SOFT: case CELL_SPEEDUP: case CELL_RADIUSUP: case CELL_TICKUP: {
        ss->map[i] = CELL_EMPTY;
        
        payload_block_destroyed_t p;
        p.coord = i;
        push_payload(MSG_BLOCK_DESTROYED, 255, 254, &p);
        } break; 
    case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': {
        int id = celli_to_id(i);

        payload_death_t p;
        p.death_id = id;
        push_payload(MSG_DEATH, 255, 254, &p);

        } break;
    }
}

void tick(){
    int alive_count = 0;
    int last_id = 0; 
    for(int i = 0; i < ss->player_count; i++) {
        player_t *player = ss->players + i;
        if(!player->alive) continue;
        alive_count++;
        last_id = player->id;
        if(player->move_ticks > 0)
            player->move_ticks--;
    }
    if(alive_count == 1){
        payload_winner_t p;
        p->id = last_id;
        push_payload(MSG_WINNER, 255, 254, &p);
        ss->game_status = GAME_END;
        return;
    }

    for(int i = 0; i < ss->bombs_count;) {
        bomb_t *bomb = ss->bombs + i;
        if(bomb->timer_ticks > 0) { // just tick
            bomb->timer_ticks--;
        }else {
            if(bomb->active) { // delete that bomb
                int bi = GET_I(bomb->row, bomb->col, ss->map_width);
                ss->map[bi] = CELL_EMPTY;
                memmove(bombs, bombs + 1, ss->bombs_count - i - 1);
                ss->bombs_count--;

                payload_explosion_end_t p;
                p.bomb_coord = bi;
                push_payload(MSG_EXPLOSION_END, 255, 254, &p);

                continue;
            }else {
                bomb->active = true;
                bomb->timer_ticks = ss->players[bomb->owner_id].bomb_timer_ticks;

                payload_explosion_start_t p;
                p.radius = bomb->radius;
                p.bomb_coord = bi;
                push_payload(MSG_EXPLOSION_START, 255, 254, &p);

                // explode cells, also propagate bomb activation to other bombs
                int xs = clampi_min((int)bomb->row - (int)bomb->radius, 0);
                int xf = clampi_max((int)bomb->row + (int)bomb->radius, (int)ss->map_width - 1);
                int ys = clampi_min((int)bomb->col - (int)bomb->radius, 0);
                int yf = clampi_max((int)bomb->col + (int)bomb->radius, (int)ss->map_height - 1);
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

// process input queue from clients -> tick -> send output queue to clients
void main_loop() {
    printf("Loop:\n");

    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    next.tv_nsec += 1000000000 / TICKS_PER_SECOND;

    int i = 0;
    while(1) {
        if(ss->game_status == GAME_LOBBY) {
            for(int i = 0; i < ss->player_count; i++){
                if(ss->start_timeout <= 0){
                    ss->game_status = GAME_RUNNING;
                    ss->start_timeout = 0;
                }else{
                    ss->start_timeout--;
                }
            }
        }else if(ss->game_status == GAME_END) {
            continue;
        }

        ss->out_queue_count = 0; // clear out queue

        for(int i = 0; i < ss->shared_queue_count; i++){
            packet_t *packet = ss->shared_queue + i;
            payload_t *payload = &packet.payload;
            player_t *player = ss->players[packet->sender_id];
            int x = player->row;
            int y = player->col;
            switch(packet->msg_type){
            case MSG_HELLO: {

            } break;
            case MSG_WELCOME: {

            } break;
            case MSG_DISCONNECT: {

            } break;
            case MSG_PING: {

            } break;
            case MSG_PONG: {

            } break;
            case MSG_LEAVE: {

            } break;
            case MSG_SET_READY: {

            } break;
            case MSG_MOVE_ATTEMPT: {
                if(player->move_ticks > 0 || !player->alive) 
                    break;

                uint8_t dir = payload->move_attempt.direction;
                int xd = x + (dir == DIR_RIGHT) - (dir == DIR_LEFT); 
                int yd = y - (dir == DIR_UP) + (dir == DIR_DOWN);
                if(xd < 0 || xd >= ss->map_width || yd < 0 || yd >= ss->map_height) 
                    break;

                int i = GET_I(xd, yd, ss->map_width);
                int pi = GET_I(x, y, ss->map_width);
                if(ss->map[i] == CELL_EMPTY) {
                    ss->map[i] = id_to_celli(player->id);
                    uint8_t cell = ss->map[i];
                    if(cell == CELL_SPEEDUP || cell == CELL_RADIUSUP || cell == CELL_TICKUP){
                        payload_bonus_retrieved_t p;
                        p.player_id = player->id;
                        p.coord = i;
                        push_payload(MSG_BONUS_RETRIEVED, 255, 254, &p);
                    }
                    if(ss->map[pi] != CELL_BOMB) {
                        ss->map[pi] = CELL_EMPTY;
                    }

                    payload_moved_t p;
                    p.player_id = player->id;
                    p.coord = i;
                    push_payload(MSG_MOVED, 255, 254, &p);
                }
            } break;
            case MSG_BOMB_ATTEMPT: {
                if(!player->alive) break;
                int i = payload->bomb_attempt->coord;
                int pi = GET_I(x, y, ss->map_width);
                if(celli_to_id(i) == player->id) {
                    ss->map[i] = CELL_BOMB;

                    payload_bomb_t p;
                    p.player_id = player->id;
                    p.coord = i;
                    push_payload(MSG_BOMB, 255, 254, &p);
                }
            } break;
            case MSG_SET_READY: case MSG_SET_STATUS:  
            case MSG_WINNER: case MSG_DEATH: case MSG_MAP: case MSG_ERROR: 
            case MSG_MOVED: case MSG_BOMB: case MSG_EXPLOSION_START: case MSG_EXPLOSION_END: 
            case MSG_BONUS_AVAILABLE: case MSG_BONUS_RETRIEVED: case MSG_BLOCK_DESTROYED: break; // server dont responds to these 
            }
        }

        tick();

        for(int i = 0; i < out_queue_count; i++){
            send_packet(out_queue[i].header.target_id, out_queue[i].packet);
        }
        
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        next.tv_nsec += 1000000000 / TICKS_PER_SECOND;
    }
}

void process_client(int id, int socket) {
    printf("Processing client: %d %d\n", id, socket);
    printf("Client count: %d\n", ss->player_count);

    uint8_t msg_type, sender_id, target_id;
    payload_t *payload;
    char in[sizeof(packet)];
    char out[100];
    while(1) {
        read(socket, in, sizeof(packet));
        msg_type  = in[0];
        sender_id = in[1];
        target_id = in[2];
        payload   = in + 3;

        switch(msg_type){
        case MSG_SET_READY:
            ss->players[sender_id].ready = true;

            break;
        case MSG_SET_STATUS:
            if(ss->game_status != GAME_END){
                send_error(sender_id, "wtf?");
                break;
            }

            ss->game_status = ((payload_set_status_t*)payload)->game_status;

            break;
        default: // process all others separately, keeping timeline during tick valid
            push_shared(msg_type, sender_id, target_id, payload);
        }
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

    ss->start_timeout = 100;

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