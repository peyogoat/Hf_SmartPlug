#include "gagent_typedef.h"
#include "bufman.h"

/* 此部分注释使用Emacs来写，在keil，Bcompare中均可识别 */
// these comment writed with emacs, can be identify by keil, Bcompare
// not in SourceInsight, Dev-cpp
// please read with UTF-8 codeset
// 从MCU驱动接收数据，解析出完整的报文包返回给调用层
// data：从驱动获取的数据
// len：驱动数据长度
// dest：上层接收缓冲区
// retval：报文包的长度
// Global var：MCUDataBuf，size：4K，allocate：heap
/* 清空缓冲区管理器，恢复所有的变量到初始状态 */
void clearBufMan(BufMan* bm)
{
    bm->size = 0;
    bm->using = 0;
    bm->tail = bm->buf + bm->cap + 1;
    bm->enable = 1;
    bm->pread = bm->buf;
    bm->pwrite = bm->buf;
    return;
}
/* 初始化一个缓冲区管理器 */
void initBufMan(BufMan* bm, filter_t filter, int cap)
{
    memset(bm, 0x0, sizeof(BufMan));
    bm->cap = cap; // 注意下面
    bm->buf = malloc(cap);
    if(bm->buf == NULL)
    {
        bm->cap = 0; // 这里
    }
    else
    {
        bm->filter = filter;
        clearBufMan(bm); // 设置初始状态
    }
    return;
}
/* 检测数据 */
/* 并将有效数据存入接受缓冲区buf */
/* 使用0xff 0xff检测起始，后面两位作为长度 */
/* 仅判断长度是否超出数据量，不对结尾是否正确判断 */
/* 如果长度错误，一定会收取错误，包括影响后面的报文 */
int filter_MCUPacket_v4(BufMan* bm, u8* buf)
{
    int ret = 0, i = 0;
    int prevflag = 0;
    short len = 0;

    /* 找两个连续的0xff标记。 */
    /* 使用prevflag来表示前一个字符的状态 */
    /* pread的使用说明： */
    /* 和pwrite一样，指向可以读取区域的首个字符，可以指向无效区域ptail */
    /* 需要在读取前先检测是否需要回卷 */
    while(bm->size)
    {
        /* 此段代码均为检测是否进行回卷 */
        if(bm->pread == bm->tail)
        {
            bm->pread = bm->buf;
        }
        if(*(bm->pread) == 0xFF)
        {
            if(prevflag == 1)
            {
                /* 找到连续标记 */
                /* 此时pread指向第二个标记 */
                bm->pread++;
                bm->size--;
                prevflag = 2;
                break;
            }
            else
            {
                /* 第一次找到 */
                prevflag = 1;
            }
        }
        else
        {
            /* 不是0xff，则标记重置。包括只有一个0xff的情况 */
            prevflag = 0;
        }
        /* 往后走 */
        bm->pread++;
        bm->size--;
    }
    /* size为0 */
    if((prevflag != 2)
       || (bm->size < 3))
        /* 或者小于个有效字节。完整的报文需要包含2个起始标志和2个长度位 */
    {
        clearBufMan(bm);
        return -1;
    }
    /* 找到标记，开始读取数据 */
    prevflag = 0;
    while(bm->size)
    {
        if(bm->pread == bm->tail)
        {
            bm->pread = bm->buf;
        }
        /* 使用prevflag表示是否读出长度 */
        if(prevflag == 2)
        {
            if(len > bm->size)
            {
                clearBufMan(bm);
                return -1;
            }

            buf[0] = 0xff;
            buf[1] = 0xff;
            buf[2] = (len >> 8) & 0xFF;
            buf[3] = len & 0xFF;
            buf += 4;
            /* 按长度读取数据 */
            /* 同时注意len超出有效内容的情况 */
            /* 如果发生，返回失败。而且此时pread已经读过起始标志， */
            /* 下次会找下一个报文的起始 */
            /* 这时pread和pwrite重合（未验证），size为0 */

            for(i = 0; i < len, bm->size; i++)
            {
                if(bm->pread == bm->tail)
                {
                    bm->pread = bm->buf;
                }
                buf[i] = *(bm->pread++);
                bm->size--;
            }
            break;
        }
        else
        {
            /* 对长度进行移位处理，等同于ntohs */
            len = len << 8;
            len += *bm->pread;
            prevflag++;
        }
        bm->pread++;
        bm->size--;
    }
    /* len是从数据中解析出来的，还需要加上报文头的长度 */
    return len + 4;
}

int filter_MCUPacket_v3(BufMan* bm, u8* buf)
{
    u8 v3header[] = {0x00, 0x00, 0x00, 0x03};
    int ret = 0, i = 0;
    int prevflag = 0;
    short len = 0;
    int size = 0;
    u8 *pread;

    /* 找两个连续的0xff标记。 */
    /* 使用prevflag来表示前一个字符的状态 */
    /* pread的使用说明： */
    /* 和pwrite一样，指向可以读取区域的首个字符，可以指向无效区域ptail */
    /* 需要在读取前先检测是否需要回卷 */
    while(bm->size)
    {
        /* 此段代码均为检测是否进行回卷 */
        if(bm->pread == bm->tail)
        {
            bm->pread = bm->buf;
        }
        if(*(bm->pread) == v3header[prevflag])
        {
            if(prevflag == 0)
            {
                size = bm->size;
                pread = bm->pread;
            }
            prevflag++;
            if(prevflag == 4)
            {
                /* 找到连续标记 */
                /* 此时pread指向第二个标记 */
                bm->pread++;
                bm->size--;
                break;
            }
        }
        else
        {
            /* 不是0xff，则标记重置。包括只有一个0xff的情况 */
            prevflag = 0;
        }
        /* 往后走 */
        bm->pread++;
        bm->size--;
    }
    /* size为0 */
    if((prevflag != 4)
       || (bm->size < 4)) /* 或者小于个有效字节。完整的报文需要包含2个起始标志和2个长度位 */
    {
        /* clearBufMan(bm); */
        bm->size = size;
        bm->pread = pread;

        return -1;
    }
    /* 找到标记，开始读取数据 */
    prevflag = 0;
    while(bm->size)
    {
        if(bm->pread == bm->tail)
        {
            bm->pread = bm->buf;
        }
        /* 使用prevflag表示是否读出长度 */
        if(prevflag == 2)
        {
            if(len > bm->size)
            {
                /* clearBufMan(bm); */
                bm->size = size;
                bm->pread = pread;
                return -1;
            }
            buf[0] = 0x00;
            buf[1] = 0x00;
            buf[2] = 0x00;
            buf[3] = 0x03;
            buf[4] = (len >> 8) & 0xFF;
            buf[5] = len & 0xFF;
            buf += 6;
            /* 按长度读取数据 */
            /* 同时注意len超出有效内容的情况 */
            /* 如果发生，返回失败。而且此时pread已经读过起始标志， */
            /* 下次会找下一个报文的起始 */
            /* 这时pread和pwrite重合（未验证），size为0 */

            for(i = 0; (i < len) && (bm->size); i++)
            {
                if(bm->pread == bm->tail)
                {
                    bm->pread = bm->buf;
                }
                buf[i] = *(bm->pread++);
                bm->size--;
            }
            break;
        }
        else
        {
            /* 对长度进行移位处理，等同于ntohs */
            len = len << 8;
            len += *bm->pread;
            prevflag++;
        }
        bm->pread++;
        bm->size--;
    }
    /* len是从数据中解析出来的，还需要加上报文头的长度 */
    return len + 6;
}
/* 实现存入数据 */
/* 仅在数据包超出空闲空间才会失败 */
int pushBufMan(BufMan* bm, u8 *data, int len)
{
    int i;
    /* 判断空闲空间 */
    if(len > (bm->cap -bm->size))
    {
        return -1;
    }
    while(bm->using == 1)
    {
        msleep(1);
    }
    bm->using = 1;
    /* 按字节存入缓冲区 */
    /* pwrite指向可写区域的第一个字节。也会指向ptail，尽管此时已经超出范围 */
    /* 初始为bm->buf */
    /* 最大为size=cap时，和pread重叠 */
    for(i = 0; i < len; i++)
    {
        /* 回绕 */
        /* 故此处要先进行检查 */
        if(bm->pwrite == bm->tail)
        {
            bm->pwrite = bm->buf;
        }
        *bm->pwrite = data[i];
        bm->pwrite++;
        bm->size++;
    }
    bm->using = 0;
    return len;
}
/* typedef int(*filter_t)(BufMan*, u8*); */
/* 中间层。用户可以写自己的filter函数 */
int getBufMan(BufMan* bm, u8* buf, filter_t filter)
{
    int ret;
    while(bm->using == 1)
    {
        msleep(1);
    }
    bm->using = 1;
    ret = filter(bm, buf);
    bm->using = 0;
    return ret;
}
/* 全局缓冲管理器 */

/* 从缓冲区获取数据 */
int mcuData_get(pBufMan pbm, u8* dest)
{
    /* if(pbm->enable == 0) */
    /* { */
    /*     initBufMan(pbm, 4096); */
    /* } */
    if(NULL == pbm ||
        pbm->enable == 0)
    {
        initBufMan(pbm, (filter_t)filter_MCUPacket_v3, 4096);
    }
    /* no data to process */
    if(pbm->size == 0)
    {
        return 0;
    }
    return getBufMan(pbm, dest, pbm->filter);
}

/* 向缓冲区放入数据 */
/* 不对数据有效性进行校验。可以存放任意数据进入 */
int mcuData_push(pBufMan pbm, u8* data, u32 len)
{
    /* 使用的管理器是否可用 */
    if(NULL == pbm ||
       pbm->enable == 0)
    {
        initBufMan(pbm, (filter_t)filter_MCUPacket_v3, 4096);
    }

    /* if(pbm->enable == 0) */
    /* { */
    /*     initBufMan(pbm, 4096); */
    /* } */
    /* 没有需要处理的数据 */
    if((len < 1)
       || (len > (pbm->cap - pbm->size)))
    {
        return -1;
    }

    /* 取出数据 */
    return pushBufMan(pbm, data, len);
}
