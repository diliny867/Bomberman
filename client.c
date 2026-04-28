/*#include "client.h"

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
static int winner = 0;

static int player_count = 0;
static player_t players[MAX_PLAYERS] = { 0 };

static int bomb_count = 0;
static bomb_t bombs[MAP_SIZE_MAX] = { 0 };

static uint8_t plis[256] = { 0 };
#define pli(id) plis[id]

void draw_cell(int coord, char pixel) {

}

void draw_map() {
    for(int i = 0; i < map_width * map_height; i++) {
        draw_cell(i, map[i]);
    }
}
void draw_bombs() {
    for(int i = 0; i < bomb_count; i++) {
        int row = bombs[i].row, col = bombs[i].col, radius = bombs[i].radius, width = map_width, height = map_height;
        int index = GET_I(row, col, width);
        if(radius == 0) {
            draw_cell(index, 'o');
        }else {
            int xl  = clampi_min(row - radius, 0);
            int xls = clampi_min(row - 1, 0);
            int xr  = clampi_max(row + radius, width - 1);
            int xrs = clampi_max(row + 1, width - 1);
            int yt  = clampi_min(col - radius, 0);
            int yts = clampi_min(col - 1, 0);
            int yb  = clampi_max(col + radius, height - 1);
            int ybs = clampi_max(col + 1, height - 1);
            for(int x = xls; x >= xl; x--) {
                draw_cell(index, '>');
            }
            for(int x = xrs; x <= xr; x++) {
                draw_cell(index, '<');
            }
            for(int y = yts; y >= yt; y--) {
                draw_cell(index, '^');
            }
            for(int y = ybs; y <= yb; y++) {
                draw_cell(index, 'v');
            }
        }
    }
}
void draw_players() {
    for(int i = 0; i < MAX_PLAYERS; i++) {
        if(players[i].id == 0 || players[i].alive) continue;
        int index = GET_I(players[i].row, players[i].col, map_width);
        draw_cell(index, '1' + i);
    }
}
void draw_text(int x, int y, char *text) {

}
void draw() {
    if(game_status == GAME_LOBBY){
        draw_text(map_width / 2, map_height / 2 - 2, "Lobby");
    }else if(game_status == GAME_RUNNING){
        draw_map();
        draw_bombs();
        draw_players();
    }else if(game_status == GAME_END){
        char buf[20];
        sprintf(buf, "Winner: %d", winner);
        draw_text(map_width / 2, map_height / 2 - 5, buf);
    }
}

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
    my_id = target_id;
    plis[my_id] = 0;
    players[pli(my_id)].id = 0;

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
        for(int i = 0; i < p->player_count; i++) {
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
        winner = pli(p->id) + 1;
        game_status = GAME_END;
    } break;
    case MSG_MOVED : {
        payload_moved_t *p = (payload_moved_t*)payload;
        int i = pli(p->player_id);
        int row = p->coord % map_width;
        int col = p->coord / map_width;
        players[i].row = row;
        players[i].col = col;
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
            map[p->coord] = 's';
            break;
        case BONUS_RADIUS:
            map[p->coord] = 'r';
            break;
        case BONUS_TIMER:
            map[p->coord] = 't';
            break;
        default: break;
        }
    } break;
    case MSG_BONUS_RETRIEVED : {
        payload_bonus_retrieved_t *p = (payload_bonus_retrieved_t*)payload;
        map[p->coord] = ' ';
    } break;
    case MSG_BLOCK_DESTROYED : {
        payload_block_destroyed_t *p = (payload_block_destroyed_t*)payload;
        map[p->coord] = ' ';
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



        // draw the thing
        draw();

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
 } */

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

/* ===================== DRAW ===================== */

void draw_cell(int coord, char pixel) {
    int x = coord % map_width;
    int y = coord / map_width;
    mvaddch(y, x, pixel);
}

void draw_text(int x, int y, char *text) {
    mvprintw(y, x, "%s", text);
}

void draw_map() {
    for(int i = 0; i < map_width * map_height; i++) {
        draw_cell(i, map[i]);
    }
}

/* ==== FIXED EXPLOSION ==== */
void draw_bombs() {
    for(int i = 0; i < bomb_count; i++) {
        int row = bombs[i].row;
        int col = bombs[i].col;
        int radius = bombs[i].radius;

        int center = GET_I(row, col, map_width);

        if(radius == 0) {
            draw_cell(center, 'o');
        } else {
            draw_cell(center, '*');

            for(int r = 1; r <= radius; r++) {

                if(row - r >= 0)
                    draw_cell(GET_I(row - r, col, map_width), '|');

                if(row + r < map_height)
                    draw_cell(GET_I(row + r, col, map_width), '|');

                if(col - r >= 0)
                    draw_cell(GET_I(row, col - r, map_width), '-');

                if(col + r < map_width)
                    draw_cell(GET_I(row, col + r, map_width), '-');
            }
        }
    }
}

void draw_players() {
    for(int i = 0; i < MAX_PLAYERS; i++) {
        if(players[i].id == 0 || !players[i].alive) continue;

        int index = GET_I(players[i].row, players[i].col, map_width);
        draw_cell(index, '1' + i);
    }
}

void draw() {
    clear();

    if(game_status == GAME_LOBBY){
        draw_text(2, 2, "Lobby");
    }
    else if(game_status == GAME_RUNNING){
        draw_map();
        draw_bombs();
        draw_players();
    }
    else if(game_status == GAME_END){
        char buf[50];
        sprintf(buf, "Winner: %d", winner);
        draw_text(2, 2, buf);
    }

    refresh();
}

/* ===================== INPUT ===================== */

void handle_input() {
    int ch = getch();

    switch(ch) {
        case KEY_UP:    send_try_move(0); break;
        case KEY_DOWN:  send_try_move(1); break;
        case KEY_LEFT:  send_try_move(2); break;
        case KEY_RIGHT: send_try_move(3); break;

        case ' ':
            if(player_count > 0) {
                int coord = players[pli(my_id)].row +
                            players[pli(my_id)].col * map_width;
                send_try_bomb(coord);
            }
            break;
    }
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

/* ===================== CLIENT ===================== */

void client(char *name, char *ip, int port) {
    srand(time(0));
    init_ui();

    char _tmp_name[30];
    if(name == NULL) {
        name = _tmp_name;
        for(int i = 0; i < 20; i++)
            name[i] = 'a' + rand()%26;
        name[20] = 0;
    }

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

    packet_t p;
    p.header = make_header(MSG_HELLO, my_id, 255);
    strcpy(p.payload.hello.version, VERSION);
    strcpy(p.payload.hello.name, name);
    send_packet_simple(server_socket, &p);

    char buffer[100000];

    while (1) {

        handle_input();
        draw();

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(server_socket, &rfds);

        struct timeval tv = {0, 10000};

        int ready = select(server_socket + 1, &rfds, NULL, NULL, &tv);
        if (ready <= 0) continue;

        ssize_t n = read(server_socket, buffer, sizeof(buffer));
        if (n <= 0) break;

        uint8_t msg_type  = buffer[0];
        uint8_t sender_id = buffer[1];
        uint8_t target_id = buffer[2];
        payload_t *payload = (payload_t*)(buffer + sizeof(msg_generic_t));

        if(handle_packet(msg_type, sender_id, target_id, payload))
            break;
    }

    shutdown_ui();
    close(server_socket);
}
