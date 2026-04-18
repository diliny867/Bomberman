
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


typedef struct {
    uint8_t game_status;

    uint16_t player_speed;
    uint16_t bomb_linger;
    uint8_t bomb_radius;
    uint16_t bomb_timer_ticks;

    uint8_t map_width;
    uint8_t map_height;
    uint8_t map[MAP_SIZE_MAX];

    uint16_t bombs_count;
    bomb_t bombs[MAP_SIZE_MAX];

    uint16_t player_count;
    player_t players[MAX_PLAYERS];
} shared_state_t;

shared_state_t *ss;

static inline void make_shared_memory(){
    ss = mmap(NULL, sizeof(shared_state_t), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
}


void read_map(char *name, uint8_t *width, uint8_t *height, uint8_t *map,
        uint16_t *player_speed, uint16_t *bomb_linger, uint8_t *bomb_radius, uint16_t *bomb_timer_ticks){
    FILE *fin = fopen(name, "r");

    fscanf("%"SCNu8"%"SCNu8, width, height);
    fscanf("%"SCNu16"%"SCNu16"%"SCNu8"%"SCNu16, player_speed, bomb_linger, bomb_radius, bomb_timer_ticks);

    int size = width * height;
    for(int i = 0; i < size;){
        int c = fgetc(fin);
        if(c == EOF) break;
        if(c == ' ') continue;
        map[i] = c;
        i++;
    }

    fclose(fin);
}


void main_loop(){
    printf("Loop:\n");

    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);

    int i = 0;
    while(1){
        // for(i = 0; i < *client_count; i++){
        //     shared_data[MAX_CLIENTS + i] += shared_data[i];
        //     shared_data[i] = 0;
        // }

        next.tv_nsec += 1000000000 / TICKS_PER_SECOND;
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
}

void process_client(int id, int socket){
    printf("Processing client: %d %d\n", id, socket);
    printf("Client count: %d\n", ss->player_count);

    char in[1];
    char out[100];
    while(1){
        read(socket, in, 1);
        // if(in[0] >= '0' && in[0] <= '9'){
        //     sprintf(out, "Client %d sum: %d\n", id, shared_data[MAX_CLIENTS + id]);
        //     write(socket, out, strlen(out));
        //     int num = (int)in[0] - '0';
        //     printf("Client %d read number: %d\n", id, num);
        //     shared_data[id] = num;
        // }
    }
}

void main_networking(){
    int main_socket;
    struct sockaddr_in server_address;
    int client_socket;
    struct sockaddr_in client_address;
    int client_address_size = sizeof(client_address);

    main_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(main_socket < 0){
        printf("Error opening server socket\n");
        exit(1);
    }
    printf("Server socket created\n");

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);

    if(bind(main_socket, (struct sockaddr*)&server_address, sizeof(server_address)) < 0){
        printf("Error binding server socket\n");
        exit(1);
    }
    printf("Server socket binded\n");

    if(listen(main_socket, MAX_CLIENTS) < 0){
        printf("Error listening to server socket\n");
        exit(1);
    }
    printf("Listening to server socket\n");

    while(1){
        client_socket = accept(main_socket, (struct sockaddr*)&client_address, &client_address_size);
        if(client_socket < 0){
            printf("Error accepting client: %d\n", errno);
            continue;
        }

        int new_client_id = ss->player_count++;
        int cpid = fork();

        if(cpid == 0){
            close(main_socket);
            cpid = fork();
            if(cpid == 0){
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

void server(){
    make_shared_memory();

    int pid = fork();
    if(pid == 0){
        main_networking();
    }else{
        main_loop();
    }    
}

int main(int argc, char **argv){

    server();

    return 0;
}