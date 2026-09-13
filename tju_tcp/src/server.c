#include "tju_tcp.h"
#include <string.h>

int main(int argc, char **argv) {
    // 开启仿真环境 
    startSimulation();

    tju_tcp_t* my_server = tju_socket();
    // printf("my_tcp state %d\n", my_server->state);
    
    tju_sock_addr bind_addr;
    bind_addr.ip = inet_network(TJU_SERVER_IP);
    bind_addr.port = 1234;

    tju_bind(my_server, bind_addr);

    tju_listen(my_server);
    // printf("my_server state %d\n", my_server->state);

    tju_tcp_t* new_conn = tju_accept(my_server);

    int handshake_only = (argc > 1 && strcmp(argv[1], "hs") == 0);
    if(!handshake_only){
        sleep(5);
        tju_send(new_conn, "hello world", 12);
        tju_send(new_conn, "hello tju", 10);

        char buf[2021];
        tju_recv(new_conn, (void*)buf, 12);
        printf("server recv %s\n", buf);

        tju_recv(new_conn, (void*)buf, 10);
        printf("server recv %s\n", buf);

        /* 延后关闭，便于观察客户端主动关闭、服务端被动关闭的先后挥手 */
        sleep(3);
    }else{
        sleep(2);
    }
    tju_close(new_conn);

    return EXIT_SUCCESS;
}
