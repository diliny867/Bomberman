
#pragma once

#include "config.h"


#define PORT 12346
#define VERSION "aboba1"

#define OUT_QUEUE_SIZE     100
#define SHARED_QUEUE_SIZE  100

static int out_queue_count = 0;
static packet_t out_queue[OUT_QUEUE_SIZE];

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

    int shared_queue_count;
    packet_t shared_queue[SHARED_QUEUE_SIZE];

    int start_timeout;
    int bonus_timeout;
    int bonus_count;
} shared_state_t;

static shared_state_t *ss;

static int pings[MAX_PLAYERS + 1]; // at MAX_PLAYERS is server

#define GET_I(x, y, width) ((y) * (width) + (x))
#define pli(id) ss->plis[(id)]

#define celli_to_i(i)   (ss->map[i] - '1')
#define id_to_celli(id) (pli(id) + '1')

#define arr_erase(arr, i, count) memmove((arr), (arr) + 1, ((count) - (i) - 1) * sizeof(arr[0]))


void server();


int get_payload_size(uint8_t type);


int clampi_min(int val, int min);
int clampi_max(int val, int max);
int clampi(int val, int min, int max);