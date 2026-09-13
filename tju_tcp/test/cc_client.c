#include "tju_tcp.h"
#include <string.h>
#include <stdlib.h>

/* 拥塞控制实验客户端：向服务端发送指定字节数（默认 1MB）。 */
int main(int argc, char **argv) {
    int nbytes = 1024 * 1024;
    if(argc > 1) nbytes = atoi(argv[1]);
    if(nbytes < SMSS) nbytes = 64 * 1024;

    startSimulation();

    tju_tcp_t* my_socket = tju_socket();
    tju_sock_addr target_addr;
    target_addr.ip = inet_network(TJU_SERVER_IP);
    target_addr.port = 1234;
    if(tju_connect(my_socket, target_addr) != 0){
        printf("[CC] connect failed\n");
        return EXIT_FAILURE;
    }

    char buf[4096];
    memset(buf, 'C', sizeof(buf));
    int sent = 0;
    while(sent < nbytes){
        int n = nbytes - sent;
        if(n > (int)sizeof(buf)) n = (int)sizeof(buf);
        if(tju_send(my_socket, buf, n) != 0) break;
        sent += n;
    }
    printf("[CC] client sent %d bytes\n", sent);
    fflush(stdout);
    tju_close(my_socket);
    return EXIT_SUCCESS;
}
