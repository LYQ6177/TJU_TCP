#ifndef _GLOBAL_H_
#define _GLOBAL_H_

#include <netinet/in.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "global.h"
#include <pthread.h>
#include <sys/select.h>
#include <arpa/inet.h>

/* 仿真两端 IP。本地 Vagrant：client 172.17.0.2、server 172.17.0.3。
   评测平台改为：
   #define TJU_CLIENT_IP "172.17.0.5"
   #define TJU_SERVER_IP "172.17.0.6" */
#define TJU_CLIENT_IP "172.17.0.2"
#define TJU_SERVER_IP "172.17.0.3"

// 单位是byte
#define SIZE32 4
#define SIZE16 2
#define SIZE8  1

// 一些Flag
#define NO_FLAG 0
#define NO_WAIT 1
#define TIMEOUT 2
#define TRUE 1
#define FALSE 0

// 定义最大包长 防止IP层分片
#define MAX_DLEN 1375 	// 最大包内数据长度
#define MAX_LEN 1400 	// 最大包长度

// TCP socket 状态定义
#define CLOSED 0
#define LISTEN 1
#define SYN_SENT 2
#define SYN_RECV 3
#define ESTABLISHED 4
#define FIN_WAIT_1 5
#define FIN_WAIT_2 6
#define CLOSE_WAIT 7
#define CLOSING 8
#define LAST_ACK 9
#define TIME_WAIT 10

// TCP 拥塞控制状态
#define SLOW_START 0
#define CONGESTION_AVOIDANCE 1
#define FAST_RECOVERY 2

// TCP 接受窗口大小
#define TCP_RECVWN_SIZE 32*MAX_DLEN // 比如最多放32个满载数据包

/* 可靠传输 / 流量控制（说明书 §5.3–5.4、§7） */
#define SMSS 1375
#define SEND_BUF_SIZE (2048 * SMSS)
#define RECV_BUF_SIZE (2048 * SMSS)
#define MAX_INFLIGHT 256
#define RTO_MIN_US 100000
#define RTO_MAX_US 4000000
#define CLOCK_G_US 1000
#define ADV_WND_MAX 65535
#define DUPACK_THRESH 3
/* 16 位通告窗口饱和（65535）时，实际接收缓冲可远大于字段上限（§7.3）。
   飞行窗口按本端缓冲能力取值，避免把更大窗口截断塞进 16 位字段（§7.4）。 */
#define SEND_FLIGHT_MAX (256 * SMSS)
#define SEND_BURST_MAX 32
/* RFC 5681：IW = min(4*SMSS, max(2*SMSS, 4380))；SMSS=1375 时为 4380。
   第一阶段设计取更保守的 1*SMSS，增长不比 RFC 更激进，慢启动曲线更清晰。 */
#define INIT_CWND SMSS
#define INIT_SSTHRESH ADV_WND_MAX
#define CWND_TRACE_SS 0
#define CWND_TRACE_CA 1
#define CWND_TRACE_FR 2
#define CWND_TRACE_TO 3

typedef struct tju_seg {
	uint32_t seq;
	uint16_t dlen;
	uint16_t plen;
	char* pkt;
	struct timeval sent_at;
	int retransmitted;
} tju_seg_t;

typedef struct tju_ofo {
	uint32_t seq;
	uint16_t len;
	char* data;
	struct tju_ofo* next;
} tju_ofo_t;

// TCP 发送窗口
// 注释的内容如果想用就可以用 不想用就删掉 仅仅提供思路和灵感
typedef struct {
	uint16_t window_size;

//   uint32_t base;
//   uint32_t nextseq;
//   uint32_t estmated_rtt;
//   int ack_cnt;
//   pthread_mutex_t ack_cnt_lock;
//   struct timeval send_time;
//   struct timeval timeout;
//   uint16_t rwnd; 
//   int congestion_status;
//   uint16_t cwnd; 
//   uint16_t ssthresh; 
} sender_window_t;

// TCP 接受窗口
// 注释的内容如果想用就可以用 不想用就删掉 仅仅提供思路和灵感
typedef struct {
	char received[TCP_RECVWN_SIZE];

//   received_packet_t* head;
//   char buf[TCP_RECVWN_SIZE];
//   uint8_t marked[TCP_RECVWN_SIZE];
//   uint32_t expect_seq;
} receiver_window_t;

// TCP 窗口 每个建立了连接的TCP都包括发送和接受两个窗口
typedef struct {
	sender_window_t* wnd_send;
  	receiver_window_t* wnd_recv;
} window_t;

typedef struct {
	uint32_t ip;
	uint16_t port;
} tju_sock_addr;


// TJU_TCP 结构体 保存TJU_TCP用到的各种数据
typedef struct tju_tcp_t {
	int state; // TCP的状态

	tju_sock_addr bind_addr; // 存放bind和listen时该socket绑定的IP和端口
	tju_sock_addr established_local_addr; // 存放建立连接后 本机的 IP和端口
	tju_sock_addr established_remote_addr; // 存放建立连接后 连接对方的 IP和端口

	pthread_mutex_t send_lock; // 发送数据锁
	char* sending_buf; // 发送数据缓存区
	int sending_len; // 发送数据缓存长度
	int send_head; // 发送缓冲中 snd_una 对应的偏移，避免每次 ACK 都 memmove

	pthread_mutex_t recv_lock; // 接收数据锁
	char* received_buf; // 接收数据缓存区
	int received_len; // 接收数据缓存长度
	int recv_head; // 接收缓冲中可读数据的偏移

	pthread_cond_t wait_cond; // 可以被用来唤醒recv函数调用时等待的线程

	window_t window; // 发送和接受窗口

	/* 连接管理：序号、重传与 accept 队列（不改动上方既有状态宏） */
	pthread_mutex_t lock;
	uint32_t iss;
	uint32_t irs;
	uint32_t snd_nxt;
	uint32_t rcv_nxt;
	uint32_t fin_seq;
	int fin_sent;
	int syn_retransmitted;
	int closed_by_app;

	char* saved_pkt;
	uint16_t saved_len;
	uint32_t rto_us;
	int retry_cnt;
	int retrans_running;

	struct tju_tcp_t* parent;
	struct tju_tcp_t* accept_q[32];
	int accept_cnt;
	pthread_mutex_t accept_lock;

	/* 可靠传输与流量控制 */
	uint32_t snd_una;
	uint16_t peer_rwnd;
	uint16_t last_adv_wnd;
	int have_rtt;
	uint32_t srtt_us;
	uint32_t rttvar_us;
	int rtt_timing;
	uint32_t timed_seq;
	int dupacks;
	tju_seg_t segs[MAX_INFLIGHT];
	int nsegs;
	tju_ofo_t* ofo;
	int persist_backoff;
	pthread_cond_t timer_cond;
	int timer_alive;
	int recv_cap;
	int sender_running;

	/* 基础 Reno（说明书 §5.5 / RFC 5681） */
	uint32_t cwnd;
	uint32_t ssthresh;
	uint32_t cwnd_accum;
	int cc_state; /* SLOW_START / CONGESTION_AVOIDANCE / FAST_RECOVERY */
	int fast_rexmit_pending; /* 与 cc_state==FAST_RECOVERY 同步，便于日志 */
	uint32_t last_event_swnd;
	uint16_t last_event_rwnd;
	int disable_cc;
	int disable_fc;
	uint32_t fixed_wnd;

} tju_tcp_t;

#define TJU_MSL_SEC 1
#define INIT_RTO_US 1000000
#define MAX_CTRL_RETRY 8

#endif