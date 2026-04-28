
#pragma once

#define _GNU_SOURCE
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>


#define PORT 12344
#define VERSION "aboba1"

//#define log(format, ...) printf(format __VA_OPT__(,) __VA_ARGS__)
#define log(...) printf(__VA_ARGS__)
// #define log(...) 

// #define log2(...) log(__VA_ARGS__)
#define log2(...)

#define MAX_PLAYERS 8
#define TICKS_PER_SECOND 20

#define MAP_WIDTH_MAX  255
#define MAP_HEIGHT_MAX 255
#define MAP_SIZE_MAX   (MAP_WIDTH_MAX * MAP_HEIGHT_MAX)

typedef enum {
    GAME_LOBBY = 0,
    GAME_RUNNING = 1,
    GAME_END = 2
} game_status_t;

typedef enum {
    DIR_UP = 0,
    DIR_DOWN = 1,
    DIR_LEFT = 2,
    DIR_RIGHT = 3
} direction_t;

typedef enum {
    BONUS_NONE = 0,
    BONUS_SPEED = 1,
    BONUS_RADIUS = 2,
    BONUS_TIMER = 3,
    BONUS_BOMB = 4
} bonus_type_t;

typedef enum {
    MSG_HELLO = 0,
    MSG_WELCOME = 1,
    MSG_DISCONNECT = 2,
    MSG_PING = 3,
    MSG_PONG = 4,
    MSG_LEAVE = 5,
    MSG_ERROR = 6,
    MSG_MAP = 7,
    MSG_SET_READY = 10,
    MSG_SET_STATUS = 20,
    MSG_WINNER = 23,
    MSG_MOVE_ATTEMPT = 30,
    MSG_BOMB_ATTEMPT = 31,
    MSG_MOVED = 40,
    MSG_BOMB = 41,
    MSG_EXPLOSION_START = 42,
    MSG_EXPLOSION_END = 43,
    MSG_DEATH = 44,
    MSG_BONUS_AVAILABLE = 45,
    MSG_BONUS_RETRIEVED = 46,
    MSG_BLOCK_DESTROYED = 47
} msg_type_t;

typedef struct {
    uint8_t msg_type;
    uint8_t sender_id;
    uint8_t target_id;
} msg_generic_t;


typedef struct {
    char version[20];
    char name[30];
} payload_hello_t;
typedef struct {
    char version[20];
    uint8_t game_status;
    uint8_t player_count;
    struct {
        uint8_t id;
        uint8_t i;
        bool ready;
        char name[30];
    } players[MAX_PLAYERS];
    // } players[];
} payload_welcome_t; 
typedef struct {
    //char *error;
    char error[];
} payload_error_t;
typedef struct {
    uint8_t game_status;
} payload_set_status_t;
typedef struct {
    uint8_t id;
} payload_winner_t;
typedef struct {
    uint8_t width;
    uint8_t height;
    uint8_t map[MAP_SIZE_MAX];
} payload_map_t;
typedef struct {
    uint8_t direction;
} payload_move_attempt_t;
typedef struct {
    uint8_t player_id;
    uint16_t coord;
} payload_moved_t;
typedef struct {
    uint16_t coord;
} payload_bomb_attempt_t;
typedef struct {
    uint8_t player_id;
    uint16_t coord;
} payload_bomb_t;
typedef struct {
    uint8_t radius;
    uint16_t coord;
} payload_explosion_start_t;
typedef struct {
    uint16_t coord;
} payload_explosion_end_t;
typedef struct {
    uint8_t death_id;
} payload_death_t;
typedef struct {
    uint8_t type;
    uint16_t coord;
} payload_bonus_available_t;
typedef struct {
    uint8_t player_id;
    uint16_t coord;
} payload_bonus_retrieved_t;
typedef struct {
    uint16_t coord;
} payload_block_destroyed_t;

typedef union {
    payload_hello_t hello;
    payload_welcome_t welcome;
    payload_error_t error;
    payload_set_status_t set_status;
    payload_winner_t winner;
    payload_map_t map;
    payload_move_attempt_t move_attempt;
    payload_moved_t moved;
    payload_bomb_attempt_t bomb_attempt;
    payload_bomb_t bomb;
    payload_explosion_start_t explosion_start;
    payload_explosion_end_t explosion_end;
    payload_death_t death;
    payload_bonus_available_t bonus_available;
    payload_bonus_retrieved_t bonus_retrieved;
    payload_block_destroyed_t block_destroyed;
} payload_t;


typedef struct {
    uint8_t id;
    char name[30];
    uint16_t row;
    uint16_t col;
    bool alive;
    bool ready;

    uint8_t bomb_count;
    uint8_t bomb_radius;
    uint16_t bomb_timer_ticks;
    uint16_t speed;

    uint8_t max_bombs;
    uint16_t move_ticks;
} player_t;

typedef struct {
    bool active;
    uint8_t owner_id;
    uint16_t row;
    uint16_t col;
    uint8_t radius;
    uint16_t timer_ticks;
} bomb_t;

#define CELL_EMPTY    '.'
#define CELL_HARD     '@'
#define CELL_SOFT     '#'
#define CELL_BOMB     'O'
#define CELL_SPEEDUP  's'
#define CELL_RADIUSUP 'r'
#define CELL_TICKUP   't'
#define CELL_BOMBUP   'b'

typedef struct {
    msg_generic_t header;
    payload_t payload;
} __attribute__((packed)) packet_t;

// useful and common(for client and server) functions/variables
extern int pings[256];

#define GET_I(x, y, width) ((y) * (width) + (x))

#define celli_to_i(cell, i)   ((uint8_t)cell - '1')
#define id_to_celli(id) (pli(id) + '1')

#define arr_erase(arr, i, count) memmove((arr), (arr) + 1, ((count) - (i) - 1) * sizeof(arr[0]))

int clampi_min(int val, int min);
int clampi_max(int val, int max);
int clampi(int val, int min, int max);

void dump_bytes(void *data, int size);

msg_generic_t make_header(uint8_t msg_type, uint8_t sender_id, uint8_t target_id);

int get_payload_size(uint8_t type);

void send_packet_simple(int socket, packet_t *packet);

void send_simple(int socket, uint8_t msg_type, uint8_t sender_id, uint8_t target_id);
void send_ping(int socket, uint8_t target_id, uint8_t sender_id, bool send_pong);
