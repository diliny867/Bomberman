
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

    return 0;
}