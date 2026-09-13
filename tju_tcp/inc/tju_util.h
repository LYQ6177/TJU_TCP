#ifndef _TJU_UTIL_H_
#define _TJU_UTIL_H_

#include <stdint.h>

/* 序列号环形比较（模 2^32），用于判定 ACK/重传边界。 */
static inline int seq_before(uint32_t a, uint32_t b){
    return (int32_t)(a - b) < 0;
}

static inline int seq_after(uint32_t a, uint32_t b){
    return (int32_t)(a - b) > 0;
}

static inline uint32_t seq_min(uint32_t a, uint32_t b){
    return seq_before(a, b) ? a : b;
}

static inline uint32_t seq_max(uint32_t a, uint32_t b){
    return seq_before(a, b) ? b : a;
}

#endif
