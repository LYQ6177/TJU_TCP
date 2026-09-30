#define _GNU_SOURCE
#include "tju_tcp.h"
#include "tju_util.h"
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <sched.h>

#define SYN_F SYN_FLAG_MASK
#define ACK_F ACK_FLAG_MASK
#define FIN_F FIN_FLAG_MASK

static FILE* trace_fp = NULL;
static FILE* event_fp = NULL;
static pthread_mutex_t event_mu = PTHREAD_MUTEX_INITIALIZER;

static const char* state_name(int s){
    switch(s){
        case CLOSED: return "CLOSED";
        case LISTEN: return "LISTEN";
        case SYN_SENT: return "SYN_SENT";
        case SYN_RECV: return "SYN_RECV";
        case ESTABLISHED: return "ESTABLISHED";
        case FIN_WAIT_1: return "FIN_WAIT_1";
        case FIN_WAIT_2: return "FIN_WAIT_2";
        case CLOSE_WAIT: return "CLOSE_WAIT";
        case CLOSING: return "CLOSING";
        case LAST_ACK: return "LAST_ACK";
        case TIME_WAIT: return "TIME_WAIT";
        default: return "UNKNOWN";
    }
}

static void tcp_trace(const char* ev, tju_tcp_t* sock, uint32_t seq, uint32_t ack, uint8_t flags){
    (void)ev; (void)sock; (void)seq; (void)ack; (void)flags;
}

static long long utc_us(void){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + (long long)tv.tv_usec;
}

static void event_trace_open(void){
    if(event_fp != NULL) return;
    const char* name = tju_host_is_server() ? "server.event.trace" : "client.event.trace";
    char path[256];
    const char* dir = getenv("TJU_EVENT_DIR");
    if(dir && dir[0]){
        snprintf(path, sizeof(path), "%s/%s", dir, name);
        event_fp = fopen(path, "w");
    }
    if(event_fp == NULL){
        snprintf(path, sizeof(path), "/vagrant/tju_tcp/test/%s", name);
        event_fp = fopen(path, "w");
    }
    if(event_fp == NULL){
        snprintf(path, sizeof(path), "./test/%s", name);
        event_fp = fopen(path, "w");
    }
    if(event_fp == NULL) event_fp = fopen(name, "w");
    if(event_fp != NULL) setvbuf(event_fp, NULL, _IOLBF, 0);
}

static void event_write(const char* ev, const char* info){
    pthread_mutex_lock(&event_mu);
    event_trace_open();
    if(event_fp != NULL){
        fprintf(event_fp, "[%lld] [%s] [%s]\n", utc_us(), ev, info);
    }
    pthread_mutex_unlock(&event_mu);
}

static void event_send(uint32_t seq, uint32_t ack, uint8_t flags, uint32_t length){
    char info[128];
    snprintf(info, sizeof(info), "seq:%u ack:%u flag:%u length:%u", seq, ack, flags, length);
    event_write("SEND", info);
}

static void event_recv(uint32_t seq, uint32_t ack, uint8_t flags, uint32_t length){
    char info[128];
    snprintf(info, sizeof(info), "seq:%u ack:%u flag:%u length:%u", seq, ack, flags, length);
    event_write("RECV", info);
}

static void event_cwnd(int type, uint32_t size){
    char info[64];
    snprintf(info, sizeof(info), "type:%d size:%u", type, size);
    event_write("CWND", info);
}

static void event_rwnd_if_changed(tju_tcp_t* sock, uint16_t size){
    if(sock->last_event_rwnd == size && sock->last_event_rwnd != 0) return;
    sock->last_event_rwnd = size;
    char info[64];
    snprintf(info, sizeof(info), "size:%u", (unsigned)size);
    event_write("RWND", info);
}

static void event_swnd_if_changed(tju_tcp_t* sock, uint32_t size){
    if(sock->last_event_swnd == size) return;
    sock->last_event_swnd = size;
    char info[64];
    snprintf(info, sizeof(info), "size:%u", size);
    event_write("SWND", info);
}

static void event_rtts(uint32_t sample_us, uint32_t srtt_us, uint32_t rttvar_us, uint32_t rto_us){
    char info[192];
    snprintf(info, sizeof(info),
             "SampleRTT:%f EstimatedRTT:%f DeviationRTT:%f TimeoutInterval:%f",
             sample_us / 1000.0, srtt_us / 1000.0, rttvar_us / 1000.0, rto_us / 1000.0);
    event_write("RTTS", info);
}

static void event_delv(uint32_t seq, uint32_t size){
    char info[64];
    snprintf(info, sizeof(info), "seq:%u size:%u", seq, size);
    event_write("DELV", info);
}

static void rdt_log(tju_tcp_t* sock, const char* ev, uint32_t seq, uint32_t ack,
                    uint32_t len, uint16_t wnd){
    const char* path = getenv("TJU_TRACE");
    if(path == NULL) return;
    if(trace_fp == NULL){
        trace_fp = fopen(path, "a");
        if(trace_fp == NULL) return;
        setvbuf(trace_fp, NULL, _IOLBF, 0);
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    char host[16];
    memset(host, 0, sizeof(host));
    gethostname(host, 8);
    fprintf(trace_fp,
            "%ld.%06ld %s %s st=%s seq=%u ack=%u len=%u wnd=%u una=%u nxt=%u rcv=%u rto=%u dup=%d segs=%d slen=%d rlen=%d\n",
            (long)tv.tv_sec, (long)tv.tv_usec, host, ev, state_name(sock->state),
            seq, ack, len, wnd, sock->snd_una, sock->snd_nxt, sock->rcv_nxt,
            sock->rto_us, sock->dupacks, sock->nsegs, sock->sending_len, sock->received_len);
}

static int has_flag(uint8_t flags, uint8_t mask){
    return (flags & mask) != 0;
}

static void get_local_remote_ip(uint32_t* local_ip, uint32_t* remote_ip){
    if(tju_host_is_server()){
        *local_ip = inet_network(TJU_SERVER_IP);
        *remote_ip = inet_network(TJU_CLIENT_IP);
    }else{
        *local_ip = inet_network(TJU_CLIENT_IP);
        *remote_ip = inet_network(TJU_SERVER_IP);
    }
}

/* 线上 sender/receiver 要求：客户端 SYN seq=0/ack=0，服务端 SYN-ACK seq=1/ack=1 */
static uint32_t generate_isn(int as_server){
    return as_server ? 1u : 0u;
}

static void unregister_established(tju_tcp_t* sock){
    int hashval = cal_hash(sock->established_local_addr.ip,
                           sock->established_local_addr.port,
                           sock->established_remote_addr.ip,
                           sock->established_remote_addr.port);
    if(established_socks[hashval] == sock){
        established_socks[hashval] = NULL;
    }
}

static void register_established(tju_tcp_t* sock){
    int hashval = cal_hash(sock->established_local_addr.ip,
                           sock->established_local_addr.port,
                           sock->established_remote_addr.ip,
                           sock->established_remote_addr.port);
    established_socks[hashval] = sock;
    ensure_receive_thread();
}

static uint16_t inet_cksum(const uint8_t* buf, int n){
    uint32_t sum = 0;
    int i;
    for(i = 0; i + 1 < n; i += 2){
        sum += ((uint32_t)buf[i] << 8) | buf[i + 1];
    }
    if(n & 1) sum += (uint32_t)buf[n - 1] << 8;
    while(sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

static void stamp_cksum(char* pkt, uint16_t plen){
    if(pkt == NULL || plen < DEFAULT_HEADER_LEN) return;
    /* 控制报文必须保持 ext=0：线上 sender/receiver 按 ext=0 识别 SYN/SYN-ACK/ACK/FIN */
    if(plen == DEFAULT_HEADER_LEN){
        pkt[19] = 0;
        return;
    }
    pkt[19] = 0;
    uint16_t c = inet_cksum((const uint8_t*)pkt, plen);
    uint8_t e = (uint8_t)((c >> 8) ^ (c & 0xFF));
    if(e == 0) e = 0xFF;
    pkt[19] = (char)e;
}

static int verify_cksum(char* pkt, uint16_t plen){
    if(pkt == NULL || plen < DEFAULT_HEADER_LEN) return 0;
    uint8_t stored = (uint8_t)pkt[19];
    /* 验收端控制报文与握手报文 ext 恒为 0，不得丢弃 */
    if(stored == 0 || plen == DEFAULT_HEADER_LEN) return 1;
    pkt[19] = 0;
    uint16_t c = inet_cksum((const uint8_t*)pkt, plen);
    uint8_t e = (uint8_t)((c >> 8) ^ (c & 0xFF));
    if(e == 0) e = 0xFF;
    pkt[19] = (char)stored;
    return stored == e;
}

static uint32_t ofo_bytes(tju_tcp_t* sock){
    uint32_t n = 0;
    tju_ofo_t* p = sock->ofo;
    while(p){
        n += p->len;
        p = p->next;
    }
    return n;
}

static uint16_t calc_adv_wnd(tju_tcp_t* sock){
    int used = sock->received_len + (int)ofo_bytes(sock);
    int cap = sock->recv_cap > 0 ? sock->recv_cap : RECV_BUF_SIZE;
    int freeb = cap - used;
    if(freeb < 0) freeb = 0;
    if(sock->fixed_wnd > 0 && freeb > (int)sock->fixed_wnd) freeb = (int)sock->fixed_wnd;
    if(freeb > ADV_WND_MAX) freeb = ADV_WND_MAX;
    sock->last_adv_wnd = (uint16_t)freeb;
    event_rwnd_if_changed(sock, sock->last_adv_wnd);
    return sock->last_adv_wnd;
}

static void clear_saved_pkt(tju_tcp_t* sock){
    if(sock->saved_pkt){
        free(sock->saved_pkt);
        sock->saved_pkt = NULL;
        sock->saved_len = 0;
    }
}

static uint32_t flight_size(tju_tcp_t* sock){
    return sock->snd_nxt - sock->snd_una;
}

static uint32_t unsent_bytes(tju_tcp_t* sock){
    uint32_t fl = flight_size(sock);
    if((uint32_t)sock->sending_len > fl) return (uint32_t)sock->sending_len - fl;
    return 0;
}

static void compact_send(tju_tcp_t* sock){
    if(sock->send_head <= 0) return;
    if(sock->sending_len > 0){
        memmove(sock->sending_buf, sock->sending_buf + sock->send_head, (size_t)sock->sending_len);
    }
    sock->send_head = 0;
}

static void compact_recv(tju_tcp_t* sock){
    if(sock->recv_head <= 0) return;
    if(sock->received_len > 0){
        memmove(sock->received_buf, sock->received_buf + sock->recv_head, (size_t)sock->received_len);
    }
    sock->recv_head = 0;
}

/* 说明书 §4、§7.4：Advertised Window 为 16 位，通告不得超过 65535。
   通告已饱和时对端空闲空间至少为 65535，实际接收缓冲可按 §7.3 远大于此；
   本端飞行上限取 SEND_FLIGHT_MAX，既不把更大窗口截断塞进 16 位字段，
   也不把饱和通告误当成只有 64KB。通告 < 65535（含零窗口）时严格遵守。
   第三阶段：实际在途量同时受 rwnd 与 cwnd 约束，取 min。 */
static uint32_t peer_rwnd_cap(tju_tcp_t* sock){
    if(sock->peer_rwnd == 0) return 0;
    if(sock->peer_rwnd >= ADV_WND_MAX) return SEND_FLIGHT_MAX;
    return sock->peer_rwnd;
}

static uint32_t send_window(tju_tcp_t* sock){
    if(sock->fixed_wnd > 0) return sock->fixed_wnd;
    uint32_t cw = sock->cwnd ? sock->cwnd : (uint32_t)INIT_CWND;
    if(sock->disable_fc) return cw;
    uint32_t rw = peer_rwnd_cap(sock);
    return rw < cw ? rw : cw;
}

static void start_retransmit(tju_tcp_t* sock);
static void start_sender(tju_tcp_t* sock);

static void send_ctrl(tju_tcp_t* sock, uint32_t seq, uint32_t ack, uint8_t flags, int save){
    uint16_t adv = calc_adv_wnd(sock);
    char* pkt = create_packet_buf(sock->established_local_addr.port,
                                 sock->established_remote_addr.port,
                                 seq, ack, DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN,
                                 flags, adv, 0, NULL, 0);
    stamp_cksum(pkt, DEFAULT_HEADER_LEN);
    tcp_trace("SEND", sock, seq, ack, flags);
    event_send(seq, ack, flags, 0);
    rdt_log(sock, "SEND_CTRL", seq, ack, 0, adv);
    if(save){
        clear_saved_pkt(sock);
        sock->saved_pkt = (char*)malloc(DEFAULT_HEADER_LEN);
        memcpy(sock->saved_pkt, pkt, DEFAULT_HEADER_LEN);
        sock->saved_len = DEFAULT_HEADER_LEN;
        sock->retry_cnt = 0;
        if(sock->rto_us < RTO_MIN_US) sock->rto_us = INIT_RTO_US;
    }
    sendToLayer3(pkt, DEFAULT_HEADER_LEN);
    free(pkt);
    if(save) start_retransmit(sock);
}

static void enqueue_accept(tju_tcp_t* listen_sock, tju_tcp_t* conn){
    pthread_mutex_lock(&listen_sock->accept_lock);
    if(listen_sock->accept_cnt < 32){
        listen_sock->accept_q[listen_sock->accept_cnt++] = conn;
    }
    pthread_cond_broadcast(&listen_sock->wait_cond);
    pthread_mutex_unlock(&listen_sock->accept_lock);
}

static int fin_acked(tju_tcp_t* sock, uint32_t ack){
    return sock->fin_sent && (ack == sock->fin_seq + 1);
}

static void enter_time_wait(tju_tcp_t* sock){
    clear_saved_pkt(sock);
    sock->state = TIME_WAIT;
    tcp_trace("TIME_WAIT", sock, sock->snd_nxt, sock->rcv_nxt, 0);
    pthread_cond_broadcast(&sock->wait_cond);
    pthread_cond_broadcast(&sock->timer_cond);
}

static void handle_incoming_fin(tju_tcp_t* sock, uint32_t seq, uint32_t ack, uint8_t flags){
    /* 仅接受按序 FIN，避免超前 FIN 把 rcv_nxt 跳过空洞、接收端过早 CLOSE_WAIT */
    if(seq != sock->rcv_nxt){
        send_ctrl(sock, sock->snd_nxt, sock->rcv_nxt, ACK_F, 0);
        return;
    }
    sock->rcv_nxt = seq + 1;
    send_ctrl(sock, sock->snd_nxt, sock->rcv_nxt, ACK_F, 0);

    switch(sock->state){
        case ESTABLISHED:
        case SYN_RECV:
            sock->state = CLOSE_WAIT;
            tcp_trace("TO_CLOSE_WAIT", sock, seq, ack, flags);
            pthread_cond_broadcast(&sock->wait_cond);
            break;
        case FIN_WAIT_1:
            if(has_flag(flags, ACK_F) && fin_acked(sock, ack)){
                enter_time_wait(sock);
            }else{
                sock->state = CLOSING;
                tcp_trace("TO_CLOSING", sock, seq, ack, flags);
            }
            break;
        case FIN_WAIT_2:
            enter_time_wait(sock);
            break;
        case TIME_WAIT:
            break;
        default:
            break;
    }
}

static void ofo_insert(tju_tcp_t* sock, uint32_t seq, const char* data, uint16_t len){
    if(len == 0) return;
    uint32_t win = (uint32_t)sock->recv_cap - (uint32_t)sock->received_len;
    if(seq_geq(seq, sock->rcv_nxt + (win > 0 ? win : 1))) return;

    tju_ofo_t** pp = &sock->ofo;
    while(*pp){
        if((*pp)->seq == seq) return;
        if(seq_lt(seq, (*pp)->seq)) break;
        pp = &(*pp)->next;
    }
    tju_ofo_t* n = (tju_ofo_t*)malloc(sizeof(tju_ofo_t));
    n->seq = seq;
    n->len = len;
    n->data = (char*)malloc(len);
    memcpy(n->data, data, len);
    n->next = *pp;
    *pp = n;
}

static void ofo_drain(tju_tcp_t* sock){
    while(sock->ofo){
        tju_ofo_t* p = sock->ofo;
        if(seq_lt(p->seq, sock->rcv_nxt)){
            uint32_t skip = sock->rcv_nxt - p->seq;
            if(skip >= p->len){
                sock->ofo = p->next;
                free(p->data);
                free(p);
                continue;
            }
            memmove(p->data, p->data + skip, p->len - skip);
            p->len = (uint16_t)(p->len - skip);
            p->seq = sock->rcv_nxt;
        }
        if(p->seq != sock->rcv_nxt) break;
        if(sock->received_len + p->len > sock->recv_cap) break;
        if(sock->recv_head + sock->received_len + p->len > sock->recv_cap){
            compact_recv(sock);
        }
        memcpy(sock->received_buf + sock->recv_head + sock->received_len, p->data, p->len);
        sock->received_len += p->len;
        sock->rcv_nxt += p->len;
        event_delv(p->seq, p->len);
        sock->ofo = p->next;
        free(p->data);
        free(p);
        pthread_cond_broadcast(&sock->wait_cond);
    }
}

static void handle_data(tju_tcp_t* sock, char* pkt, uint32_t seq, uint32_t data_len){
    if(data_len == 0) return;
    char* payload = pkt + DEFAULT_HEADER_LEN;

    if(seq_lt(seq + data_len, sock->rcv_nxt) || seq + data_len == sock->rcv_nxt){
        return;
    }
    if(seq_lt(seq, sock->rcv_nxt)){
        uint32_t skip = sock->rcv_nxt - seq;
        payload += skip;
        data_len -= skip;
        seq = sock->rcv_nxt;
    }
    if(seq != sock->rcv_nxt){
        ofo_insert(sock, seq, payload, (uint16_t)data_len);
        return;
    }
    if(sock->received_len + (int)data_len > sock->recv_cap){
        data_len = (uint32_t)(sock->recv_cap - sock->received_len);
        if(data_len == 0) return;
    }
    if(sock->recv_head + sock->received_len + (int)data_len > sock->recv_cap){
        compact_recv(sock);
    }
    memcpy(sock->received_buf + sock->recv_head + sock->received_len, payload, data_len);
    sock->received_len += (int)data_len;
    sock->rcv_nxt += data_len;
    event_delv(seq, data_len);
    pthread_cond_broadcast(&sock->wait_cond);
    ofo_drain(sock);
}

static void update_rtt(tju_tcp_t* sock, uint32_t r_us){
    if(r_us == 0) r_us = 1;
    if(!sock->have_rtt){
        sock->srtt_us = r_us;
        sock->rttvar_us = r_us / 2;
        sock->have_rtt = 1;
    }else{
        uint32_t delta = (sock->srtt_us > r_us) ? (sock->srtt_us - r_us) : (r_us - sock->srtt_us);
        sock->rttvar_us = sock->rttvar_us - (sock->rttvar_us >> 2) + (delta >> 2);
        sock->srtt_us = sock->srtt_us - (sock->srtt_us >> 3) + (r_us >> 3);
    }
    uint32_t adj = 4 * sock->rttvar_us;
    if(adj < CLOCK_G_US) adj = CLOCK_G_US;
    uint32_t rto = sock->srtt_us + adj;
    if(rto < RTO_MIN_US) rto = RTO_MIN_US;
    if(rto > RTO_MAX_US) rto = RTO_MAX_US;
    sock->rto_us = rto;
    event_rtts(r_us, sock->srtt_us, sock->rttvar_us, sock->rto_us);
}

static void drop_acked_segs(tju_tcp_t* sock, uint32_t ack){
    int i, j = 0;
    struct timeval now;
    gettimeofday(&now, NULL);
    for(i = 0; i < sock->nsegs; i++){
        tju_seg_t* g = &sock->segs[i];
        if(seq_leq(g->seq + g->dlen, ack)){
            if(!g->retransmitted && sock->rtt_timing && g->seq == sock->timed_seq){
                uint32_t r = (uint32_t)((now.tv_sec - g->sent_at.tv_sec) * 1000000L
                                        + (now.tv_usec - g->sent_at.tv_usec));
                update_rtt(sock, r);
                sock->rtt_timing = 0;
                rdt_log(sock, "RTT", g->seq, ack, r, sock->peer_rwnd);
            }
            if(g->pkt) free(g->pkt);
        }else{
            if(j != i) sock->segs[j] = sock->segs[i];
            j++;
        }
    }
    sock->nsegs = j;
}

static void slide_send_buf(tju_tcp_t* sock, uint32_t ack){
    if(seq_leq(ack, sock->snd_una)) return;
    uint32_t d = ack - sock->snd_una;
    if(d > (uint32_t)sock->sending_len) d = (uint32_t)sock->sending_len;
    if(d > 0){
        sock->send_head += (int)d;
        sock->sending_len -= (int)d;
        if(sock->send_head > (SEND_BUF_SIZE / 2)){
            compact_send(sock);
        }
    }
    sock->snd_una = ack;
    pthread_cond_broadcast(&sock->wait_cond);
}

static void try_send(tju_tcp_t* sock);

/* RFC 5681 §3.2：ssthresh = max(FlightSize/2, 2*SMSS) */
static uint32_t reno_halve_ssthresh(tju_tcp_t* sock){
    uint32_t fs = flight_size(sock);
    uint32_t thr = fs / 2;
    if(thr < 2u * (uint32_t)SMSS) thr = 2u * (uint32_t)SMSS;
    return thr;
}

/* 完整 Reno 快速恢复：第 3 个 dup ACK 时膨胀 cwnd = ssthresh + 3*SMSS */
static void reno_enter_fast_recovery(tju_tcp_t* sock){
    if(sock->disable_cc) return;
    sock->ssthresh = reno_halve_ssthresh(sock);
    sock->cwnd_accum = 0;
    sock->cwnd = sock->ssthresh + 3u * (uint32_t)SMSS;
    sock->cc_state = FAST_RECOVERY;
    sock->fast_rexmit_pending = 1;
    event_cwnd(CWND_TRACE_FR, sock->cwnd);
    event_swnd_if_changed(sock, send_window(sock));
}

/* 恢复期间每再收到一个 dup ACK：cwnd += SMSS，随后 try_send 可发新数据 */
static void reno_fr_on_dupack(tju_tcp_t* sock){
    if(sock->disable_cc) return;
    if(sock->cc_state != FAST_RECOVERY) return;
    sock->cwnd += (uint32_t)SMSS;
    event_cwnd(CWND_TRACE_FR, sock->cwnd);
    event_swnd_if_changed(sock, send_window(sock));
}

/* 恢复 ACK（确认新数据）后收缩：cwnd = ssthresh，进入拥塞避免（Reno，非 NewReno） */
static void reno_exit_fast_recovery(tju_tcp_t* sock){
    sock->cwnd = sock->ssthresh;
    sock->cc_state = CONGESTION_AVOIDANCE;
    sock->cwnd_accum = 0;
    sock->fast_rexmit_pending = 0;
    event_cwnd(CWND_TRACE_CA, sock->cwnd);
    event_swnd_if_changed(sock, send_window(sock));
}

static void reno_cut_on_loss(tju_tcp_t* sock, int is_timeout){
    if(sock->disable_cc) return;
    sock->ssthresh = reno_halve_ssthresh(sock);
    sock->cwnd_accum = 0;
    sock->dupacks = 0;
    sock->cwnd = (uint32_t)SMSS;
    sock->cc_state = SLOW_START;
    sock->fast_rexmit_pending = 0;
    event_cwnd(CWND_TRACE_TO, sock->cwnd);
    event_swnd_if_changed(sock, send_window(sock));
    (void)is_timeout;
}

static void reno_on_newack(tju_tcp_t* sock, uint32_t newly){
    if(sock->disable_cc) return;
    if(newly == 0) return;
    if(sock->state != ESTABLISHED && sock->state != CLOSE_WAIT) return;

    if(sock->cc_state == FAST_RECOVERY){
        reno_exit_fast_recovery(sock);
        return;
    }

    uint32_t old = sock->cwnd;
    if(sock->cc_state == SLOW_START || sock->cwnd < sock->ssthresh){
        uint32_t inc = newly;
        if(inc > (uint32_t)SMSS) inc = (uint32_t)SMSS;
        sock->cwnd += inc;
        if(sock->cwnd >= sock->ssthresh){
            sock->cc_state = CONGESTION_AVOIDANCE;
            sock->cwnd_accum = 0;
        }else{
            sock->cc_state = SLOW_START;
        }
        if(sock->cwnd != old){
            event_cwnd(sock->cc_state == CONGESTION_AVOIDANCE ? CWND_TRACE_CA : CWND_TRACE_SS,
                       sock->cwnd);
        }
    }else{
        sock->cc_state = CONGESTION_AVOIDANCE;
        sock->cwnd_accum += newly;
        while(sock->cwnd > 0 && sock->cwnd_accum >= sock->cwnd){
            sock->cwnd_accum -= sock->cwnd;
            sock->cwnd += (uint32_t)SMSS;
        }
        if(sock->cwnd != old){
            event_cwnd(CWND_TRACE_CA, sock->cwnd);
        }
    }
    event_swnd_if_changed(sock, send_window(sock));
}

static void retransmit_oldest(tju_tcp_t* sock){
    if(sock->nsegs <= 0) return;
    tju_seg_t* g = &sock->segs[0];
    g->retransmitted = 1;
    if(sock->rtt_timing && sock->timed_seq == g->seq) sock->rtt_timing = 0;
    gettimeofday(&g->sent_at, NULL);
    sendToLayer3(g->pkt, g->plen);
    event_send(g->seq, sock->rcv_nxt, ACK_F, g->dlen);
    rdt_log(sock, "RETRANS_DATA", g->seq, sock->rcv_nxt, g->dlen, sock->peer_rwnd);
}

static void retransmit_all(tju_tcp_t* sock){
    int i;
    sock->rtt_timing = 0;
    for(i = 0; i < sock->nsegs; i++){
        tju_seg_t* g = &sock->segs[i];
        g->retransmitted = 1;
        gettimeofday(&g->sent_at, NULL);
        sendToLayer3(g->pkt, g->plen);
    }
}

static void restart_rto_after_ack(tju_tcp_t* sock){
    sock->retry_cnt = 0;
    if(sock->have_rtt){
        uint32_t adj = 4 * sock->rttvar_us;
        if(adj < CLOCK_G_US) adj = CLOCK_G_US;
        uint32_t rto = sock->srtt_us + adj;
        if(rto < RTO_MIN_US) rto = RTO_MIN_US;
        if(rto > RTO_MAX_US) rto = RTO_MAX_US;
        sock->rto_us = rto;
    }else{
        sock->rto_us = INIT_RTO_US;
    }
}

static void handle_ack(tju_tcp_t* sock, uint32_t ack, uint32_t data_len, uint8_t flags){
    if(!has_flag(flags, ACK_F)) return;
    if(seq_gt(ack, sock->snd_nxt)) return;

    if(seq_lt(ack, sock->snd_una)) return;

    if(ack == sock->snd_una){
        if(data_len == 0 && !has_flag(flags, FIN_F) && sock->nsegs > 0){
            sock->dupacks++;
            rdt_log(sock, "DUPACK", sock->snd_una, ack, 0, sock->peer_rwnd);
            if(!sock->disable_cc && sock->cc_state == FAST_RECOVERY){
                reno_fr_on_dupack(sock);
            }else if(sock->dupacks == DUPACK_THRESH){
                if(!sock->disable_cc) reno_enter_fast_recovery(sock);
                retransmit_oldest(sock);
                rdt_log(sock, "FAST_REXMIT", sock->snd_una, ack, 0, sock->peer_rwnd);
            }
        }
        /* 通告窗口可能已打开；FR 膨胀后也要尝试发送新数据 */
        try_send(sock);
        return;
    }

    uint32_t newly = ack - sock->snd_una;
    sock->dupacks = 0;
    drop_acked_segs(sock, ack);
    slide_send_buf(sock, ack);
    sock->persist_backoff = 0;
    restart_rto_after_ack(sock);
    reno_on_newack(sock, newly);
    rdt_log(sock, "ACK_ADV", sock->snd_una, ack, 0, sock->peer_rwnd);
    pthread_cond_broadcast(&sock->timer_cond);
    try_send(sock);
}

static int send_data_seg(tju_tcp_t* sock, uint32_t seq, const char* data, uint16_t dlen, int is_rexmit){
    uint16_t plen = DEFAULT_HEADER_LEN + dlen;
    uint16_t adv = calc_adv_wnd(sock);
    if(!is_rexmit && sock->nsegs >= MAX_INFLIGHT) return 0;
    char* pkt = create_packet_buf(sock->established_local_addr.port,
                                  sock->established_remote_addr.port,
                                  seq, sock->rcv_nxt, DEFAULT_HEADER_LEN, plen,
                                  ACK_F, adv, 0, (char*)data, dlen);
    stamp_cksum(pkt, plen);

    if(!is_rexmit){
        tju_seg_t* g = &sock->segs[sock->nsegs++];
        g->seq = seq;
        g->dlen = dlen;
        g->plen = plen;
        g->pkt = (char*)malloc(plen);
        memcpy(g->pkt, pkt, plen);
        gettimeofday(&g->sent_at, NULL);
        g->retransmitted = 0;
        if(!sock->rtt_timing){
            sock->rtt_timing = 1;
            sock->timed_seq = seq;
        }
        sock->snd_nxt += dlen;
        rdt_log(sock, "SEND_DATA", seq, sock->rcv_nxt, dlen, adv);
        event_send(seq, sock->rcv_nxt, ACK_F, dlen);
        event_swnd_if_changed(sock, send_window(sock));
    }
    sendToLayer3(pkt, plen);
    free(pkt);
    start_retransmit(sock);
    return 1;
}

static void try_send(tju_tcp_t* sock){
    if(sock->state != ESTABLISHED && sock->state != CLOSE_WAIT) return;
    int burst = 0;
    while(sock->nsegs < MAX_INFLIGHT && burst < SEND_BURST_MAX){
        uint32_t fl = flight_size(sock);
        uint32_t unsent = unsent_bytes(sock);
        if(unsent == 0) break;
        uint32_t sw = send_window(sock);
        uint32_t usable = (sw > fl) ? (sw - fl) : 0;
        if(usable == 0) break;
        /* 说明书 §5.4.6：避免 Silly Window，窗口过小且仍有后续数据时等待；关闭阶段仍要把尾段发完 */
        if(usable < (uint32_t)SMSS && unsent > usable && !sock->closed_by_app) break;
        uint32_t dlen = unsent;
        if(dlen > SMSS) dlen = SMSS;
        if(dlen > usable) dlen = usable;
        const char* data = sock->sending_buf + sock->send_head + (sock->snd_nxt - sock->snd_una);
        if(!send_data_seg(sock, sock->snd_nxt, data, (uint16_t)dlen, 0)) break;
        burst++;
    }
}

static void send_window_update(tju_tcp_t* sock){
    if(sock->state == ESTABLISHED || sock->state == CLOSE_WAIT || sock->state == FIN_WAIT_1
       || sock->state == FIN_WAIT_2 || sock->state == TIME_WAIT){
        send_ctrl(sock, sock->snd_nxt, sock->rcv_nxt, ACK_F, 0);
        rdt_log(sock, "WND_UPD", sock->snd_nxt, sock->rcv_nxt, 0, calc_adv_wnd(sock));
    }
}

static int need_timer(tju_tcp_t* sock){
    int st = sock->state;
    if(st == CLOSED || st == TIME_WAIT || st == LISTEN) return 0;
    if(st == SYN_SENT || st == SYN_RECV) return sock->saved_pkt != NULL;
    if(st == FIN_WAIT_1 || st == LAST_ACK || st == CLOSING) return sock->saved_pkt != NULL;
    if(sock->nsegs > 0) return 1;
    if(unsent_bytes(sock) > 0 && sock->peer_rwnd == 0) return 1;
    return 0;
}

static void do_retransmit(tju_tcp_t* sock){
    int st = sock->state;
    sock->retry_cnt++;
    if(sock->rto_us < RTO_MAX_US / 2) sock->rto_us *= 2;
    else sock->rto_us = RTO_MAX_US;

    if((st == SYN_SENT || st == SYN_RECV) && sock->saved_pkt){
        sock->syn_retransmitted = 1;
        sendToLayer3(sock->saved_pkt, sock->saved_len);
        tcp_trace("RETRANS", sock, get_seq(sock->saved_pkt), get_ack(sock->saved_pkt),
                  get_flags(sock->saved_pkt));
        event_send(get_seq(sock->saved_pkt), get_ack(sock->saved_pkt),
                   get_flags(sock->saved_pkt), 0);
        rdt_log(sock, "RETRANS_SYN", get_seq(sock->saved_pkt), get_ack(sock->saved_pkt), 0, 0);
        return;
    }
    if((st == FIN_WAIT_1 || st == LAST_ACK || st == CLOSING) && sock->saved_pkt){
        sendToLayer3(sock->saved_pkt, sock->saved_len);
        event_send(get_seq(sock->saved_pkt), get_ack(sock->saved_pkt),
                   get_flags(sock->saved_pkt), 0);
        rdt_log(sock, "RETRANS_FIN", get_seq(sock->saved_pkt), get_ack(sock->saved_pkt), 0, 0);
        return;
    }
    if(sock->nsegs > 0){
        reno_cut_on_loss(sock, 1);
        retransmit_oldest(sock);
        rdt_log(sock, "RTO_REXMIT", sock->snd_una, sock->rcv_nxt, 0, sock->peer_rwnd);
        return;
    }
    if(unsent_bytes(sock) > 0 && sock->peer_rwnd == 0){
        const char* data = sock->sending_buf + sock->send_head + (sock->snd_nxt - sock->snd_una);
        send_data_seg(sock, sock->snd_nxt, data, 1, 0);
        sock->persist_backoff++;
        if(sock->persist_backoff > 8) sock->persist_backoff = 8; /* 封顶，避免探测间隔无界增长 */
        rdt_log(sock, "ZERO_PROBE", sock->snd_nxt - 1, sock->rcv_nxt, 1, 0);
    }
}

static void* retrans_loop(void* arg){
    tju_tcp_t* sock = (tju_tcp_t*)arg;
    pthread_mutex_lock(&sock->lock);
    while(sock->timer_alive && sock->state != CLOSED){
        if(!need_timer(sock)){
            sock->retrans_running = 0;
            pthread_cond_wait(&sock->timer_cond, &sock->lock);
            continue;
        }
        sock->retrans_running = 1;
        uint32_t rto = sock->rto_us ? sock->rto_us : INIT_RTO_US;
        if(sock->peer_rwnd == 0 && sock->nsegs == 0 && unsent_bytes(sock) > 0){
            uint32_t pr = INIT_RTO_US << (sock->persist_backoff > 6 ? 6 : sock->persist_backoff);
            if(pr > RTO_MAX_US) pr = RTO_MAX_US;
            rto = pr;
        }
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += rto / 1000000;
        ts.tv_nsec += (long)(rto % 1000000) * 1000L;
        if(ts.tv_nsec >= 1000000000L){
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        int rc = pthread_cond_timedwait(&sock->timer_cond, &sock->lock, &ts);
        if(rc == ETIMEDOUT && need_timer(sock)){
            if((sock->state == SYN_SENT || sock->state == SYN_RECV
                || sock->state == FIN_WAIT_1 || sock->state == LAST_ACK || sock->state == CLOSING)
               && sock->retry_cnt > MAX_CTRL_RETRY && sock->saved_pkt){
                sock->state = CLOSED;
                clear_saved_pkt(sock);
                pthread_cond_broadcast(&sock->wait_cond);
                break;
            }
            do_retransmit(sock);
        }
    }
    sock->retrans_running = 0;
    pthread_mutex_unlock(&sock->lock);
    return NULL;
}

static void start_retransmit(tju_tcp_t* sock){
    pthread_cond_broadcast(&sock->timer_cond);
    if(sock->retrans_running) return;
    sock->retrans_running = 1;
    pthread_t tid;
    if(pthread_create(&tid, NULL, retrans_loop, sock) == 0){
        pthread_detach(tid);
    }else{
        sock->retrans_running = 0;
    }
}

/* 每连接一条发送线程（说明书禁止的是每报文一线程），把通告窗口持续填满 */
static void* send_loop(void* arg){
    tju_tcp_t* sock = (tju_tcp_t*)arg;
    pthread_mutex_lock(&sock->lock);
    while(sock->timer_alive && sock->state != CLOSED && sock->state != TIME_WAIT){
        int can_more = 0;
        if(sock->state == ESTABLISHED || sock->state == CLOSE_WAIT){
            try_send(sock);
            can_more = (unsent_bytes(sock) > 0 && send_window(sock) > flight_size(sock)
                        && sock->nsegs < MAX_INFLIGHT);
        }
        if(can_more){
            /* 让出锁，让收包线程处理 ACK，避免 UDP 接收队列被突发发送堵死 */
            pthread_mutex_unlock(&sock->lock);
            sched_yield();
            pthread_mutex_lock(&sock->lock);
            continue;
        }
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 200000L;
        if(ts.tv_nsec >= 1000000000L){
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&sock->wait_cond, &sock->lock, &ts);
    }
    sock->sender_running = 0;
    pthread_mutex_unlock(&sock->lock);
    return NULL;
}

static void start_sender(tju_tcp_t* sock){
    pthread_cond_broadcast(&sock->wait_cond);
    if(sock->sender_running) return;
    sock->sender_running = 1;
    pthread_t tid;
    if(pthread_create(&tid, NULL, send_loop, sock) == 0){
        pthread_detach(tid);
    }else{
        sock->sender_running = 0;
    }
}

static void on_established(tju_tcp_t* sock){
    sock->snd_una = sock->snd_nxt;
    if(sock->syn_retransmitted){
        sock->rto_us = 3 * INIT_RTO_US;
        sock->have_rtt = 0;
        tcp_trace("RTO_RESET3S", sock, sock->snd_nxt, sock->rcv_nxt, 0);
    }
    event_cwnd(CWND_TRACE_SS, sock->cwnd);
    event_swnd_if_changed(sock, send_window(sock));
    if(getenv("TJU_CC_DEBUG")){
        fprintf(stderr, "[CC_DEBUG] established cwnd=%u ssthresh=%u rto=%u disable_cc=%d\n",
                sock->cwnd, sock->ssthresh, sock->rto_us, sock->disable_cc);
        fflush(stderr);
    }
    start_retransmit(sock);
    start_sender(sock);
}

/*
创建 TCP socket
初始化对应的结构体
设置初始状态为 CLOSED
*/
tju_tcp_t* tju_socket(){
    tju_tcp_t* sock = (tju_tcp_t*)malloc(sizeof(tju_tcp_t));
    memset(sock, 0, sizeof(tju_tcp_t));
    sock->state = CLOSED;

    pthread_mutex_init(&(sock->send_lock), NULL);
    sock->sending_buf = (char*)malloc(SEND_BUF_SIZE);
    sock->sending_len = 0;
    sock->send_head = 0;

    pthread_mutex_init(&(sock->recv_lock), NULL);
    sock->recv_cap = RECV_BUF_SIZE;
    {
        const char* e = getenv("TJU_RECV_CAP");
        if(e){
            int v = atoi(e);
            if(v >= SMSS && v <= RECV_BUF_SIZE) sock->recv_cap = v;
        }
    }
    sock->received_buf = (char*)malloc(sock->recv_cap);
    sock->received_len = 0;
    sock->recv_head = 0;

    if(pthread_cond_init(&sock->wait_cond, NULL) != 0){
        perror("ERROR condition variable not set\n");
        exit(-1);
    }
    pthread_cond_init(&sock->timer_cond, NULL);

    sock->window.wnd_send = NULL;
    sock->window.wnd_recv = NULL;

    pthread_mutex_init(&(sock->lock), NULL);
    pthread_mutex_init(&(sock->accept_lock), NULL);
    sock->rto_us = INIT_RTO_US;
    sock->peer_rwnd = ADV_WND_MAX;
    sock->timer_alive = 1;
    sock->cwnd = (uint32_t)INIT_CWND;
    sock->ssthresh = (uint32_t)INIT_SSTHRESH;
    sock->cwnd_accum = 0;
    sock->cc_state = SLOW_START;
    sock->fast_rexmit_pending = 0;
    sock->last_event_swnd = 0;
    sock->last_event_rwnd = 0;
    sock->disable_cc = getenv("TJU_DISABLE_CC") ? 1 : 0;
    sock->disable_fc = getenv("TJU_DISABLE_FC") ? 1 : 0;
    sock->fixed_wnd = 0;
    {
        /* 可选：用环境变量覆盖初始 RTO（微秒），便于性能对照实验 */
        const char* ir = getenv("TJU_INIT_RTO_US");
        if(ir){
            int v = atoi(ir);
            if(v >= (int)RTO_MIN_US && v <= (int)RTO_MAX_US) sock->rto_us = (uint32_t)v;
        }
    }
    {
        const char* w = getenv("TJU_FIXED_WND");
        if(w){
            int mss = atoi(w);
            if(mss > 0){
                sock->fixed_wnd = (uint32_t)mss * (uint32_t)SMSS;
                sock->cwnd = sock->fixed_wnd;
                sock->ssthresh = sock->fixed_wnd;
                sock->disable_cc = 1;
                sock->disable_fc = 1;
            }
        }
    }

    return sock;
}

int tju_bind(tju_tcp_t* sock, tju_sock_addr bind_addr){
    sock->bind_addr = bind_addr;
    return 0;
}

int tju_listen(tju_tcp_t* sock){
    uint32_t local_ip, remote_ip;
    get_local_remote_ip(&local_ip, &remote_ip);
    sock->bind_addr.ip = local_ip;

    pthread_mutex_lock(&sock->lock);
    sock->state = LISTEN;
    sock->accept_cnt = 0;
    pthread_mutex_unlock(&sock->lock);
    int hashval = cal_hash(sock->bind_addr.ip, sock->bind_addr.port, 0, 0);
    listen_socks[hashval] = sock;
    ensure_receive_thread();
    tcp_trace("LISTEN", sock, 0, 0, 0);
    return 0;
}

tju_tcp_t* tju_accept(tju_tcp_t* listen_sock){
    pthread_mutex_lock(&listen_sock->accept_lock);
    while(listen_sock->accept_cnt <= 0){
        pthread_cond_wait(&listen_sock->wait_cond, &listen_sock->accept_lock);
    }
    tju_tcp_t* conn = listen_sock->accept_q[0];
    int i;
    for(i = 1; i < listen_sock->accept_cnt; i++){
        listen_sock->accept_q[i - 1] = listen_sock->accept_q[i];
    }
    listen_sock->accept_cnt--;
    pthread_mutex_unlock(&listen_sock->accept_lock);
    tcp_trace("ACCEPT", conn, conn->snd_nxt, conn->rcv_nxt, 0);
    return conn;
}

int tju_connect(tju_tcp_t* sock, tju_sock_addr target_addr){
    uint32_t local_ip, remote_ip;
    get_local_remote_ip(&local_ip, &remote_ip);

    sock->established_remote_addr = target_addr;
    sock->established_local_addr.ip = local_ip;
    sock->established_local_addr.port = 5678;

    sock->iss = generate_isn(0);
    sock->snd_nxt = sock->iss;
    sock->snd_una = sock->iss;

    register_established(sock);

    pthread_mutex_lock(&sock->lock);
    sock->state = SYN_SENT;
    send_ctrl(sock, sock->iss, 0, SYN_F, 1);
    sock->snd_nxt = sock->iss + 1;

    while(sock->state != ESTABLISHED && sock->state != CLOSED){
        pthread_cond_wait(&sock->wait_cond, &sock->lock);
    }
    int ret = (sock->state == ESTABLISHED) ? 0 : -1;
    pthread_mutex_unlock(&sock->lock);
    return ret;
}

int tju_send(tju_tcp_t* sock, const void *buffer, int len){
    if(buffer == NULL || len <= 0) return -1;
    const char* src = (const char*)buffer;
    int sent = 0;

    pthread_mutex_lock(&sock->lock);
    if(sock->closed_by_app || (sock->state != ESTABLISHED && sock->state != CLOSE_WAIT)){
        pthread_mutex_unlock(&sock->lock);
        return -1;
    }
    while(sent < len){
        while(sock->sending_len >= SEND_BUF_SIZE
              && sock->state != CLOSED
              && !sock->closed_by_app){
            pthread_cond_wait(&sock->wait_cond, &sock->lock);
        }
        if(sock->closed_by_app && sent == 0 && sock->state != ESTABLISHED && sock->state != CLOSE_WAIT){
            pthread_mutex_unlock(&sock->lock);
            return -1;
        }
        if(sock->send_head + sock->sending_len >= SEND_BUF_SIZE){
            compact_send(sock);
        }
        int space = SEND_BUF_SIZE - sock->send_head - sock->sending_len;
        if(space <= 0){
            compact_send(sock);
            space = SEND_BUF_SIZE - sock->sending_len;
        }
        if(space <= 0) continue;
        int n = len - sent;
        if(n > space) n = space;
        memcpy(sock->sending_buf + sock->send_head + sock->sending_len, src + sent, n);
        sock->sending_len += n;
        sent += n;
        try_send(sock);
        pthread_cond_broadcast(&sock->wait_cond);
    }
    pthread_mutex_unlock(&sock->lock);
    return 0;
}

int tju_recv(tju_tcp_t* sock, void *buffer, int len){
    if(buffer == NULL || len <= 0) return 0;
    pthread_mutex_lock(&sock->lock);
    while(sock->received_len <= 0){
        if(sock->state == CLOSED || sock->state == TIME_WAIT
           || sock->state == LAST_ACK || sock->state == CLOSING
           || sock->state == CLOSE_WAIT){
            pthread_mutex_unlock(&sock->lock);
            return 0;
        }
        pthread_cond_wait(&sock->wait_cond, &sock->lock);
    }
    uint16_t old_adv = sock->last_adv_wnd;
    int read_len = sock->received_len < len ? sock->received_len : len;
    memcpy(buffer, sock->received_buf + sock->recv_head, read_len);
    sock->recv_head += read_len;
    sock->received_len -= read_len;
    if(sock->recv_head > (sock->recv_cap / 2)){
        compact_recv(sock);
    }
    uint16_t new_adv = calc_adv_wnd(sock);
    if(old_adv == 0 && new_adv > 0){
        send_window_update(sock);
    }else if(new_adv >= SMSS && old_adv < SMSS){
        send_window_update(sock);
    }
    pthread_mutex_unlock(&sock->lock);
    return read_len;
}

int tju_handle_packet(tju_tcp_t* sock, char* pkt){
    uint32_t seq = get_seq(pkt);
    uint32_t ack = get_ack(pkt);
    uint8_t flags = get_flags(pkt);
    uint16_t plen = get_plen(pkt);
    uint32_t data_len = (plen >= DEFAULT_HEADER_LEN) ? (plen - DEFAULT_HEADER_LEN) : 0;
    uint16_t adv = get_advertised_window(pkt);

    if(!verify_cksum(pkt, plen)){
        rdt_log(sock, "CKSUM_DROP", seq, ack, data_len, adv);
        return 0;
    }

    tcp_trace("RECV", sock, seq, ack, flags);
    event_recv(seq, ack, flags, data_len);

    pthread_mutex_lock(&sock->lock);

    if(!(has_flag(flags, SYN_F) && !has_flag(flags, ACK_F))){
        sock->peer_rwnd = adv;
        event_swnd_if_changed(sock, send_window(sock));
    }

    if(sock->state == LISTEN && has_flag(flags, SYN_F) && !has_flag(flags, ACK_F)){
        tju_tcp_t* conn = tju_socket();
        uint32_t local_ip, remote_ip;
        get_local_remote_ip(&local_ip, &remote_ip);

        conn->parent = sock;
        conn->bind_addr = sock->bind_addr;
        conn->established_local_addr.ip = local_ip;
        conn->established_local_addr.port = sock->bind_addr.port;
        conn->established_remote_addr.ip = remote_ip;
        conn->established_remote_addr.port = get_src(pkt);

        conn->irs = seq;
        conn->rcv_nxt = seq + 1;
        conn->iss = generate_isn(1);
        conn->snd_nxt = conn->iss;
        conn->snd_una = conn->iss;
        conn->peer_rwnd = adv ? adv : ADV_WND_MAX;

        register_established(conn);

        pthread_mutex_lock(&conn->lock);
        conn->state = SYN_RECV;
        send_ctrl(conn, conn->iss, conn->rcv_nxt, SYN_F | ACK_F, 1);
        conn->snd_nxt = conn->iss + 1;
        pthread_mutex_unlock(&conn->lock);

        pthread_mutex_unlock(&sock->lock);
        return 0;
    }

    if(sock->state == SYN_SENT){
        if(has_flag(flags, SYN_F) && has_flag(flags, ACK_F) && ack == sock->iss + 1){
            sock->irs = seq;
            sock->rcv_nxt = seq + 1;
            sock->peer_rwnd = adv ? adv : ADV_WND_MAX;
            clear_saved_pkt(sock);
            sock->snd_una = sock->iss + 1;
            send_ctrl(sock, sock->snd_nxt, sock->rcv_nxt, ACK_F, 0);
            sock->state = ESTABLISHED;
            on_established(sock);
            tcp_trace("ESTAB", sock, sock->snd_nxt, sock->rcv_nxt, ACK_F);
            pthread_cond_broadcast(&sock->wait_cond);
        }
        pthread_mutex_unlock(&sock->lock);
        return 0;
    }

    if(sock->state == SYN_RECV){
        if(has_flag(flags, SYN_F) && !has_flag(flags, ACK_F)){
            send_ctrl(sock, sock->iss, sock->rcv_nxt, SYN_F | ACK_F, 1);
            pthread_mutex_unlock(&sock->lock);
            return 0;
        }
        if(has_flag(flags, ACK_F) && ack == sock->iss + 1){
            clear_saved_pkt(sock);
            sock->snd_una = sock->iss + 1;
            sock->state = ESTABLISHED;
            on_established(sock);
            tcp_trace("ESTAB", sock, sock->snd_nxt, sock->rcv_nxt, flags);
            if(sock->parent){
                enqueue_accept(sock->parent, sock);
            }
            pthread_cond_broadcast(&sock->wait_cond);
            if(data_len > 0 && !has_flag(flags, FIN_F)){
                handle_data(sock, pkt, seq, data_len);
                send_ctrl(sock, sock->snd_nxt, sock->rcv_nxt, ACK_F, 0);
            }
            if(has_flag(flags, FIN_F)){
                uint32_t finseq = seq + data_len;
                handle_incoming_fin(sock, finseq, ack, flags);
            }
            pthread_mutex_unlock(&sock->lock);
            return 0;
        }
        pthread_mutex_unlock(&sock->lock);
        return 0;
    }

    if(sock->state == ESTABLISHED && has_flag(flags, SYN_F) && has_flag(flags, ACK_F)){
        send_ctrl(sock, sock->snd_nxt, sock->rcv_nxt, ACK_F, 0);
        pthread_mutex_unlock(&sock->lock);
        return 0;
    }
    if((sock->state == ESTABLISHED || sock->state == SYN_RECV)
       && has_flag(flags, SYN_F) && !has_flag(flags, ACK_F)){
        send_ctrl(sock, sock->iss, sock->rcv_nxt, SYN_F | ACK_F, 1);
        pthread_mutex_unlock(&sock->lock);
        return 0;
    }

    if(has_flag(flags, ACK_F)){
        handle_ack(sock, ack, data_len, flags);
        if(sock->state == FIN_WAIT_1 && fin_acked(sock, ack)){
            clear_saved_pkt(sock);
            sock->state = FIN_WAIT_2;
            tcp_trace("TO_FIN_WAIT2", sock, seq, ack, flags);
        }else if(sock->state == CLOSING && fin_acked(sock, ack)){
            enter_time_wait(sock);
        }else if(sock->state == LAST_ACK && fin_acked(sock, ack)){
            clear_saved_pkt(sock);
            sock->state = CLOSED;
            sock->timer_alive = 0;
            tcp_trace("CLOSED", sock, seq, ack, flags);
            pthread_cond_broadcast(&sock->wait_cond);
            pthread_cond_broadcast(&sock->timer_cond);
        }
    }

    int got_data = 0;
    if(data_len > 0 && !has_flag(flags, SYN_F)){
        uint32_t old_nxt = sock->rcv_nxt;
        handle_data(sock, pkt, seq, data_len);
        got_data = 1;
        if(sock->rcv_nxt == old_nxt){
            rdt_log(sock, "OFO_OR_DUP", seq, sock->rcv_nxt, data_len, calc_adv_wnd(sock));
        }
    }

    if(has_flag(flags, FIN_F)){
        uint32_t finseq = seq + data_len;
        handle_incoming_fin(sock, finseq, ack, flags);
        pthread_mutex_unlock(&sock->lock);
        return 0;
    }

    if(got_data){
        send_ctrl(sock, sock->snd_nxt, sock->rcv_nxt, ACK_F, 0);
    }

    pthread_mutex_unlock(&sock->lock);
    return 0;
}

int tju_close (tju_tcp_t* sock){
    pthread_mutex_lock(&sock->lock);
    sock->closed_by_app = 1;

    if(sock->state == CLOSED){
        pthread_mutex_unlock(&sock->lock);
        return 0;
    }

    if(sock->state == LISTEN){
        sock->state = CLOSED;
        sock->timer_alive = 0;
        int hashval = cal_hash(sock->bind_addr.ip, sock->bind_addr.port, 0, 0);
        if(listen_socks[hashval] == sock){
            listen_socks[hashval] = NULL;
        }
        pthread_cond_broadcast(&sock->timer_cond);
        pthread_mutex_unlock(&sock->lock);
        return 0;
    }

    while(sock->sending_len > 0 && sock->state != CLOSED){
        try_send(sock);
        pthread_cond_wait(&sock->wait_cond, &sock->lock);
    }

    if(sock->state == ESTABLISHED || sock->state == SYN_RECV || sock->state == SYN_SENT){
        sock->fin_seq = sock->snd_nxt;
        sock->fin_sent = 1;
        send_ctrl(sock, sock->fin_seq, sock->rcv_nxt, FIN_F | ACK_F, 1);
        sock->snd_nxt = sock->fin_seq + 1;
        sock->state = FIN_WAIT_1;
        tcp_trace("SEND_FIN", sock, sock->fin_seq, sock->rcv_nxt, FIN_F | ACK_F);
    }else if(sock->state == CLOSE_WAIT){
        sock->fin_seq = sock->snd_nxt;
        sock->fin_sent = 1;
        send_ctrl(sock, sock->fin_seq, sock->rcv_nxt, FIN_F | ACK_F, 1);
        sock->snd_nxt = sock->fin_seq + 1;
        sock->state = LAST_ACK;
        tcp_trace("SEND_FIN", sock, sock->fin_seq, sock->rcv_nxt, FIN_F | ACK_F);
    }

    while(sock->state != TIME_WAIT && sock->state != CLOSED){
        pthread_cond_wait(&sock->wait_cond, &sock->lock);
    }

    int need_msl = (sock->state == TIME_WAIT);
    pthread_mutex_unlock(&sock->lock);

    if(need_msl){
        usleep(2 * TJU_MSL_SEC * 1000000);
        pthread_mutex_lock(&sock->lock);
        if(sock->state == TIME_WAIT){
            sock->state = CLOSED;
            sock->timer_alive = 0;
            tcp_trace("TW_EXPIRED", sock, sock->snd_nxt, sock->rcv_nxt, 0);
            pthread_cond_broadcast(&sock->timer_cond);
        }
        pthread_mutex_unlock(&sock->lock);
    }

    unregister_established(sock);
    return 0;
}
