
#include "server.h"
#include "client.h"


int main(int argc, char **argv) {

    if(argc <= 1) {
        client();
    }else {
        if(argv[1][0] == '-' && argv[1][1] == 's') {
            server();
        }else {
            client();
        }
    }

    /*if(argc > 1 && argv[1][0] == '-' && argv[1][1] == 's') {
        server();
    } else {
        // default values
        const char *ip = "127.0.0.1";
        int port = 8080;

        if(argc >= 3) {
            ip = argv[1];
            port = atoi(argv[2]);
        }

        client(ip, port);
    }*/

    return 0;
}
