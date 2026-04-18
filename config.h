
#include <stdint.h>
#include <stdbool.h>

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
    BONUS_TIMER = 3
} bonus_type_t;

typedef enum {
    MSG_HELLO = 0,
    MSG_WELCOME = 1,
    MSG_DISCONNECT = 2,
    MSG_PING = 3,
    MSG_PONG = 4,
    MSG_LEAVE = 5,
    MSG_ERROR = 6,
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
    char server_id[20];
    uint8_t game_status;
    uint8_t players_count;
    struct {
        uint8_t ready;
        char name[30];
    } players[];
} payload_welcome_t; 
typedef struct {
    char error[];
} payload_error_t;
typedef struct {
    uint8_t game_status;
} payload_set_status_t;
typedef struct {
    uint8_t winnder_id;
} payload_winner_t;
typedef struct {
    uint8_t width;
    uint8_t height;
    uint8_t map[];
} payload_map_t;
uint8_t move_direction;
typedef struct {
    uint8_t player_id;
    uint16_t new_coord;
} payload_moved_t;
typedef struct {
    uint16_t bomb_coord;
} payload_bomb_attempt_t;
typedef struct {
    uint8_t source_player_id;
    uint16_t bomb_coord;
} payload_bomb_t;
typedef struct {
    uint8_t radius;
    uint16_t bomb_coord;
} payload_explosion_start_t;
typedef struct {
    uint16_t bomb_coord;
} payload_explosion_end_t;
typedef struct {
    uint8_t death_id;
} payload_death_t;
typedef struct {
    uint8_t bonus_type;
    uint16_t bonus_coord;
} payload_new_bonus_t;
typedef struct {
    uint8_t player_id;
    uint16_t bonus_coord;
} payload_bonus_retrieved_t;
typedef struct {
    uint16_t block_coord;
} payload_block_destroyed_t;


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
} player_t;

typedef struct {
    bool active;
    uint8_t owner_id;
    uint16_t row;
    uint16_t col;
    uint8_t radius;
    uint16_t timer_ticks;
    uint16_t linger;
} bomb_t;

#define CELL_EMPTY    '.'
#define CELL_HARD     'H'
#define CELL_SOFT     'S'
#define CELL_BOMB     'B'
#define CELL_SPEEDUP  'A'
#define CELL_RADIUSUP 'R'
#define CELL_TICKUP   'T'
