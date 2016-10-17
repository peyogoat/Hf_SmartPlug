#ifndef _BUFMAN_H_
#define _BUFMAN_H_
#include "gagent.h"

typedef int (*filter_t)(void*, u8*);
typedef struct BufMan_t
{
    int cap; /* 有效空间 */
    int size;  /* 有效数据量 */
    int enable; /* 是否可用 */
    int using; /* 读写使能标志 */

    filter_t filter;            /* 解析报文的 */
    u8* buf; /* 起始位置 */
    u8* tail; /* 有效空间的下一位 */
    u8* pread; /* 读指针 */
    u8* pwrite; /* 写指针 */
}BufMan, *pBufMan;

void clearBufMan(BufMan* bm);
void initBufMan(BufMan* bm, filter_t filter, int cap);
int filter_MCUPacket_v3(BufMan* bm, u8* buf);
int pushBufMan(BufMan* bm, u8 *data, int len);
int getBufMan(BufMan* bm, u8* buf, filter_t filter);
int mcuData_get(pBufMan pbm, u8* dest);
int mcuData_push(pBufMan pbm, u8* data, u32 len);

#endif