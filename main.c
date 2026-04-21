
#include "server.h"
#include "client.h"

#include <stdlib.h>


int main(int argc, char **argv) {

    if(argc > 1 && argv[1][0] == '-' && argv[1][1] == 's') {
        server();
    } else {
        // default values
        const char *ip = "127.0.0.1";
        int port = PORT;

        if(argc >= 3) {
            ip = argv[1];
            port = atoi(argv[2]);
        }

        client(ip, port);
    }

    return 0;
}
