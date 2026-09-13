#include "tju_tcp.h"
#include <string.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    startSimulation();

    tju_tcp_t* my_server = tju_socket();
    tju_sock_addr bind_addr;
    bind_addr.ip = inet_network(TJU_SERVER_IP);
    bind_addr.port = 1234;
    tju_bind(my_server, bind_addr);
    tju_listen(my_server);
    tju_tcp_t* new_conn = tju_accept(my_server);

    /* 先不读，让接收缓冲填满、通告窗口降到 0 */
    sleep(6);

    char buf[256];
    int total = 0;
    while(total < 20 * 1024){
        int n = tju_recv(new_conn, buf, 256);
        if(n <= 0) break;
        total += n;
        usleep(150000);
    }
    printf("[FLOW] server got %d bytes\n", total);
    fflush(stdout);
    tju_close(new_conn);
    return EXIT_SUCCESS;
}
