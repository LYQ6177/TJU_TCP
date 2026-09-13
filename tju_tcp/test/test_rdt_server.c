#include "tju_tcp.h"
#include <string.h>
#include <signal.h>
#include <stdlib.h>

void sleep_no_wake(int sec){
    do{
        printf("Interrupted\n");
        sec =sleep(sec);
    }while(sec > 0);
}

int main(int argc, char **argv) {
    startSimulation();

    tju_tcp_t* my_server = tju_socket();

    tju_sock_addr bind_addr;
    bind_addr.ip = inet_network("172.17.0.3");
    bind_addr.port = 1234;

    tju_bind(my_server, bind_addr);

    tju_listen(my_server);

    tju_tcp_t* new_conn = tju_accept(my_server);

    int perf = getenv("TJU_PERF_MODE") != NULL;
    if(!perf) sleep_no_wake(8);

    if(argc > 1 && strcmp(argv[1], "short") == 0){
        int i;
        for (i=0; i<50; i++){
            char buf[16];
            tju_recv(new_conn, (void*)buf, 16);
            printf("[RDT TEST] server recv %s", buf);
            fflush(stdout);
        }
    }else{
        char buf[4096];
        int total = 0;
        while(1){
            int n = tju_recv(new_conn, buf, sizeof(buf));
            if(n <= 0) break;
            total += n;
        }
        printf("[RDT TEST] server recv %d bytes\n", total);
        fflush(stdout);
    }
    if(perf){
        tju_close(new_conn);
        return EXIT_SUCCESS;
    }
    sleep_no_wake(100);

    return EXIT_SUCCESS;
}
