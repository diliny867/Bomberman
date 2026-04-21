#include "config.h"

int pings[256];

int clampi_min(int val, int min) {
    return val < min ? min : val;
}
int clampi_max(int val, int max) {
    return val > max ? max : val;
}
int clampi(int val, int min, int max) {
    return clampi_max(clampi_min(val, min), max);
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

void send_simple(int socket, uint8_t msg_type, uint8_t sender_id, uint8_t target_id) {
    packet_t packet;
    packet.header.msg_type = msg_type;
    packet.header.sender_id = sender_id;
    packet.header.target_id = target_id;
    send_packet_simple(socket, &packet);
}

void send_ping(int socket, uint8_t target_id, uint8_t sender_id, bool send_pong) {
    send_simple(socket, send_pong ? MSG_PONG : MSG_PING, sender_id, target_id);
}
