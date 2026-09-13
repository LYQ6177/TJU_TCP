#include "tju_tcp.h"
#include <string.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    startSimulation();

    tju_tcp_t* my_socket = tju_socket();
    tju_sock_addr target_addr;
    target_addr.ip = inet_network(TJU_SERVER_IP);
    target_addr.port = 1234;
    tju_connect(my_socket, target_addr);

    char buf[1024];
    memset(buf, 'A', sizeof(buf));
    int i;
    for(i = 0; i < 20; i++){
        tju_send(my_socket, buf, 1024);
    }
    tju_close(my_socket);
    return EXIT_SUCCESS;
}
