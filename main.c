
#include "server.h"
#include "client.h"

#include <stdlib.h>


int main(int argc, char **argv) {

    if(argc > 1 && argv[1][0] == '-' && argv[1][1] == 's') {
        int port = PORT;
        if(argc > 2)
            port = atoi(argv[2]);
        server(port);
    } else {
        // default values
        char *name = NULL;
        char *ip = "127.0.0.1";
        int port = PORT;

        if(argc > 1)
            ip = argv[1];
        if(argc > 2)
            port = atoi(argv[2]);
        if(argc > 3)
            name = argv[3];

        client(name, ip, port);
    }

    return 0;
}
