#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <arpa/inet.h>


#define OUT_QUEUE_SLOT_SIZE (1000 / MAX_PLAYERS)
#define SHARED_QUEUE_SIZE   1000

typedef struct {
    packet_t data[OUT_QUEUE_SLOT_SIZE];
    int count;
    int start;
} out_queue_slot_t;

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

    uint8_t plis[256];
    uint8_t next_id;

    // ring buffer
    packet_t shared_queue[SHARED_QUEUE_SIZE];
    int shared_queue_count;
    int shared_queue_start;

    // ring buffer
    out_queue_slot_t out_queue[MAX_PLAYERS];

    int start_timeout;
    int bonus_timeout;
    int bonus_count;
} shared_state_t;

static shared_state_t *ss;

#define pli(id) ss->plis[(id)]


static inline void make_shared_memory() {
    ss = mmap(NULL, sizeof(shared_state_t), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
}

void random_map(char *file_name) {
    FILE *fout = fopen(file_name, "w");

    int width = 10 + rand() % 20;
    int height = 10 + rand() % 10;
    int preample = fprintf(fout, "%d %d %d %d %d %d", width, height, 10, 15, 1, 40);

    for(int y = 0; y < height; y++) {
        for(int x = 0; x < width; x++) {
            int c = rand() % 2 == 0 ? CELL_EMPTY : CELL_SOFT;
            fputc(c, fout);
        }
        // fputc('\n', fout);
    }

    int hard_count = rand() % (width * height / 6);
    for(int i = 0; i < hard_count; i++) {
        int coord = preample + rand() % (width * height);
        fseek(fout, coord, SEEK_SET);
        fputc(CELL_HARD, fout);
    }

    int bonus_count = width / 2 + rand() % (width / 2);
    for(int i = 0; i < bonus_count; i++) {
        int b = 1 + rand() % 4;
        int coord = preample + rand() % (width * height);
        fseek(fout, coord, SEEK_SET);
        if(b == BONUS_SPEED){
            fputc(CELL_SPEEDUP, fout);
        }
        if(b == BONUS_RADIUS){
            fputc(CELL_RADIUSUP, fout);
        }
        if(b == BONUS_TIMER){
            fputc(CELL_TICKUP, fout);
        }
        if(b == BONUS_BOMB){
            fputc(CELL_BOMBUP, fout);
        }
    }

    int player_positions[MAX_PLAYERS];
    for(int i = 0; i < MAX_PLAYERS; i++) {
        int x = 1 + rand() % (width - 2);
        int y = 1 + rand() % (height - 2);
        int coord = preample + GET_I(x, y, width);
        player_positions[i] = coord;
        // clear around player
        fseek(fout, coord - 1, SEEK_SET);
        fputc(CELL_EMPTY, fout);
        fseek(fout, coord + 1, SEEK_SET);
        fputc(CELL_EMPTY, fout);
        fseek(fout, coord - width, SEEK_SET);
        fputc(CELL_EMPTY, fout);
        fseek(fout, coord + width, SEEK_SET);
        fputc(CELL_EMPTY, fout);
    }
    // fill players after to not delete previous player position
    for(int i = 0; i < MAX_PLAYERS; i++) {
        fseek(fout, preample + player_positions[i], SEEK_SET);
        fputc('1' + i, fout);
    }

    fclose(fout);
}

void read_map(char *name) {
    FILE *fin = fopen(name, "r");

    fscanf(fin, "%"SCNu8"%"SCNu8, &ss->map_width, &ss->map_height);
    fscanf(fin, "%"SCNu16"%"SCNu16"%"SCNu8"%"SCNu16, &ss->player_speed, &ss->bomb_linger_ticks, &ss->bomb_radius, &ss->bomb_timer_ticks);

    int size = ss->map_width * ss->map_height;
    for(int i = 0; i < size;) {
        int c = fgetc(fin);
        if(c == EOF) break;
        if(c == ' ' || c == '\n' || c == '\r') continue;
        if(c >= '1' && c <= '8'){
            ss->start_coords[c - '1'] = i;
            ss->map[i] = CELL_EMPTY;
        }else {
            if(c == 'R'){

            }
            ss->map[i] = c;
        }
        i++;
    }

    printf("%d %d\n", ss->map_width, ss->map_height);
    for(int y = 0; y < ss->map_height; y++){
        for(int x = 0; x < ss->map_width; x++){
            printf("%c", ss->map[GET_I(x, y, ss->map_width)]);
        }
        printf("\n");
    }

    fclose(fin);
}


void _push_payload(packet_t *queue, int *queue_count, int *start, int max_size, uint8_t msg_type, uint8_t sender_id, uint8_t target_id, payload_t *payload, int payload_size) {
    if(*queue_count >= max_size) {
        log("Queue of size %d is filled, dropping packet type %hhu from %hhu to %hhu\n", max_size, msg_type, sender_id, target_id);
        return;
    }
    int index = (*start + *queue_count) % max_size;
    queue[index].header = make_header(msg_type, sender_id, target_id);
    if(payload)
        memcpy(&queue[index].payload, payload, payload_size);
    (*queue_count)++;
}
void push_payload(uint8_t msg_type, uint8_t sender_id, uint8_t target_id, payload_t *payload) {
    log("Pushing %d %d\n", msg_type, pli(target_id));
    if(target_id == 254){
        for(int i = 0; i < MAX_PLAYERS; i++) {
            if(ss->players[i].id == 0) continue;
            _push_payload(ss->out_queue[i].data, &ss->out_queue[i].count, &ss->out_queue[i].start, OUT_QUEUE_SLOT_SIZE, msg_type, sender_id, ss->players[i].id, payload, get_payload_size(msg_type));
        }
    }else {
        int i = pli(target_id);
        _push_payload(ss->out_queue[i].data, &ss->out_queue[i].count, &ss->out_queue[i].start, OUT_QUEUE_SLOT_SIZE, msg_type, sender_id, target_id, payload, get_payload_size(msg_type));
    }
}
void push_shared(uint8_t msg_type, uint8_t sender_id, uint8_t target_id, payload_t *payload) {
    _push_payload(ss->shared_queue, &ss->shared_queue_count, &ss->shared_queue_start, SHARED_QUEUE_SIZE, msg_type, sender_id, target_id, payload, get_payload_size(msg_type));
}


void send_packet(packet_t *packet) {
    if(packet->header.target_id == 254) {
        for(int i = 0; i < MAX_PLAYERS; i++) {
            if(ss->players[i].id == 0) continue;
            send_packet_simple(ss->sockets[i], packet);
        }
    }else {
        log("In sendpacket %d\n", ss->sockets[pli(packet->header.target_id)]);
        send_packet_simple(ss->sockets[pli(packet->header.target_id)], packet);
    }
}

void send_error(int target_id, char *text) {
    packet_t packet;
    packet.header = make_header(MSG_ERROR, 255, target_id);
    strcpy(packet.payload.error.error, text);
    send_packet(&packet);
}

void change_game_status(uint8_t status) {
    if(ss->game_status == status) return;
    if(status == GAME_RUNNING) {
        for(int i = 0; i < MAX_PLAYERS; i++) {
            if(ss->players[i].id == 0) continue;
            ss->players[i].alive = 1;
            ss->players[i].ready = 0;
        }
    }
    ss->game_status = status;
    payload_set_status_t p;
    p.game_status = status;
    push_payload(MSG_SET_STATUS, 255, 254, (payload_t*)&p);
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
    player.max_bombs        = ss->player_max_bombs;
    return player;
}

int find_player_slot() {
    for(int i = 0; i < MAX_PLAYERS; i++) {
        if(ss->players[i].id == 0)
            return i;
    }
    return MAX_PLAYERS;
}

bool cell_empty(int cell_i) {
    if(ss->map[cell_i] != CELL_EMPTY)
        return false;
    for(int i = 0; i < MAX_PLAYERS; i++) {
        if(ss->players[i].id == 0) continue;
        player_t *player = ss->players + i;
        if(GET_I(player->row, player->col, ss->map_width) == cell_i) {
            return false;
        }
    }
    return true;
}
bool cell_can_move(int cell_i){
    if(ss->map[cell_i] != CELL_EMPTY && ss->map[cell_i] != CELL_SPEEDUP && ss->map[cell_i] != CELL_RADIUSUP && ss->map[cell_i] != CELL_TICKUP && ss->map[cell_i] != CELL_BOMBUP)
        return false;

    for(int i = 0; i < MAX_PLAYERS; i++) {
        if(ss->players[i].id == 0) continue;
        player_t *player = ss->players + i;
        if(GET_I(player->row, player->col, ss->map_width) == cell_i) {
            return false;
        }
    }
    return true;
}


bool cell_explode(int x, int y) {
    for(int i = 0; i < MAX_PLAYERS; i++) {
        if(ss->players[i].id == 0) continue;
        if(ss->players[i].row == x && ss->players[i].col == y){
            ss->players[i].alive = 0;
            payload_death_t p;
            p.death_id = ss->players[i].id;
            push_payload(MSG_DEATH, 255, 254, (payload_t*)&p);
        }
    }
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
                push_payload(MSG_EXPLOSION_START, 255, 254, (payload_t*)&p);

                return true;
            }
        }
        return false;
    case CELL_SOFT: case CELL_SPEEDUP: case CELL_RADIUSUP: case CELL_TICKUP: case CELL_BOMBUP: {
        ss->map[i] = CELL_EMPTY;
        
        payload_block_destroyed_t p;
        p.coord = i;
        push_payload(MSG_BLOCK_DESTROYED, 255, 254, (payload_t*)&p);

        
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
    // static int tick_i = 0;
    // log("Tick %d\n", tick_i++);

    if(ss->game_status == GAME_LOBBY || ss->game_status == GAME_END) {
        static int timeout = 100;
        int ready_count = 0;
        for(int i = 0; i < MAX_PLAYERS; i++){
            if(ss->players[i].id == 0) continue;
            if(!ss->players[i].ready){
                break;
            }
            ready_count++;
        }
        // log("Ready: %d\n", ready_count);
        if(ready_count > 0 && ready_count == ss->player_count) {
            log("Timeout: %d\n", timeout);
            timeout--;
            if(timeout <= 0) {
                timeout = 100;
                change_game_status(GAME_RUNNING);
                for(int i = 0; i < MAX_PLAYERS; i++) {
                    log("Player:%d id:%d ready:%d alive:%d\n", i, ss->players[i].id, ss->players[i].ready, ss->players[i].alive);
                }
            }
        }else {
            timeout = 100;
        }
    }

    if(ss->game_status != GAME_RUNNING) {
        return;
    }

    if(ss->bonus_count < ss->map_width) {
        if(ss->bonus_timeout <= 0) {
            int size = ss->map_width * ss->map_height;
            for(int i = 0; i < 8; i++) {
                int i = rand() % size;
                if(cell_empty(i)) {
                    payload_bonus_available_t p;
                    p.type = 1 + rand() % 4;
                    p.coord = i;
                    if(p.type == BONUS_SPEED){
                        ss->map[i] = CELL_SPEEDUP;
                    }
                    if(p.type == BONUS_RADIUS){
                        ss->map[i] = CELL_RADIUSUP;
                    }
                    if(p.type == BONUS_TIMER){
                        ss->map[i] = CELL_TICKUP;
                    }
                    if(p.type == BONUS_BOMB){
                        ss->map[i] = CELL_BOMBUP;
                    }
                    ss->bonus_count++;
                    push_payload(MSG_BONUS_AVAILABLE, 255, 254, (payload_t*)&p);
                    break;
                }
            }

            ss->bonus_timeout = 100;
        }else {
            ss->bonus_timeout--;
        }
    }

    int alive_count = 0;
    int last_id = 0; 
    for(int i = 0; i < MAX_PLAYERS; i++) {
        if(ss->players[i].id == 0) continue;
        player_t *player = ss->players + i;
        if(!player->alive) continue;
        alive_count++;
        last_id = player->id;
        if(player->move_ticks > 0)
            player->move_ticks--;
    }

    if(alive_count == 1) { // also send SET_STATUS ?
        payload_winner_t p;
        p.id = last_id;
        push_payload(MSG_WINNER, 255, 254, (payload_t*)&p);
        change_game_status(GAME_END);
        return;
    }
    if(alive_count == 0) {
        change_game_status(GAME_LOBBY);
        return;
    }


    for(int i = 0; i < ss->bombs_count;) {
        bomb_t *bomb = ss->bombs + i;
        if(bomb->timer_ticks > 0) { // just tick
            bomb->timer_ticks--;
        }else {
            int bi = GET_I(bomb->row, bomb->col, ss->map_width);
            if(bomb->active) { // delete that bomb
                ss->map[bi] = CELL_EMPTY;
                arr_erase(ss->bombs, i, ss->bombs_count);
                ss->bombs_count--;
                // memmove(bombs, bombs + 1, (ss->bombs_count - i - 1) * sizeof(bomb_t));
                ss->players[pli(bomb->owner_id)].bomb_count--;

                payload_explosion_end_t p;
                p.coord = bi;
                push_payload(MSG_EXPLOSION_END, 255, 254, (payload_t*)&p);

                continue;
            }else {
                bomb->active = true;
                bomb->timer_ticks = ss->players[pli(bomb->owner_id)].bomb_timer_ticks;

                payload_explosion_start_t p;
                p.radius = bomb->radius;
                p.coord = bi;
                push_payload(MSG_EXPLOSION_START, 255, 254, (payload_t*)&p);
            }
        }
        if(bomb->timer_ticks <= 0 || bomb->active){
            int row = bomb->row, col = bomb->col, radius = bomb->radius, width = ss->map_width, height = ss->map_height;
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
            cell_explode(row, col);
        }
        i++;
    }
}

void handle_game_packets() {
    int count = ss->shared_queue_count;
    int start = ss->shared_queue_start;
    // clear ring buffer
    ss->shared_queue_count = 0;
    ss->shared_queue_start = (ss->shared_queue_start + ss->shared_queue_count) % SHARED_QUEUE_SIZE;
    for(int i = 0; i < count; i++){
        int index = (start + i) % SHARED_QUEUE_SIZE;
        packet_t *packet = ss->shared_queue + index;
        msg_generic_t header = packet->header;
        payload_t *payload = &packet->payload;
        uint8_t sender_i = pli(header.sender_id);
        player_t *player = ss->players + sender_i;
        int x = player->row;
        int y = player->col;
        switch(header.msg_type){
        case MSG_HELLO: {
            payload_hello_t *p = (payload_hello_t*)payload;
            log("Got client hello from: name: %.30s version: %.20s\n", p->name, p->version);
            // dump_bytes(packet, 53);
            if(ss->player_count < MAX_PLAYERS) {
                ss->players[sender_i] = make_player(header.sender_id, p->name);
                ss->player_count++;
            
                payload_welcome_t welcome;
                strcpy(welcome.version, VERSION);
                welcome.game_status = ss->game_status;
                welcome.player_count = ss->player_count;

                payload_map_t pm;
                pm.width = ss->map_width;
                pm.height = ss->map_height;
                memcpy(pm.map, ss->map, ss->map_width * ss->map_height);

                int player_index = 0;
                for(int i = 0; i < MAX_PLAYERS; i++) {
                    if(ss->players[i].id == 0) continue;

                    int p_index = player_index++;
                    // if(ss->players[i].id == header.sender_id) {
                    //     p_index = 0;
                    // }else{
                    //     p_index = 1 + player_index;
                    //     player_index++;
                    // }

                    welcome.players[p_index].id = ss->players[i].id;
                    welcome.players[p_index].ready = ss->players[i].ready;
                    welcome.players[p_index].i = i;
                    strcpy(welcome.players[p_index].name, ss->players[i].name);

                    pm.map[GET_I(ss->players[i].row, ss->players[i].col, ss->map_width)] = '1' + i;
                }

                push_payload(MSG_WELCOME, 255, 254, (payload_t*)&welcome);
                push_payload(MSG_MAP, 255, 254, (payload_t*)&pm);
            }else {
                push_payload(MSG_DISCONNECT, 255, header.sender_id, NULL);
            }
        } break;
        case MSG_LEAVE: {
            push_payload(MSG_LEAVE, header.sender_id, 254, NULL);
            push_payload(MSG_DISCONNECT, 255, header.sender_id, NULL);

            //arr_erase(ss->players, header.sender_id, ss->player_count);
            //arr_erase(ss->sockets, header.sender_id, ss->player_count);
            // ss->players[sender_i].id = 0;
            // ss->player_count--;
        } break;
        case MSG_SET_READY: {
            log("Ready i: %d\n", sender_i);
            ss->players[sender_i].ready = 1;
        } break;
        case MSG_SET_STATUS: {
            payload_set_status_t *p = (payload_set_status_t*)payload;
            if(ss->game_status != GAME_END && p->game_status != GAME_LOBBY){
                send_error(header.sender_id, "wtf?");
                break;
            }
            change_game_status(p->game_status);
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
            if(cell_can_move(i)) {
                player->row = xd;
                player->col = yd;
                player->move_ticks = player->speed;
                // ss->map[i] = id_to_celli(player->id);
                uint8_t cell = ss->map[i];
                if(cell == CELL_SPEEDUP || cell == CELL_RADIUSUP || cell == CELL_TICKUP || cell == CELL_BOMBUP) {
                    player->speed            -= cell == CELL_SPEEDUP;
                    player->bomb_radius      += cell == CELL_RADIUSUP;
                    player->bomb_timer_ticks += (cell == CELL_TICKUP) * 10;
                    player->max_bombs        += cell == CELL_BOMBUP;

                    payload_bonus_retrieved_t p;
                    p.player_id = player->id;
                    p.coord = i;
                    push_payload(MSG_BONUS_RETRIEVED, 255, 254, (payload_t*)&p);
                }
                // if(ss->map[pi] != CELL_BOMB) {
                //     ss->map[pi] = CELL_EMPTY;
                // }

                payload_moved_t p;
                p.player_id = player->id;
                p.coord = i;
                push_payload(MSG_MOVED, 255, 254, (payload_t*)&p);
            }
        } break;
        case MSG_BOMB_ATTEMPT: {
            if(!player->alive) break;
            int i = payload->bomb_attempt.coord;
            int pi = GET_I(x, y, ss->map_width);
            if(i == pi && player->bomb_count < player->max_bombs) {
                ss->map[i] = CELL_BOMB;
                player->bomb_count++;
                ss->bombs[ss->bombs_count++] = make_bomb(player);

                payload_bomb_t p;
                p.player_id = player->id;
                p.coord = i;
                push_payload(MSG_BOMB, 255, 254, (payload_t*)&p);
            }
        } break;
        default: break;
        // case MSG_WELCOME: case MSG_DISCONNECT:
        // case MSG_WINNER: case MSG_DEATH: case MSG_MAP: case MSG_ERROR: 
        // case MSG_MOVED: case MSG_BOMB: case MSG_EXPLOSION_START: case MSG_EXPLOSION_END: 
        // case MSG_BONUS_AVAILABLE: case MSG_BONUS_RETRIEVED: case MSG_BLOCK_DESTROYED: break; // server dont responds to these 
        }
    }

    // ss->shared_queue_count = 0; // clear packet queue, we handled them all
}

// process input queue from clients -> tick -> send output queue to clients
void main_loop() {
    log("Loop:\n");

    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    next.tv_nsec += 1000000000 / TICKS_PER_SECOND;

    int i = 0;
    while(1) {
        // if(ss->game_status == GAME_LOBBY) {
        //     if(ss->start_timeout <= 0){
        //         change_game_status(GAME_RUNNING);
        //         ss->start_timeout = 0;
        //     }else{
        //         ss->start_timeout--;
        //     }
        // }

        handle_game_packets();

        tick();

        // for(int i = 0; i < out_queue_count; i++){
        //     log("Sending: %d\n", out_queue[i].header.msg_type);
        //     send_packet(&out_queue[i]);
        //     if(out_queue[i].header.msg_type == MSG_DISCONNECT) {
        //         close(ss->sockets[pli(out_queue[i].header.target_id)]);
        //     }
        // }
        // out_queue_count = 0; // clear out queue
        
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        next.tv_nsec += 1000000000 / TICKS_PER_SECOND;
        if (next.tv_nsec >= 1000000000) {
            next.tv_nsec -= 1000000000;
            next.tv_sec += 1;
        }
    }
}

void process_client(int id, int socket) {
    log("Processing client: %d %d\n", id, socket);
    log("Client count: %d\n", ss->player_count);

    int slot = find_player_slot();
    if(slot >= MAX_PLAYERS) {
        push_payload(MSG_DISCONNECT, 255, id, NULL);
        return;
    }
    ss->plis[id] = slot;
    ss->sockets[slot] = socket;

    log("Socket: %d %d %d\n", socket, ss->sockets[slot], slot);

    uint8_t msg_type, sender_id, target_id;
    payload_t *payload;
    uint8_t buffer[sizeof(packet_t)];
    uint8_t *in;
    while(1) {
        packet_t *queue_data = ss->out_queue[slot].data;
        int      queue_start = ss->out_queue[slot].start;
        int      queue_count = ss->out_queue[slot].count;
        // clear read part of ring buffer
        ss->out_queue[slot].count = 0;
        ss->out_queue[slot].start = (queue_start + queue_count) % OUT_QUEUE_SLOT_SIZE;
        for(int i = 0; i < queue_count; i++){
            int index = (queue_start + i) % OUT_QUEUE_SLOT_SIZE;
            log("Sending: %d\n", queue_data[index].header.msg_type);
            send_packet(&queue_data[index]);
            if(queue_data[index].header.msg_type == MSG_DISCONNECT) {
                close(ss->sockets[pli(queue_data[index].header.target_id)]);
            }
        }


        // wait for socket or timeouts
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(socket, &rfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 1000 };
        int ready = select(socket + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) return;
        if (ready == 0) continue;  // timeout


        log("Server read:\n");
        ssize_t n = read(socket, buffer, sizeof(buffer));
        if(n <= 0) return;
        log("Read from client %d %ld bytes\n", id, n);


        int msg_size = 0;
        while(msg_size < n) {
            in = buffer + msg_size;

            msg_type  = in[0];
            //sender_id = in[1];
            sender_id = id;
            target_id = in[2];
            payload   = (payload_t*)(in + sizeof(msg_generic_t));
            log("msg_type: %hhu sender_id: %hhu target_id: %hhu\n", msg_type, sender_id, target_id);

            switch(msg_type) {
            case MSG_PING:
                send_ping(ss->sockets[pli(sender_id)], sender_id, 255, true);
                break;
            case MSG_PONG:
                pings[pli(sender_id)] = 0;
                break;
            default: // process all others separately, keeping timeline during tick valid
                push_shared(msg_type, sender_id, target_id, payload);
            }

            msg_size += 3 + get_payload_size(msg_type);
        }
    }
}

void main_networking(int port) {
    int main_socket;
    struct sockaddr_in server_address;
    int client_socket;
    struct sockaddr_in client_address;
    int client_address_size = sizeof(client_address);

    main_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(main_socket < 0) {
        log("Error opening server socket\n");
        exit(1);
    }
    log("Server socket created\n");

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port);

    if(bind(main_socket, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
        log("Error binding server socket\n");
        close(main_socket);
        exit(1);
    }
    log("Server socket binded\n");

    if(listen(main_socket, MAX_PLAYERS) < 0) {
        log("Error listening to server socket\n");
        exit(1);
    }
    log("Listening to server socket\n");

    log("%d %d\n\n", get_payload_size(MSG_WELCOME), get_payload_size(MSG_MAP));

    while(1) {
        client_socket = accept(main_socket, (struct sockaddr*)&client_address, &client_address_size);
        if(client_socket < 0) {
            log("Error accepting client: %d\n", errno);
            continue;
        }

        int new_client_id = ss->next_id++;
        int cpid = fork();

        if(cpid == 0) {
            close(main_socket);
            cpid = fork();
            if(cpid == 0) {
                process_client(new_client_id, client_socket);
                ss->players[pli(new_client_id)].id = 0;
                ss->player_count--;
                close(main_socket);
                log("Ended processing client %d\n", new_client_id);
                exit(0);
            }else{
                wait(NULL);
                log("Orphaned client: %d\n", new_client_id);
                exit(0);
            }
        }else{
            close(client_socket);
        }
    }

}

void server(int port) {
    srand(time(0));

    make_shared_memory();

    ss->next_id = 1; // skip id 0
    ss->start_timeout = 100;
    ss->player_max_bombs = 1;
    ss->game_status = GAME_LOBBY;
    ss->plis[255] = 255;

    char *rand_map_name = "map_tmp_tmp___.map_tmp";
    random_map(rand_map_name);
    read_map(rand_map_name);
    
    // read_map("main.map");

    int pid = fork();
    if(pid == 0) {
        main_networking(port);
    }else{
        main_loop();
    }    
}