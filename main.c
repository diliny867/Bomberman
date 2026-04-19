
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


#define PORT 12345
#define VERSION "aboba1"


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
    uint8_t player_max_bombs;

    uint8_t map_width;
    uint8_t map_height;
    uint8_t map[MAP_SIZE_MAX];
    uint16_t start_coords[MAX_PLAYERS];

    uint16_t bombs_count;
    bomb_t bombs[MAP_SIZE_MAX];

    uint16_t player_count;
    player_t players[MAX_PLAYERS];
    int sockets[MAX_PLAYERS];

    uint8_t plis[255];
    uint8_t next_id;

    int shared_queue_count;
    packet_t shared_queue[SHARED_QUEUE_SIZE];

    int start_timeout;

} shared_state_t;

shared_state_t *ss;

int pings[MAX_PLAYERS + 1]; // at MAX_PLAYERS is server

#define GET_I(x, y, width) ((y) * (width) + (x))
#define pli(id) ss->plis[(id)]

#define arr_erase(arr, i, count) memmove((arr), (arr) + 1, ((count) - (i) - 1) * sizeof(arr[0]))


static inline void make_shared_memory() {
    ss = mmap(NULL, sizeof(shared_state_t), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
}


void read_map(char *name) {
    FILE *fin = fopen(name, "r");

    fscanf("%"SCNu8"%"SCNu8, ss->map_width, ss->map_height);
    fscanf("%"SCNu16"%"SCNu16"%"SCNu8"%"SCNu16, ss->player_speed, ss->bomb_linger_ticks, ss->bomb_radius, ss->bomb_timer_ticks);

    int size = ss->map_width * ss->map_height;
    for(int i = 0; i < size;) {
        int c = fgetc(fin);
        if(c == EOF) break;
        if(c == ' ') continue;
        if(c >= '1' && c <= '8'){
            ss->start_coords[c - '1'] = i;
            ss->map[i] = CELL_EMPTY;
        }else{
            ss->map[i] = c;
        }
        i++;
    }

    fclose(fin);
}

static inline int celli_to_i(int i){
    return ss->map[i] - '1';
}
static inline int id_to_celli(int id){
    return pli(id) + '1';
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
    if(payload)
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
// extern int server_socket;
void send_packet(packet_t *packet) {
    void *data = packet;
    int size = 3 + get_payload_size(packet->header.msg_type);
    if(packet->header.target_id == 254) {
        for(int i = 0; i < MAX_PLAYERS; i++) {
            if(ss->players[i].id == 0) continue
            write(ss->sockets[i], data, size);
        }
    }else if(packet->header.target_id == 255) {
        // write(server_socket, data, size);
    }else {
        write(ss->sockets[pli(packet->header.target_id)], data, size);
    }
}

void send_error(int target_id, char *text) {
    packet_t packet;
    packet.header.msg_type = MSG_ERROR;
    packet.header.sender_id = 255;
    packet.header.target_id = target_id;
    packet.payload.error = text;
    send_packet(packet);
}

void send_simple(int msg_type, int target_id) {
    packet_t packet;
    packet.header.msg_type = msg_type;
    packet.header.sender_id = 255;
    packet.header.target_id = target_id;
    send_packet(packet);
}

void send_pong(int target_id, bool send_pong) {
    send_simple(send_pong ? MSG_PONG : MSG_PING, target_id);
}

void change_game_status(uint8_t status) {
    if(ss->game_status == status) return;
    ss->game_status = status;
    payload_set_status_t p;
    p.game_status = status;
    push_payload(MSG_SET_STATUS, 255, 254, &p);
}

bomb_t make_bomb(player_t *player) {
    bomb_t bomb;
    bomb.active = false;
    bomb.owner_id = player->id;
    bomb.row = player->row;
    bomb.col = player->col;
    bomb.radius = player->bomb_radius;
    bomb.timer_ticks = player->bomb_timer_ticks;
    return bomb;
}
player_t make_player(uint8_t id, char *name) {
    player_t player;
    player.id = id;
    strcpy(player.name, name);
    player.row              = ss->start_coords[pli(id)] % ss->map_width;
    player.col              = ss->start_coords[pli(id)] / ss->map_width;
    player.alive            = false;
    player.ready            = false;
    player.bomb_count       = 0;
    player.bomb_radius      = ss->bomb_radius;
    player.bomb_timer_ticks = ss->bomb_timer_ticks;
    player.speed            = ss->player_speed;
    return player;
}

int find_player_slot() {
    for(int i = 0; i < MAX_PLAYERS; i++) {
        if(ss->players[i].id == 0)
            return i;
    }
    return MAX_PLAYERS;
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

bool cell_explode(int x, int y) {
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

                return true;
            }
        }
        return false;
    case CELL_SOFT: case CELL_SPEEDUP: case CELL_RADIUSUP: case CELL_TICKUP: {
        ss->map[i] = CELL_EMPTY;
        
        payload_block_destroyed_t p;
        p.coord = i;
        push_payload(MSG_BLOCK_DESTROYED, 255, 254, &p);

        
        } return true;
    // case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': {
    //     int id = celli_to_id(i);

    //     payload_death_t p;
    //     p.death_id = id;
    //     push_payload(MSG_DEATH, 255, 254, &p);

    //     } break;
    }
    return false;
}

void tick(){
    if(ss->game_status == GAME_LOBBY){
        int ready_count = 0;
        for(int i = 0; i < MAX_PLAYERS; i++){
            if(ss->players[i].id == 0) continue
            if(!ss->players[i].ready){
                break;
            }
            ready_count++;
        }
        if(ready_count == ss->player_count){
            change_game_status(GAME_RUNNING);

        }
    }else if(ss->game_status != GAME_RUNNING) {
        return;
    }

    int alive_count = 0;
    int last_id = 0; 
    for(int i = 0; i < MAX_PLAYERS; i++) {
        if(ss->players[i].id == 0) continue
        player_t *player = ss->players + i;
        if(!player->alive) continue;
        alive_count++;
        last_id = player->id;
        if(player->move_ticks > 0)
            player->move_ticks--;
    }
    if(alive_count == 1) { // also send SET_STATUS ?
        payload_winner_t p;
        p->id = last_id;
        push_payload(MSG_WINNER, 255, 254, &p);
        change_game_status(GAME_END);
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
                arr_erase(ss->bombs, i, ss->bombs_count);
                ss->bombs_count--;
                // memmove(bombs, bombs + 1, (ss->bombs_count - i - 1) * sizeof(bomb_t));
                ss->players[pli(bomb->owner_id)].bomb_count--;

                payload_explosion_end_t p;
                p.bomb_coord = bi;
                push_payload(MSG_EXPLOSION_END, 255, 254, &p);

                continue;
            }else {
                bomb->active = true;
                bomb->timer_ticks = ss->players[pli(bomb->owner_id)].bomb_timer_ticks;

                payload_explosion_start_t p;
                p.radius = bomb->radius;
                p.bomb_coord = bi;
                push_payload(MSG_EXPLOSION_START, 255, 254, &p);

                int row = bomb->row, col = bomb->col, radius = bomb->radius, width = map->map_width, height = ss->map_height;
                // explode cells in order, also propagate bomb activation to other bombs
                int xl  = clampi_min(row - radius, 0);
                int xls = clampi_min(row - 1, 0);
                int xr  = clampi_max(row + radius, width - 1);
                int xrs = clampi_max(row + 1, width - 1);
                int yt  = clampi_min(col - radius, 0);
                int yts = clampi_min(col - 1, 0);
                int yb  = clampi_max(col + radius, height - 1);
                int ybs = clampi_max(col + 1, height - 1);
                for(int x = xls; x >= xl; x--) {
                    if(cell_explode(x, bomb->col))
                        break;
                }
                for(int x = xrs; x <= xr; x++) {
                    if(cell_explode(x, bomb->col))
                        break;
                }
                for(int y = yts; y >= yt; y--) {
                    if(cell_explode(bomb->row, y))
                        break;
                }
                for(int y = ybs; y <= yb; y++) {
                    if(cell_explode(bomb->row, y))
                        break;
                }
            }
        }
        i++;
    }
}

bool cell_empty(int y) {
    if(ss->map[i] != CELL_EMPTY)
        return false;
    for(int i = 0; i < MAX_PLAYERS; i++){
        if(ss->players[i].id == 0) continue
        player_t *player = ss->players + i;
        if(GET_I(player->row, player->col, ss->map_width) == i) {
            return false;
        }
    }
    return true;
}

void handle_game_packets() {
    for(int i = 0; i < ss->shared_queue_count; i++){
        packet_t *packet = ss->shared_queue + i;
        msg_generic_t header = packet->header;
        payload_t *payload = &packet.payload;
        uint8_t sender_i = pli(header.sender_id);
        player_t *player = ss->players[sender_i];
        int x = player->row;
        int y = player->col;
        switch(header.msg_type){
        case MSG_HELLO: {
            payload_hello_t *p = (payload_hello_t*)payload;
            printf("Got client hello from: name: %.30s version: %.20s \n", p->name, p->version);
            if(ss->player_count < MAX_PLAYERS){
                ss->players[sender_i] = make_player(header.sender_id, p->name);
                ss->player_count++
            
                payload_welcome_t welcome;
                strcpy(welcome.version, VERSION);
                welcome.game_status = ss->game_status;
                welcome.player_count = ss->player_count;
                for(int i = 0; i < MAX_PLAYERS; i++) {
                    if(ss->players[i].id == 0) continue
                    welcome.players[i].ready = ss->players[i].ready;
                    strcpy(welcome.players[i].name, ss->players[i].name);
                }
                push_payload(MSG_WELCOME, 255, header.sender_id, &welcome);
                payload_map_t pm;
                pm.width = ss->map_width;
                pm.height = ss->map_height;
                memcpy(pm.map, ss->map);
                for(int i = 0; i < MAX_PLAYERS; i++) {
                    if(ss->players[i].id == 0) continue
                }
            }else {
                push_payload(MSG_DISCONNECT, 255, header.sender_id, NULL);    
            }
        } break;
        case MSG_LEAVE: {
            push_payload(MSG_LEAVE, header.sender_id, 254, NULL);
            push_payload(MSG_DISCONNECT, 255, header.sender_id, NULL);

            //arr_erase(ss->players, header.sender_id, ss->player_count);
            //arr_erase(ss->sockets, header.sender_id, ss->player_count);
            ss->players[sender_i].id = 0;
            ss->player_count--;
        } break;
        case MSG_SET_READY: {
            ss->players[sender_i].ready = true;
        } break;
        case MSG_SET_STATUS: {
            if(ss->game_status != GAME_END){
                send_error(header.sender_id, "wtf?");
                break;
            }
            change_game_status(((payload_set_status_t*)payload)->game_status);
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
            if(cell_empty(i)) {
                player->row = xd;
                player->col = yd;
                // ss->map[i] = id_to_celli(player->id);
                uint8_t cell = ss->map[i];
                if(cell == CELL_SPEEDUP || cell == CELL_RADIUSUP || cell == CELL_TICKUP) {
                    player->speed            -= cell == CELL_SPEEDUP;
                    player->bomb_radius      += cell == CELL_RADIUSUP;
                    player->bomb_timer_ticks += cell == CELL_TICKUP;

                    payload_bonus_retrieved_t p;
                    p.player_id = player->id;
                    p.coord = i;
                    push_payload(MSG_BONUS_RETRIEVED, 255, 254, &p);
                }
                // if(ss->map[pi] != CELL_BOMB) {
                //     ss->map[pi] = CELL_EMPTY;
                // }

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
            if(i == pi && player->bomb_count < ss->player_max_bombs) {
                ss->map[i] = CELL_BOMB;
                player->bomb_count++;
                ss->bombs[ss->bombs_count++] = make_bomb(player);

                payload_bomb_t p;
                p.player_id = player->id;
                p.coord = i;
                push_payload(MSG_BOMB, 255, 254, &p);
            }
        } break;
        default: break;
        // case MSG_WELCOME: case MSG_DISCONNECT: case MSG_PING: case MSG_PONG: 
        // case MSG_WINNER: case MSG_DEATH: case MSG_MAP: case MSG_ERROR: 
        // case MSG_MOVED: case MSG_BOMB: case MSG_EXPLOSION_START: case MSG_EXPLOSION_END: 
        // case MSG_BONUS_AVAILABLE: case MSG_BONUS_RETRIEVED: case MSG_BLOCK_DESTROYED: break; // server dont responds to these 
        }
    }

    ss->shared_queue_count = 0; // clear packet queue, we handled them all
}

// process input queue from clients -> tick -> send output queue to clients
void main_loop() {
    printf("Loop:\n");

    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    next.tv_nsec += 1000000000 / TICKS_PER_SECOND;

    int i = 0;
    while(1) {
        ss->out_queue_count = 0; // clear out queue

        if(ss->game_status == GAME_LOBBY) {
            if(ss->start_timeout <= 0){
                change_game_status(GAME_RUNNING);
                ss->start_timeout = 0;
            }else{
                ss->start_timeout--;
            }
        }

        handle_game_packets();

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

    int slot = find_player_slot();
    if(slot >= MAX_PLAYERS) {
        send_simple(MSG_DISCONNECT, id);
        return;
    }
    ss->plis[id] = slot;
    ss->sockets[slot] = socket;

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

        switch(msg_type) {
        case MSG_PING:
            send_ping(sender_id, true);
            break;
        case MSG_PONG:
            pings[sender_id == 255 ? MAX_PLAYERS : pli(sender_id)] = 0;
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

    if(listen(main_socket, MAX_PLAYERS) < 0) {
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

        int new_client_id = ss->next_id++;
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

    ss->next_id = 1; // skip id 0
    ss->start_timeout = 100;
    ss->player_max_bombs = 1;

    read_map("main.map");

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