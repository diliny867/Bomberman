#include "client.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <arpa/inet.h>
#include <ncurses.h>

/* ===================== STATE ===================== */

static uint8_t my_id = 0;
static int server_socket = 0;

static uint8_t map_width = 0;
static uint8_t map_height = 0;
static uint8_t map[MAP_SIZE_MAX];

static uint8_t game_status = GAME_LOBBY;
static int winner = 0;

static int player_count = 0;
static player_t players[MAX_PLAYERS] = { 0 };

static int bomb_count = 0;
static bomb_t bombs[MAP_SIZE_MAX] = { 0 };

static uint8_t plis[256] = { 0 };
#define pli(id) plis[id]

/* ===================== UI ===================== */

void init_ui() {
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
}

void shutdown_ui() {
    endwin();
}

void random_str(char *name, int count) {
    assert(count < 30);
    name[count] = '\0';
    for(int i = 0; i < count; i++) {
        name[i] = '0' + rand() % ('z' - '0');
    }
}

/* ===================== DRAW ===================== */

void draw_cell(int x, int y, char pixel) {
    mvaddch(y, x, pixel);
}

void draw_text(int x, int y, char *text) {
    mvprintw(y, x, "%s", text);
}

void draw_map() {
    for(int i = 0; i < map_width * map_height; i++) {
        int x = i % map_width;
        int y = i / map_width;
        draw_cell(x, y, map[i]);
    }
}

void draw_stats() {
    char buf[50];
    player_t *player = players + pli(my_id);
    sprintf(buf, "Decreased Speed: %d", player->speed);
    draw_text(map_width + 2, 2, buf);
    sprintf(buf, "Bonus Radius: %d", player->bomb_radius);
    draw_text(map_width + 2, 3, buf);
    sprintf(buf, "Additional Bombs: %d", player->max_bombs);
    draw_text(map_width + 2, 4, buf);
    sprintf(buf, "Additional Bomb Timer Ticks: %d", player->bomb_timer_ticks);
    draw_text(map_width + 2, 5, buf);
}

/* ==== FIXED EXPLOSION ==== */
void draw_bombs() {
    for(int i = 0; i < bomb_count; i++) {
        int row = bombs[i].row;
        int col = bombs[i].col;
        int radius = bombs[i].radius;

        int center = GET_I(row, col, map_width);

        if(radius == 0) {
            draw_cell(row, col, CELL_BOMB);
        } else {
            draw_cell(row, col, '*');

            for(int r = 1; r <= radius; r++) {

                if(row - r >= 0)
                    draw_cell(row - r, col, '-');

                if(row + r < map_height)
                    draw_cell(row + r, col, '-');

                if(col - r >= 0)
                    draw_cell(row, col - r, '|');

                if(col + r < map_width)
                    draw_cell(row, col + r, '|');
            }
        }
    }
}

void draw_players() {
    for(int i = 0; i < MAX_PLAYERS; i++) {
        if(players[i].id == 0 || !players[i].alive) continue;

        draw_cell(players[i].row, players[i].col, '1' + i);
    }
}

void draw() {
    clear();

    if(game_status == GAME_LOBBY){
        char buf[50];
        sprintf(buf, "Lobby %s", players[pli(my_id)].ready ? "Ready" : "Not ready");
        draw_text(2, 2, buf);
    }
    else if(game_status == GAME_RUNNING){
        draw_map();
        draw_bombs();
        draw_players();
        draw_stats();
        // char buf[50];
        // sprintf(buf, "%d %d %d %d", players[pli(my_id)].row, players[pli(my_id)].col, players[pli(my_id)].id, players[pli(my_id)].alive);
        // draw_text(10, 1, buf);
    }
    else if(game_status == GAME_END){
        char buf[50];
        sprintf(buf, "Winner: %s", players[winner].name);
        draw_text(2, 2, buf);
    }

    // for(int i = 0; i< MAX_PLAYERS; i++){
    //     char buf[50];
    //     sprintf(buf, "%d %d", players[i].id, players[i].alive);
    //     draw_text(10, 10+i, buf);
    // }

    refresh();
}

/* ===================== NETWORK ===================== */

void client_send_simple(uint8_t msg_type) {
    send_simple(server_socket, msg_type, my_id, 255);
}

void send_try_bomb(uint16_t coord) {
    packet_t p;
    p.header = make_header(MSG_BOMB_ATTEMPT, my_id, 255);
    p.payload.bomb_attempt.coord = coord;
    send_packet_simple(server_socket, &p);
}

void send_try_move(uint8_t direction) {
    packet_t p;
    p.header = make_header(MSG_MOVE_ATTEMPT, my_id, 255);
    p.payload.move_attempt.direction = direction;
    send_packet_simple(server_socket, &p);
}

int handle_packet(uint8_t msg_type, uint8_t sender_id, uint8_t target_id, payload_t *payload) {
    if(my_id == 0){
        my_id = target_id;
        plis[my_id] = 0;
        players[pli(my_id)].id = my_id;
    }

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
        player_count = p->player_count;
        for(int i = 0; i < p->player_count; i++) {
            plis[p->players[i].id] = p->players[i].i;
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
        for(int i = 0; i < map_width * map_height; i++) {
            int index = map[i] - '1';
            if(map[i] >= '1' && map[i] <= '8' && players[index].id != 0) {
                players[index].row = i % map_width;
                players[index].col = i / map_width;
                players[index].ready = 0;
                players[index].alive = 1;
                map[i] = CELL_EMPTY;
            }
        }
    } break;
    case MSG_SET_READY : {
        client_send_simple(MSG_SET_READY);
    } break;
    case MSG_SET_STATUS : {
        payload_set_status_t *p = (payload_set_status_t*)payload;
        game_status = p->game_status;
        if(game_status == GAME_RUNNING){
            for(int i = 0; i < MAX_PLAYERS; i++){
                players[i].alive = 1;
            }
        }
    } break;
    case MSG_WINNER : {
        payload_winner_t *p = (payload_winner_t*)payload;
        winner = pli(p->id);
        game_status = GAME_END;
    } break;
    case MSG_MOVED : {
        payload_moved_t *p = (payload_moved_t*)payload;
        int i = pli(p->player_id);
        int row = p->coord % map_width;
        int col = p->coord / map_width;
        players[i].row = row;
        players[i].col = col;
        log2("New row: %d  col: %d  i: %d  my_i: %d\n", row, col, pli(p->player_id), pli(my_id));
    } break;
    case MSG_BOMB : {
        payload_bomb_t *p = (payload_bomb_t*)payload;
        bombs[bomb_count].owner_id = p->player_id;
        bombs[bomb_count].row = p->coord % map_width;
        bombs[bomb_count].col = p->coord / map_width;
        bombs[bomb_count].radius = 0;
        bombs[bomb_count].timer_ticks = 0;
        bomb_count++;
    } break;
    case MSG_EXPLOSION_START : {
        payload_explosion_start_t *p = (payload_explosion_start_t*)payload;
        for(int i = 0; i < bomb_count; i++) {
            int coord = bombs[i].row + bombs[i].col * map_width;
            if(coord == p->coord) {
                bombs[i].radius = p->radius;
            }
        }
    } break;
    case MSG_EXPLOSION_END : {
        payload_explosion_end_t *p = (payload_explosion_end_t*)payload;
        for(int i = 0; i < bomb_count; i++) {
            int coord = bombs[i].row + bombs[i].col * map_width;
            if(coord == p->coord) {
                bombs[i] = bombs[bomb_count - 1];
            }
        }
        bomb_count--;
    } break;
    case MSG_DEATH : {
        payload_death_t *p = (payload_death_t*)payload;
        for(int i = 0; i < MAX_PLAYERS; i++) {
            if(players[i].id == p->death_id) {
                players[i].alive = false;
                break;
            }
        }
    } break;
    case MSG_BONUS_AVAILABLE : {
        payload_bonus_available_t *p = (payload_bonus_available_t*)payload;
        switch(p->type) {
        case BONUS_SPEED:
            map[p->coord] = CELL_SPEEDUP;
            break;
        case BONUS_RADIUS:
            map[p->coord] = CELL_RADIUSUP;
            break;
        case BONUS_TIMER:
            map[p->coord] = CELL_TICKUP;
            break;
        case BONUS_BOMB:
            map[p->coord] = CELL_BOMBUP;
            break;
        default: break;
        }
    } break;
    case MSG_BONUS_RETRIEVED : {
        payload_bonus_retrieved_t *p = (payload_bonus_retrieved_t*)payload;
        for(int i = 0; i < MAX_PLAYERS; i++) {
            if(players[i].id == p->player_id) {
                players[i].speed            += map[p->coord] == CELL_SPEEDUP;
                players[i].bomb_radius      += map[p->coord] == CELL_RADIUSUP;
                players[i].bomb_timer_ticks += (map[p->coord] == CELL_TICKUP) * 10;
                players[i].max_bombs        += map[p->coord] == CELL_BOMBUP;
            }
        }
        map[p->coord] = CELL_EMPTY;
    } break;
    case MSG_BLOCK_DESTROYED : {
        payload_block_destroyed_t *p = (payload_block_destroyed_t*)payload;
        map[p->coord] = CELL_EMPTY;
    } break;
    // case MSG_HELLO: case MSG_MOVE_ATTEMPT: case MSG_BOMB_ATTEMPT: client ignores these
    default: break;
    }

    return 0;
}

/* ===================== INPUT ===================== */

void handle_input() {
    int ch = getch();

    switch(ch) {
        case KEY_UP:    send_try_move(DIR_UP   ); break;
        case KEY_DOWN:  send_try_move(DIR_DOWN ); break;
        case KEY_LEFT:  send_try_move(DIR_LEFT ); break;
        case KEY_RIGHT: send_try_move(DIR_RIGHT); break;

        case ' ':
            if(game_status == GAME_LOBBY || game_status == GAME_END) {
                players[pli(my_id)].ready = 1;
                client_send_simple(MSG_SET_READY);
                break;
            }
            if(player_count > 0) {
                int coord = GET_I(players[pli(my_id)].row, players[pli(my_id)].col, map_width);
                send_try_bomb(coord);
            }
            break;
    }
}


/* ===================== CLIENT ===================== */

void client(char *name, char *ip, int port) {
    srand(time(0));
    init_ui();

    char _tmp_name[30];
    if(name == NULL) {
        name = _tmp_name;
        random_str(name, 20);
    }

    log2("name: %s version: %s\n", name, VERSION);

    struct sockaddr_in server_addr;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        shutdown_ui();
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);

    if (connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        shutdown_ui();
        close(server_socket);
        return;
    }

    log2("Connected to server %s:%d\n", ip, port);

    packet_t p;
    p.header = make_header(MSG_HELLO, my_id, 255);
    strcpy(p.payload.hello.version, VERSION);
    strcpy(p.payload.hello.name, name);
    send_packet_simple(server_socket, &p);

    uint8_t msg_type, sender_id, target_id;
    payload_t *payload;
    assert(sizeof(packet_t) <= 100000);
    uint8_t buffer[100000];
    uint8_t *in;
    while (1) {

        handle_input();
        draw();

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(server_socket, &rfds);

        struct timeval tv = {0, 10000};

        int ready = select(server_socket + 1, &rfds, NULL, NULL, &tv);
        if (ready <= 0) continue;

        log2("Client read:\n");
        ssize_t n = read(server_socket, buffer, sizeof(buffer));
        if (n <= 0) break;
        log2("Read from server %ld bytes\n", n);

        int msg_size = 0;
        while(msg_size < n) {
            in = buffer + msg_size;

            msg_type  = in[0];
            sender_id = in[1];
            target_id = in[2];
            payload   = (payload_t*)(in + sizeof(msg_generic_t));
            log2("msg_type: %hhu sender_id: %hhu target_id: %hhu\n", msg_type, sender_id, target_id);

            if(handle_packet(msg_type, sender_id, target_id, payload))
                break;

            msg_size += 3 + get_payload_size(msg_type);
        }
    }

    shutdown_ui();
    close(server_socket);
}
