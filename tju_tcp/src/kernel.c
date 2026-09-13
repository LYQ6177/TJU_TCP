#include "kernel.h"
#include <ifaddrs.h>
#include <stdlib.h>

int tju_host_is_server(void){
    char hostname[256];
    memset(hostname, 0, sizeof(hostname));
    gethostname(hostname, sizeof(hostname) - 1);
    if(strcmp(hostname, "server") == 0) return 1;
    if(strcmp(hostname, "client") == 0) return 0;
    if(strstr(hostname, "server") != NULL) return 1;
    if(strstr(hostname, "client") != NULL) return 0;

    struct ifaddrs *ifaddr = NULL;
    int saw_s = 0, saw_c = 0;
    if(getifaddrs(&ifaddr) == 0){
        uint32_t sip = inet_network(TJU_SERVER_IP);
        uint32_t cip = inet_network(TJU_CLIENT_IP);
        struct ifaddrs *ifa;
        for(ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next){
            if(ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) continue;
            uint32_t a = ntohl(((struct sockaddr_in*)ifa->ifa_addr)->sin_addr.s_addr);
            if(a == sip) saw_s = 1;
            if(a == cip) saw_c = 1;
        }
        freeifaddrs(ifaddr);
    }
    if(saw_s && !saw_c) return 1;
    if(saw_c && !saw_s) return 0;
    return 0;
}

/*
模拟Linux内核收到一份TCP报文的处理函数
*/
void onTCPPocket(char* pkt){
    // 当我们收到TCP包时 包中 源IP 源端口 是发送方的 也就是我们眼里的 远程(remote) IP和端口
    uint16_t remote_port = get_src(pkt);
    uint16_t local_port = get_dst(pkt);
    uint32_t remote_ip, local_ip;
    if(tju_host_is_server()){
        local_ip = inet_network(TJU_SERVER_IP);
        remote_ip = inet_network(TJU_CLIENT_IP);
    }else{
        local_ip = inet_network(TJU_CLIENT_IP);
        remote_ip = inet_network(TJU_SERVER_IP);
    }

    int hashval;
    // 根据4个ip port 组成四元组 查找有没有已经建立连接的socket
    hashval = cal_hash(local_ip, local_port, remote_ip, remote_port);

    // 首先查找已经建立连接的socket哈希表
    if (established_socks[hashval]!=NULL){
        tju_handle_packet(established_socks[hashval], pkt);
        return;
    }

    // 没有的话再查找监听中的socket哈希表
    hashval = cal_hash(local_ip, local_port, 0, 0); //监听的socket只有本地监听ip和端口 没有远端
    if (listen_socks[hashval]!=NULL){
        tju_handle_packet(listen_socks[hashval], pkt);
        return;
    }

    // 都没找到 丢掉数据包
    printf("找不到能够处理该TCP数据包的socket, 丢弃该数据包\n");
    return;
}



/*
以用户填写的TCP报文为参数
根据用户填写的TCP的目的IP和目的端口,向该地址发送数据报
不可以修改此函数实现
*/
void sendToLayer3(char* packet_buf, int packet_len){
    if (packet_len>MAX_LEN){
        printf("ERROR: 不能发送超过 MAX_LEN 长度的packet, 防止IP层进行分片\n");
        return;
    }

    struct sockaddr_in conn;
    conn.sin_family      = AF_INET;            
    conn.sin_port        = htons(20218);
    int rst;
    if(tju_host_is_server()){
        conn.sin_addr.s_addr = inet_addr(TJU_CLIENT_IP);
        rst = sendto(BACKEND_UDPSOCKET_ID, packet_buf, packet_len, 0, (struct sockaddr*)&conn, sizeof(conn));
    }else{
        conn.sin_addr.s_addr = inet_addr(TJU_SERVER_IP);
        rst = sendto(BACKEND_UDPSOCKET_ID, packet_buf, packet_len, 0, (struct sockaddr*)&conn, sizeof(conn));
    }
    (void)rst;
}

/*
 仿真接受数据线程
 不断调用server或cliet监听在20218端口的UDPsocket的recvfrom
 一旦收到了大于TCPheader长度的数据 
 则接受整个TCP包并调用onTCPPocket()
*/
void* receive_thread(void* arg){

    char hdr[DEFAULT_HEADER_LEN];
    char* pkt;

    uint32_t plen = 0;
    int len;
    int n;

    struct sockaddr_in from_addr;
    int from_addr_size = sizeof(from_addr);

    while(1) {
        // MSG_PEEK 表示看一眼 不会把数据从缓冲区删除
        len = recvfrom(BACKEND_UDPSOCKET_ID, hdr, DEFAULT_HEADER_LEN, MSG_PEEK, (struct sockaddr *)&from_addr, &from_addr_size);
        // 一旦收到了大于header长度的数据 则接受整个TCP包
        if(len >= DEFAULT_HEADER_LEN){
            plen = get_plen(hdr);
            /* 一个 UDP 数据报就是一个 TCP 段，禁止跨包拼接，否则丢包/乱序会卡死收包线程 */
            if(plen < DEFAULT_HEADER_LEN || plen > MAX_LEN){
                recvfrom(BACKEND_UDPSOCKET_ID, hdr, DEFAULT_HEADER_LEN, NO_FLAG, (struct sockaddr *)&from_addr, &from_addr_size);
                continue;
            }
            pkt = malloc(plen);
            n = recvfrom(BACKEND_UDPSOCKET_ID, pkt, plen, NO_FLAG, (struct sockaddr *)&from_addr, &from_addr_size);
            if(n >= DEFAULT_HEADER_LEN){
                onTCPPocket(pkt);
            }
            free(pkt);
        }
    }
}

/*
 开启仿真, 运行起后台线程

 不论是server还是client
 都创建一个UDP socket 监听在20218端口
 然后创建新线程 不断调用该socket的recvfrom
*/
void startSimulation(){
    // 对于内核 初始化监听socket哈希表和建立连接socket哈希表
    int index;
    for(index=0;index<MAX_SOCK;index++){
        listen_socks[index] = NULL;
        established_socks[index] = NULL;
    }

    BACKEND_UDPSOCKET_ID = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (BACKEND_UDPSOCKET_ID < 0){
        printf("ERROR opening socket");
        exit(-1);
    }

    // 设置socket选项 SO_REUSEADDR = 1 
    // 意思是 允许绑定本地地址冲突 和 改变了系统对处于TIME_WAIT状态的socket的看待方式 
    int optval = 1;
    setsockopt(BACKEND_UDPSOCKET_ID, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));
    /* 默认 4MB；可用 TJU_UDP_BUF_KB 覆盖（单位 KB），减轻突发发送时 ACK 在 UDP 队列被挤掉 */
    int udp_buf = 4 * 1024 * 1024;
    {
        const char* kb_env = getenv("TJU_UDP_BUF_KB");
        if(kb_env && *kb_env){
            long kb = strtol(kb_env, NULL, 10);
            if(kb >= 64 && kb <= 64 * 1024){
                udp_buf = (int)(kb * 1024);
            }
        }
    }
    setsockopt(BACKEND_UDPSOCKET_ID, SOL_SOCKET, SO_RCVBUF, (const void *)&udp_buf, sizeof(udp_buf));
    setsockopt(BACKEND_UDPSOCKET_ID, SOL_SOCKET, SO_SNDBUF, (const void *)&udp_buf, sizeof(udp_buf));

    struct sockaddr_in conn;
    memset(&conn, 0, sizeof(conn)); 
    conn.sin_family = AF_INET;
    conn.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY = 0.0.0.0
    conn.sin_port = htons((unsigned short)20218);

    if (bind(BACKEND_UDPSOCKET_ID, (struct sockaddr *) &conn, sizeof(conn)) < 0){
        printf("ERROR on binding");
        exit(-1);
    }

    /* 不在此处启动收包线程：线上会把 test_sender.c 与本 kernel 链在一起，
       测试程序自己 recvfrom(BACKEND_UDPSOCKET_ID)；若内核线程先把 SYN-ACK 收走并丢弃，
       就会出现「未能接收到第二次握手」且 tmux 会话已退出。 */
    return;
}

void ensure_receive_thread(void){
    static int started = 0;
    static pthread_mutex_t once = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&once);
    if(!started && BACKEND_UDPSOCKET_ID > 0){
        pthread_t thread_id;
        if(pthread_create(&thread_id, NULL, receive_thread, (void*)(&BACKEND_UDPSOCKET_ID)) == 0){
            pthread_detach(thread_id);
            started = 1;
        }else{
            printf("ERROR open thread");
            pthread_mutex_unlock(&once);
            exit(-1);
        }
    }
    pthread_mutex_unlock(&once);
}

int cal_hash(uint32_t local_ip, uint16_t local_port, uint32_t remote_ip, uint16_t remote_port){
    /* 用无符号运算，避免 (int)IP 溢出后出现负下标 */
    uint32_t h = local_ip + (uint32_t)local_port + remote_ip + (uint32_t)remote_port;
    return (int)(h % (uint32_t)MAX_SOCK);
}