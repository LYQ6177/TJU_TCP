#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* 轻量自检：RFC 1071 风格 16 位一补数折合。 */
static uint16_t fold_sum(const uint8_t* buf, size_t n){
    uint32_t sum = 0;
    size_t i;
    for(i = 0; i + 1 < n; i += 2){
        sum += ((uint32_t)buf[i] << 8) | buf[i + 1];
    }
    if(n & 1) sum += (uint32_t)buf[n - 1] << 8;
    while(sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

static int expect_eq(const char* name, uint16_t got, uint16_t want){
    if(got != want){
        fprintf(stderr, "FAIL %s: got=0x%04x want=0x%04x\n", name, got, want);
        return 1;
    }
    printf("OK %s = 0x%04x\n", name, got);
    return 0;
}

int main(void){
    int failed = 0;

    failed |= expect_eq("empty", fold_sum(NULL, 0), 0xFFFF);

    {
        uint8_t z[4] = {0, 0, 0, 0};
        failed |= expect_eq("zeros4", fold_sum(z, 4), 0xFFFF);
    }
    {
        uint8_t one[2] = {0x00, 0x01};
        failed |= expect_eq("u16_1", fold_sum(one, 2), (uint16_t)~0x0001u);
    }
    {
        uint8_t odd[1] = {0xAB};
        failed |= expect_eq("odd_ab", fold_sum(odd, 1), (uint16_t)~0xAB00u);
    }
    {
        /* 自反性：对缓冲写入校验和后再折合应为 0xFFFF（一补数性质） */
        uint8_t buf[4] = {0x12, 0x34, 0x00, 0x00};
        uint16_t c = fold_sum(buf, 2);
        buf[2] = (uint8_t)(c >> 8);
        buf[3] = (uint8_t)(c & 0xFF);
        uint32_t sum = 0;
        sum += ((uint32_t)buf[0] << 8) | buf[1];
        sum += ((uint32_t)buf[2] << 8) | buf[3];
        while(sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
        failed |= expect_eq("ones_complement_closed", (uint16_t)sum, 0xFFFF);
    }

    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
