#include "tju_tcp.h"
#include <string.h>
#include <stdlib.h>

void sleep_no_wake(int sec){
    do{
        sec =sleep(sec);
    }while(sec > 0);
}

int main(int argc, char **argv) {
    startSimulation();

    tju_tcp_t* my_socket = tju_socket();

    tju_sock_addr target_addr;
    target_addr.ip = inet_network("172.17.0.3");
    target_addr.port = 1234;

    tju_connect(my_socket, target_addr);

    int perf = getenv("TJU_PERF_MODE") != NULL;
    if(!perf) sleep_no_wake(8);

    if(argc > 1 && strcmp(argv[1], "short") == 0){
        int i;
        for(i=0;i<50;i++){
            char buf[16];
            sprintf(buf , "test message%d\n", i);
            tju_send(my_socket, buf, 16);
        }
    }else{
        int nseg = 8192;
        const char* es = getenv("TJU_PERF_SEGS");
        if(es){
            int v = atoi(es);
            if(v > 0) nseg = v;
        }
        char buf[1375];
        memset(buf, 'A', sizeof(buf));
        int i;
        for(i = 0; i < nseg; i++){
            tju_send(my_socket, buf, 1375);
        }
        printf("[RDT] client sent %d bytes\n", nseg * 1375);
        fflush(stdout);
    }
    if(perf){
        tju_close(my_socket);
        return EXIT_SUCCESS;
    }
    sleep_no_wake(100);

    return EXIT_SUCCESS;
}
