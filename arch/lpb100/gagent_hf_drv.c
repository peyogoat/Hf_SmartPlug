#include <hsf.h>
#include <hfnet.h>
#include <hfwifi.h>
#include "hfgpio.h"
#include "hftimer.h"
#include "gagent.h"
#include "gagent_md5.h"

/* define 宏定义段 */
#define MAX_TIMER_COUNT 10
#define WIFI_STATE_DOWN 0x00
#define WIFI_STATE_UP 0x01

#define WATCHDOG_FEED_TIME 30

#define LED_RED HFGPIO_F_NREADY
#define LED_GREEN HFGPIO_F_NLINK

#define OTA_PORT13 HFGPIO_F_SLEEP_RQ
#define OTA_PORT15 HFGPIO_F_SLEEP_ON
#define pull_up hfgpio_fset_out_low
#define low_down hfgpio_fset_out_high


#define KBYTE 1024
#define MBYTE KBYTE * 1024

struct sockaddr_t g_stUDPBroadcastAddr;
int g_UDPServerFd_JD;
int g_UDPBroadcastServerFd;
u32 MS_TIMER;
/* 串口描述符 */
static hfuart_handle_t uart_MCU;
static int OTAflag = TRUE;
static int connectflag = 0;
/* 通用AT命令接口缓冲区 */
static char rec[256] = {0};
static char cmbuf[256] = {0};
hftimer_handle_t timers;
unsigned int ledstatus = 0;
static int l_LedStatus = 0;
static fd_set readfds, exceptfds;
static int wifiLevel = 0;
static NetHostList_str *hfaplist = NULL;
static NetHostList_str saplist;

extern hfthread_hande_t mt;

/* 函数声明 */
unsigned char * transcode(unsigned char *src, int size, unsigned char *buf);
/* 函数实现 */
static int apnum = 0;
char* _atexec(const char *content)
{
    int rst = 0;
    ZERO(rec, sizeof(rec));
    ZERO(cmbuf, sizeof(cmbuf));
    sprintf(rec, "AT+%s\r\n", content);
    rst = hfat_send_cmd(rec, strlen(rec), cmbuf, sizeof(cmbuf));
    if(rst == HF_SUCCESS)
    {
        return cmbuf;
    }
    return NULL;
}
/* 返回执行结果 */
char* atexec(const char *content)
{
    char *s = _atexec(content);
    return s;
}

/* void命令 */
int atexec0(const char *content)
{
    char *s = NULL;
    s = _atexec(content);
    if(s != NULL &&
       strncmp(s, "+ok", strlen("+ok")) == 0)
    {
        return HF_SUCCESS;
    }
    return -1;
}

/* 获取系统运行时间 */
uint32 GAgent_GetDevTime_S()
{
    return hfsys_get_time()/1000;
}

uint32 GAgent_GetDevTime_MS()
{
    return hfsys_get_time();
}

/* 打开串口。ABI */
int serial_open(char *comport, int bandrate,int nBits,char nEvent,int nStop)
{
    return (int)uart_MCU;
}

/* 向串口发送数据。ABI */
int serial_write(int serial_fd,unsigned char *buf,int buflen)
{
    hfuart_send(uart_MCU, (char*)buf, buflen, 1000);
    return buflen;
}

/* 在字符串中查找子串 */
char* GAgent_strstr(const char*s1, const char*s2)
{
    int n;

    if(*s2 != NULL)
    {
        while(*s1)
        {
            for(n=0;*(s1+n)==*(s2+n);n++)
            {
                if(!*(s2+n+1))
                    return (char*)s1;
            }
            s1++;
        }
        return NULL;
    }
    else
    {
        return(char*)s1;
    }
}

/* DNS解析函数 */
int gethostbyname(char *name, char ipaddress[], int len)
{
    int ret;
    ip_addr_t addr;
    if(strlen(name) < 1)
    {
        return -1;
    }
    ret = hfnet_gethostbyname(name, &addr); // 调用库函数实现
    if(ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_DEBUG, "%s:hfnet_gethostbyname faild", __func__);
        return -1;
    }
    strcpy(ipaddress, inet_ntoa(addr));
    GAgent_Printf(GAGENT_DEBUG,"%s %s %s:%s",__func__, __LINE__, name, ipaddress);
    return 0;
}

/* LED控制函数。ABI */
#define GAGENT_LED HFGPIO_F_NREADY
void led(int onoff)
{
    if(onoff)
    {
        hfgpio_fset_out_high(GAGENT_LED);
    }
    else
    {
        hfgpio_fset_out_low(GAGENT_LED);
    }
    return;
}

/* 获取Wifi模块的MAC地址。ABI */
int8 GAgent_DevGetMacAddress(uint8 *szmac)
{
    int ret;
    char buffer[64];
    // The same as AT+WSMAC
    // 使用系统AT命令获得
    ret = hfat_send_cmd("AT+WSMAC\r\n", sizeof("AT+WSMAC\r\n"), buffer, 64);
    if(ret != HF_SUCCESS)
    {
        ;
    }
    GAgent_Printf(GAGENT_DEBUG, "%s:MAC addr %s", __func__, buffer + 4);
    // 跳过返回头 "+ok=" 4个字符
    // 并将字符串结果转换成IP地址
    /* GAgent_String2MAC(buffer + 4, szmac); */
    memcpy(szmac, buffer + 4, 12);
    return 0;
}

/* 显示Wifi模块的配置信息 */
void showAPParam(void)
{
    int ret;
    char buffer[128];
    // 当前工作模式
    ret = hfat_send_cmd("AT+WMODE\r\n", sizeof("AT+WMODE\r\n"), buffer, 128); 
    GAgent_Printf(GAGENT_DEBUG, "WAPParam:WMODE=%s", buffer);
    // AP模式SSID
    ret = hfat_send_cmd("AT+WAP\r\n", sizeof("AT+WAP\r\n"), buffer, 128);
    GAgent_Printf(GAGENT_DEBUG, "WAPParam:WAP=%s", buffer);
    // AP模式KEY
    ret = hfat_send_cmd("AT+WAKEY\r\n", sizeof("AT+WAKEY\r\n"), buffer, 128);
    GAgent_Printf(GAGENT_DEBUG, "WAPParam:WAKEY=%s", buffer);
    // 软件版本
    ret = hfat_send_cmd("AT+VER\r\n", sizeof("AT+VER\r\n"), buffer, 128);
    GAgent_Printf(GAGENT_DEBUG, "WAPParam:VER=%s", buffer);
    ret = hfat_send_cmd("AT+LVER\r\n", sizeof("AT+LVER\r\n"), buffer, 128);
    GAgent_Printf(GAGENT_DEBUG, "WAPParam:LVER=%s", buffer);

    // STA模式SSID
    ret = hfat_send_cmd("AT+WSSSID\r\n", sizeof("AT+WSSSID\r\n"), buffer, 128);
    GAgent_Printf(GAGENT_DEBUG, "WAPParam:WSSID=%s", buffer);
    // STA模式KEY
    ret = hfat_send_cmd("AT+WSKEY\r\n", sizeof("AT+WSKEY\r\n"), buffer, 128);
    GAgent_Printf(GAGENT_DEBUG, "WAPParam:WSKEY=%s", buffer);
    // 设备MAC地址
    GAgent_DevGetMacAddress(buffer);
    GAgent_Printf(GAGENT_DEBUG, "WAPParam:WMAC%s", buffer);
    return;
}

/* 进入Airlink配置模式。ABI */
void GAgent_OpenAirlink(int32 timeout_s)
{
    //TODO
    pgContextData->gc.flag |= XPG_CFG_FLAG_CONFIG;
    GAgent_DevSaveConfigData( &(pgContextData->gc) );
    atexec0("SMTLK");
    return ;
}
/* AirLink配置模式结果。无需实现ABI */
void GAgent_AirlinkResult(pgcontext pgc)
{
    return ;
}

/* 关闭Wifi。ABI */
void hfwifidown(void)
{
    atexec0("WIFI=down");
    return;
}

/* 打开Wifi。ABI */
void hfwifiup(void)
{
    atexec0("WIFI=UP");
    return;
}

/* 获取模块STA配置参数，包括路由器SSID和Key */
void DRV_GAgent_GetSTAConfig(char *ssid, char *key)
{
    char buf[64];
    // STA模式SSID
    hfat_send_cmd("AT+WSSSID\r\n", sizeof("AT+WSSSID\r\n"), buf, 64);
    strcpy(ssid, buf + 4);
    // STA模式KEY
    hfat_send_cmd("AT+WSKEY\r\n", sizeof("AT+WSKEY\r\n"), buf, 64);
    strcpy(key, buf + 4);
    GAgent_Printf(GAGENT_DEBUG, "Attention:-----ssid:%s key:%s", ssid, key);
    return;
}

/* 关闭AP模式。ABI */
void GAgent_DRVWiFi_APModeStop(pgcontext pgc)
{
    /* 不需要做任何事 */
    hfwifidown();
    return;
}

/* 检查串口是否可读。ABI */
int check_uart(pgcontext pgc)
{
    int ret, mculen, readlen;
    u32 savetime = 0, ctime = 0;
    short *mdlen = NULL;
    char cread;



    FD_ZERO(&readfds);
    FD_ZERO(&exceptfds);
    FD_SET((int)uart_MCU, &readfds);
    struct timeval_t t;

    t.tv_sec = 0;
    t.tv_usec = 0;
    ret = hfuart_select(((int)uart_MCU + 1), &readfds, NULL, &exceptfds, &t);
    return ret;
}

/* 从串口读取数据。ABI */
int serial_read(int fd, unsigned char *buf, int buflen)
{
    int ret = 0;
    /* if(FD_ISSET((int)uart_MCU, &readfds)) */
    /* { */
        ret = hfuart_recv(uart_MCU, buf, buflen, 10);
        if(ret > 0)
        {
            /* AS; */
            /* 耗时 */
            /* GAgent_DebugPacket(buf, ret); */
            /* AS; */
        }
    /* } */
    return ret;
}


/* 重启设备 */
void GAgent_DevReset(void)
{
    hfsys_reset();
    return;
}

/* 获取无线连接的信号质量 */
int wifislq(void)
{
    int ret;
    char buffer[128];
    /* 当前工作模式 */
    ret = hfat_send_cmd("AT+WSLQ\r\n", sizeof("AT+WSLQ\r\n"), buffer, 128);
    GAgent_Printf(GAGENT_DEBUG, "%s", buffer);
    /* +ok=Normal, 57% */
    /* Disconnected */
    if(GAgent_strstr(buffer + 4, "Disconnected") != NULL)
    {
        return 0;
    }
    *(GAgent_strstr(buffer + 4, "%")) = '\0';
    ret = atoi(GAgent_strstr(buffer + 4, ",") + 1);
    return ret;
}

/* 根据信号质量，计算信号等级 */
int XPG_GetWiFiLevel(char wfiLevel)
{
    int ret;
    if(wfiLevel <= 0 || wfiLevel > 100)
    {
        return 0;
    }
    if(wfiLevel == 100)
    {
        wfiLevel--;
    }
    switch(wfiLevel / 10)
    {
        // WIFI_LEVEL_0 ~ WIFI_LEVEL_1
    case 0:
    case 1:
        ret = 1;
        break;
        // 1 ~ 2
    case 2:
    case 3:
        ret = 2;
        break;
        // 2 ~ 3
    case 4:
        ret = 3;
        break;
        // 3 ~ 4
    case 5:
        ret = 4;
        break;
        // 4 ~ 5
    case 6:
        ret = 5;
        // 5 ~ 6
    case 7:
        ret = 6;
        break;
        // 6+
    case 8:
    case 9:
    case 10:
        ret = 7;
        break;
    default:
        ret = 0;
        break;
    }
    return ret;
}
/* 打印日志接口。ABI */
void DRV_ConAuxPrint(char *buffer, int len, int level)
{
    u_printf(buffer);
    return;
}

/* 模块在循环中需要底层执行的动作，比如刷新系统。ABI */
void GAgent_DevTick()
{
    /* MBM 多线程的情况下，不适用 */
    static int feedtime = 0;
    int i = GAgent_GetDevTime_S();
    if((i - feedtime) > WATCHDOG_FEED_TIME)
    {
        /* hfthread_reset_softwatchdog(NULL); */
        GAgent_Printf(GAGENT_DEBUG, "feed dog");
        feedtime = i;
    }

}

/* 扫描热点信息 */
void DRV_GAgent_WiFiStartScan()
{
    ;
}

/* 设置LED状态。ABI */
void GAgent_DevLED_Red(uint8 onoff)
{

    if(onoff)
    {
        pull_up(LED_RED);
    }
    else
    {
        low_down(LED_RED);
    }
    return; /*red led*/
}
void GAgent_DevLED_Green(uint8 onoff)
{
    if(onoff)
    {
        pull_up(LED_GREEN);
    }
    else
    {
        low_down(LED_GREEN);
    }
    return;  /*green led*/    
}

/* select在平台的封装。ABI */
int32 GAgent_select(int32 nfds, fd_set *readfds, fd_set *writefds,
                    fd_set *exceptfds,int32 sec,int32 usec)
{
    struct timeval t;
    int ret = 0, rv = 0;
    fd_set tmpfds;
    int uartcheck = 0;
    /* 汉枫模块的串口使用专用的select接口 */
    /* 需要先判断是否需要检测串口 */
    if(FD_ISSET((int)uart_MCU, readfds))
    {
        uartcheck = 1;
    }
    t.tv_sec = 0; //sec; // 秒
    t.tv_usec = 0; //usec;  // 微秒
    ret = select(nfds,readfds,writefds,exceptfds,&t);
    //AS;

    /* 如果需要，单独对串口进行select */
    if(uartcheck == 1)
    {
        FD_ZERO(&tmpfds);
        FD_SET((int)uart_MCU, &tmpfds);
        t.tv_sec = 0; //sec; // 秒
        t.tv_usec = 0; //usec;  // 微秒
        rv = hfuart_select((int)uart_MCU + 1, &tmpfds, NULL, NULL, &t);
        if(rv > 0 &&
           FD_ISSET((int)uart_MCU, &tmpfds))
        {
            /* 然后合并结果 */
            FD_SET((int)uart_MCU, readfds);
            ret += rv;
        }
    }

    return ret;
}

/* 打开系统串口。ABI */
int32 GAgent_OpenUart( int32 BaudRate,int8 number,int8 parity,int8 stopBits,int8 flowControl )
{
    int32 uart_fd=0;
    uart_fd = serial_open( NULL,BaudRate,number,'N',stopBits );
    if( uart_fd<=0 )
        return (-1);
    return uart_fd;
}

/* 打开串口。ABI */
void GAgent_LocalDataIOInit(pgcontext pgc)
{
    pgc->rtinfo.local.uart_fd = GAgent_OpenUart(9600,8,0,0,0);
    while(pgc->rtinfo.local.uart_fd <=0)
    {
        pgc->rtinfo.local.uart_fd = GAgent_OpenUart(9600,8,0,0,0);
        sleep(1);
    }
    //serial_write(pgc->rtinfo.local.uart_fd,"GAgent Start !!!",strlen("GAgent Start !!!"));
    return ;
}

/* 设置socket的keepalive值 */
void set_clientalive(int tcpfd)
{
    int tmp = 1;
    setsockopt(tcpfd, SOL_SOCKET, SO_KEEPALIVE, (void*)&tmp, sizeof(tmp));
    tmp = 5;
    setsockopt(tcpfd, IPPROTO_TCP, TCP_KEEPIDLE, (void*)&tmp, sizeof(tmp));
    tmp = 5;
    setsockopt(tcpfd, IPPROTO_TCP, TCP_KEEPINTVL, (void*)&tmp, sizeof(tmp));
    tmp = 3;
    setsockopt(tcpfd, IPPROTO_TCP, TCP_KEEPCNT, (void*)&tmp, sizeof(tmp));
    return;
}
/* 设置socket非阻塞 */
void set_socknoblk(int sockfd)
{
    lwip_fcntl(sockfd, F_SETFL, O_NONBLOCK);
}

void set_sockblk(int sockfd, int block)
{
    volatile int flag = 0;
    flag = lwip_fcntl(sockfd, F_GETFL, 0);
    if(block)
    {
        flag &= ~O_NONBLOCK;
    }
    else
    {
        flag |= O_NONBLOCK;
    }
    lwip_fcntl(sockfd, F_SETFL, flag);
}

/* DNS解析。ABI */
uint32 GAgent_GetHostByName(int8* domain, int8 *IPAddress)
{
    return gethostbyname(domain, (u8 *)IPAddress, 32);
}
/* Socket族接口底层封装 */
int Socket_sendto(int sockfd, u8 *data, int len, void *addr, int addr_size)
{
    return sendto(sockfd, data, len, 0, (struct sockaddr *)addr, addr_size);
}
int Socket_accept(int sockfd, void *addr, u32_t *addr_size)
{
    return accept(sockfd, (struct sockaddr*)addr, addr_size);
}
int Socket_recvfrom(int sockfd, u8 *buffer, int len, void *addr, u32_t *addr_size)
{
    return recvfrom(sockfd, buffer, len, 0, (struct sockaddr *)addr, addr_size);
}

int32 GAgent_connect(int iSocketId, uint16 port, int8 *ServerIpAddr, int8 flag)
{
#if 1
    /* 使用非阻塞connect。非阻塞主要目的在于自定超时时间 */
    int8 ret=0;
    struct sockaddr_t Msocket_address;
    fd_set rfds, wfds;
    struct timeval tv;

    memset(&Msocket_address, 0x0, sizeof(struct sockaddr_t));
    GAgent_Printf(GAGENT_INFO,"do connect ip:%s port=%d",ServerIpAddr,port );
    /* set_socknoblk(iSocketId); */
    set_sockblk(iSocketId, 0);
    // Create the stuff we need to connect
    Msocket_address.sin_family = AF_INET;
    Msocket_address.sin_port= htons(port);// = port;
    Msocket_address.sin_addr.s_addr = inet_addr(ServerIpAddr);
    ret = connect(iSocketId, (struct sockaddr *)&Msocket_address, sizeof(struct sockaddr_t));
    if(0 == ret)
    {
        return iSocketId;
    }
    else
    {
        if(errno == EINPROGRESS)
        {
            FD_ZERO(&rfds);
            FD_ZERO(&wfds);
            FD_SET(iSocketId, &rfds);
            FD_SET(iSocketId, &wfds);
            tv.tv_sec = 4;
            tv.tv_usec = 0;
            ret = select(iSocketId + 1, &rfds, &wfds, NULL, &tv);
            switch(ret)
            {
            case -1:
                return -1;
                break;
            case 0:
                return -1;
                break;
            default:
                if(FD_ISSET(iSocketId, &rfds) ||
                   FD_ISSET(iSocketId, &wfds))
                {
                    set_sockblk(iSocketId, 1);
                    return iSocketId;
                }
            }
        }
        return  -1;
    }
#else
    int8 ret=0;
    struct sockaddr_t Msocket_address;

    memset(&Msocket_address, 0x0, sizeof(struct sockaddr_t));
    GAgent_Printf(GAGENT_INFO,"do connect ip:%s port=%d",ServerIpAddr,port );
    /* set_socknoblk(iSocketId); */
    // Create the stuff we need to connect
    Msocket_address.sin_family = AF_INET;
    Msocket_address.sin_port= htons(port);// = port;
    Msocket_address.sin_addr.s_addr = inet_addr(ServerIpAddr);
    ret = connect(iSocketId, (struct sockaddr *)&Msocket_address, sizeof(struct sockaddr_t));
    if( ret==0)
        return iSocketId;
    else
        return  -1;

#endif
}
int32 GAgent_CreateTcpServer(uint16 tcp_port)
{
    int bufferSize;
    struct sockaddr_t addr;
    int g_TCPServerFd = -1;
    if (g_TCPServerFd < 1)
    {
        g_TCPServerFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(g_TCPServerFd == 0)
        {
            g_TCPServerFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        }
        if(g_TCPServerFd < 0)
        {
            GAgent_Printf(GAGENT_DEBUG, "Create TCPServer failed.errno:%d", errno);
            g_TCPServerFd = -1;
            return g_TCPServerFd;
        }
        //not support setting recive buffer size
        memset(&addr, 0x0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port=htons(tcp_port);
        addr.sin_addr.s_addr=INADDR_ANY;
        if(bind(g_TCPServerFd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        {
            GAgent_Printf(GAGENT_DEBUG, "TCPSrever socket bind error,errno:%d", errno);
            close(g_TCPServerFd);
            g_TCPServerFd = -1;
            return g_TCPServerFd;
        }

        if(listen(g_TCPServerFd, 0) != 0)
        {
            GAgent_Printf(GAGENT_DEBUG, "TCPServer socket listen error,errno:%d", errno);
            close(g_TCPServerFd);
            g_TCPServerFd = -1;
            return g_TCPServerFd;
        }


    }
    else
    {
        /**/
        ;
    }
    GAgent_Printf(GAGENT_DEBUG,"TCP Server socketid:%d on port:%d", g_TCPServerFd, tcp_port);
    return g_TCPServerFd;
}

int32 GAgent_CreateUDPServer(uint16 udp_port)
{
    struct sockaddr_t addr;
    int g_UDPServerFd = -1;
    if (g_UDPServerFd < 1)
    {
        g_UDPServerFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if(g_UDPServerFd < 0)
        {
            GAgent_Printf(GAGENT_DEBUG, "UDPServer socket create error,errno:%d", errno);
            g_UDPServerFd = -1;
            return g_UDPServerFd ;
        }
        memset(&addr, 0x0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(udp_port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if(bind(g_UDPServerFd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        {
            GAgent_Printf(GAGENT_DEBUG, "UDPServer socket bind error,errno:%d", errno);
            close(g_UDPServerFd);
            g_UDPServerFd = -1;
            return g_UDPServerFd ;
        }
    }
    GAgent_Printf(GAGENT_DEBUG,"UDP Server socketid:%d on port:%d", g_UDPServerFd, udp_port);
    return g_UDPServerFd;
}

int32 GAgent_CreateUDPBroadCastServer(uint16 udp_port, struct sockaddr_in *addr)
{

    int udpbufsize = 1;
    int g_UDPBroadcastServerFd = -1;

    if(g_UDPBroadcastServerFd < 1)
    {
        g_UDPBroadcastServerFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if(g_UDPBroadcastServerFd < 0)
        {
            GAgent_Printf(GAGENT_DEBUG, "UDP BC socket create error,errno:%d", errno);
            g_UDPBroadcastServerFd = -1;
            return g_UDPBroadcastServerFd ;
        }

        g_stUDPBroadcastAddr.sin_family = AF_INET;
        g_stUDPBroadcastAddr.sin_port=htons(udp_port);
        g_stUDPBroadcastAddr.sin_addr.s_addr=htonl(INADDR_BROADCAST);
        addr->sin_family = AF_INET;
        addr->sin_port=htons(udp_port);
        addr->sin_addr.s_addr=htonl(INADDR_BROADCAST);

        if(setsockopt(g_UDPBroadcastServerFd, SOL_SOCKET, SO_BROADCAST, &udpbufsize, 4) != 0)
        {
            GAgent_Printf(GAGENT_DEBUG,"UDP BC Server setsockopt error,errno:%d", errno);
            //return g_UDPBroadcastServerFd;
        }
        /* if(bind(g_UDPBroadcastServerFd, (struct sockaddr *)&g_stUDPBroadcastAddr, sizeof(g_stUDPBroadcastAddr)) != 0) */
        /* { */
        /*     GAgent_Printf(GAGENT_DEBUG,"UDP BC Server bind error,errno:%d", errno); */
        /*     close(g_UDPBroadcastServerFd); */
        /*     g_UDPBroadcastServerFd = -1; */
        /*     return g_UDPBroadcastServerFd; */
        /* } */

    }
    GAgent_Printf(GAGENT_DEBUG,"UDP BC Server socketid:%d on port:%d", g_UDPBroadcastServerFd, udp_port);
    return g_UDPBroadcastServerFd;
}

void GAgent_CreateUDPServer_JD(int udp_port)
{
    struct sockaddr_t addr;
    int udpbufsize=2;
    int g_UDPServerFd_JD = -1;
    if (g_UDPServerFd_JD < 1)
    {

        g_UDPServerFd_JD = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        GAgent_Printf(GAGENT_INFO,"GOTO %s:%d", __FUNCTION__, __LINE__);
        if(g_UDPServerFd_JD < 1)
        {
            GAgent_Printf(GAGENT_ERROR, "UDPServer socket create error,errno:%d", errno);
            g_UDPServerFd_JD = -1;
            return ;
        }

        lwip_fcntl(g_UDPServerFd_JD, F_SETFL, O_NONBLOCK);
        memset(&addr, 0x0, sizeof(addr));
        addr.sin_port =htons(udp_port);
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr=htonl(INADDR_ANY);

        if(bind(g_UDPServerFd_JD, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        {
            GAgent_Printf(GAGENT_ERROR, "UDPServer socket bind error,errno:%d", errno);
            close(g_UDPServerFd_JD);
            g_UDPServerFd_JD = -1;
            return ;
        }
        GAgent_Printf(GAGENT_INFO,"UDP Server for JD socketid:%d on port:%d", g_UDPServerFd_JD, udp_port);
    }
    
    return;
}

void Socket_CreateWebConfigServer(int tcp_port)
{
    int bufferSize;
    struct sockaddr_t addr;
    int g_TCPWebConfigFd = -1;
    if (g_TCPWebConfigFd < 1) 
    {
        g_TCPWebConfigFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if(g_TCPWebConfigFd < 0)
        {
            GAgent_Printf(GAGENT_ERROR, "Create TCPServer failed.errno:%d", errno);
            g_TCPWebConfigFd = -1;
            return;
        }

        memset(&addr, 0x0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port=htons(tcp_port);
        addr.sin_addr.s_addr=htonl(INADDR_ANY);
        if(bind(g_TCPWebConfigFd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        {
            GAgent_Printf(GAGENT_ERROR, "TCPSrever socket bind error.errno:%d", errno);
            close(g_TCPWebConfigFd);
            g_TCPWebConfigFd = -1;
            return;

        }
        if(listen(g_TCPWebConfigFd, 0) != 0)
        {
            GAgent_Printf(GAGENT_ERROR, "TCPServer socket listen error.errno:%d", errno);
            close(g_TCPWebConfigFd);
            g_TCPWebConfigFd = -1;
            return;
        }
    }
    else
    {
        /**/
        ;
    }
    GAgent_Printf(GAGENT_INFO,"TCP Server socketid:%d on port:%d", g_TCPWebConfigFd, tcp_port);
    return;	
	
}

/* 检查信号强度 */
void wifiStrengthcheck(void)
{
    int wifiStrength, wifiLevel;
    /* if((wifiStatus & WIFI_MODE_STATION) != WIFI_MODE_STATION) */
    /* { */
    /*     return; */
    /* } */
    wifiStrength = wifislq();
    /* if(abs(wifiStrength-g_Xpg_GlobalVar.wifiStrength)>=10) */
    /* { */
    /*     g_Xpg_GlobalVar.wifiStrength = wifiStrength; */
    /*     wifiLevel = XPG_GetWiFiLevel(wifiStrength); */
    /*     wifiStatus &= ~(WIFI_LEVEL); */
    /*     wifiStatus = wifiStatus|(wifiLevel<<8); */
    /* } */
    return;
}

typedef struct scan_result
{
    int num;
    int allap;
    int result;
    int maxnum;
    PWIFI_SCAN_RESULT_ITEM apbuf;
    PWIFI_SCAN_RESULT_ITEM pwifi;
}scan_res;
scan_res sr;

/* 热点搜索回调 */
int hfwifi_scan_test(PWIFI_SCAN_RESULT_ITEM scan_ret)
{
    int i;
    if(sr.allap != 1)
    {
        if(strcmp(sr.pwifi->ssid, scan_ret->ssid) == 0)
        {
            memcpy(sr.pwifi, scan_ret, sizeof(WIFI_SCAN_RESULT_ITEM));
            sr.result = 1;
        }
    }
    else
    {
        if(sr.num < sr.maxnum)
        {
            memcpy(&sr.apbuf[sr.num], scan_ret, sizeof(WIFI_SCAN_RESULT_ITEM));
            sr.num++;
        }
    }
#if 0
    GAgent_Printf(GAGENT_DEBUG, "%s,%d,%d,%d,%d,%X%X%X%X%X%X", scan_ret->ssid,
                  scan_ret->auth,
                  scan_ret->encry,
                  scan_ret->channel,
                  scan_ret->rssi,
                  ((u8*)scan_ret->mac)[0],
                  ((u8*)scan_ret->mac)[1],
                  ((u8*)scan_ret->mac)[2],
                  ((u8*)scan_ret->mac)[3],
                  ((u8*)scan_ret->mac)[4],
                  ((u8*)scan_ret->mac)[5]
       );
    GAgent_Printf(GAGENT_DEBUG, "\r\n");
#endif
    return 0;
}

/* 搜索热点 */
void scanWifi(PWIFI_SCAN_RESULT_ITEM pwifi)
{
    /* 搜索所有热点 */
    if(NULL == pwifi)
    {
        sr.allap = 1;
        sr.num = 0;
        sr.result = 0;
        if(sr.maxnum == 0)
        {
            sr.maxnum = 32;
        }
        if(sr.apbuf == NULL)
        {
            sr.apbuf = (PWIFI_SCAN_RESULT_ITEM)malloc(sizeof(WIFI_SCAN_RESULT_ITEM) * sr.maxnum);
            if(NULL == sr.apbuf)
            {
                return ;
            }
        }

    }
    /* 搜索指定热点，信息保存在pwifi */
    else
    {
        sr.allap = 0;
        sr.apbuf = NULL;
        sr.pwifi = pwifi;
        sr.result = 0;
    }
    GAgent_Printf(GAGENT_DEBUG, "ssid,auth,encry,channel,rssi,mac\r\n");
    hfwifi_scan(hfwifi_scan_test);
}

/* Wifi的加密方式 */
static char *auths[] =
{
    "OPEN",
    "SHARED",
    "WPAPSK",
    "WPA2PSK",
    "WPAPSKWPA2PSK",
};
static char *encs[] =
{
    "NONE",
    "WEP-A",
    "TKIP",
    "AES",
    "TKIPAES",
};

/* 设置为STA模式。ABI */
int16 GAgent_DRVWiFi_StationCustomModeStart(int8 *StaSsid,int8 *StaPass, uint16 wifiStatus)
{
    int ret;
    char buffer[64];
    char cmdbuf[128];
    u8 *auth, *enc;

    GAgent_Printf(GAGENT_DEBUG, "%s ssid = %s, pass = %s", __func__, StaSsid, StaPass);
    ret = hfat_send_cmd("AT+WMODE=STA\r\n", sizeof("AT+WMODE=STA\r\n"), buffer, 64);
    if (ret != HF_SUCCESS)
    {
        ;
    }

    memset(cmdbuf, 0, 128);
    sprintf(cmdbuf, "AT+WSSSID=%s\r\n", StaSsid);
    WIFI_SCAN_RESULT_ITEM wifi;
    memset(&wifi, 0x0, sizeof(wifi));
    memcpy(wifi.ssid, StaSsid, strlen(StaSsid));
    scanWifi(&wifi);
    /* 如果没有搜索到，使用默认值 */
    if(sr.result != 1)
    {
        wifi.auth = 3;
        wifi.encry = 3;
    }
    else
    {
        /*  */
        if(wifi.auth == 4)
        {
            wifi.auth = 3;
        }
        if(wifi.encry == 4)
        {
            wifi.encry = 3;
        }
    }
    hfwifidown();
    GAgent_Printf(GAGENT_DEBUG, "%s", cmdbuf);
    ret = hfat_send_cmd(cmdbuf, strlen(cmdbuf), buffer, 64);
    if (ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_ERROR,"goto  %04d", __LINE__);
        ;
    }
    GAgent_Printf(GAGENT_DEBUG,"goto  %04d", __LINE__);
    memset(cmdbuf, 0, 128);
    GAgent_Printf(GAGENT_DEBUG,"goto  %04d", __LINE__);
    sprintf(cmdbuf, "AT+WSKEY=%s,%s,%s\r\n", auths[wifi.auth], encs[wifi.encry], StaPass);
    GAgent_Printf(GAGENT_DEBUG,"goto  %04d", __LINE__);
    ret = hfat_send_cmd(cmdbuf, strlen(cmdbuf), buffer, 64);
    GAgent_Printf(GAGENT_DEBUG,"goto  %04d", __LINE__);
    if (ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_ERROR,"goto  %04d", __LINE__);
        ;
    }
    GAgent_Printf(GAGENT_DEBUG, "Success to set station mode, retval = %d", ret);
    GAgent_DevLED_Red(0);
    GAgent_DevLED_Green(1);
    hfwifiup();

    wifiStatus = GAgent_DevCheckWifiStatus(WIFI_MODE_STATION, 1);
    GAgent_Printf( GAGENT_INFO," Station ssid:%s StaPass:%s",StaSsid,StaPass );
    GAgent_DevReset();
    return WIFI_MODE_STATION;
}

/* Wifi模块工作在软AP模式。ABI */
int16 GAgent_DRV_WiFi_SoftAPModeStart(const int8 *ap_name, const int8 *ap_password, int16 wifiStatus)
{
    
    // As a cmd,it must be end with '\r\n'.reference by AT cmd guide.
    int ret;
    char buffer[64];
    char cmdbuf[64];
    // 如果获取不到MAC地址，默认为FFFF FFFF FFFF
    /* memset(buffer, 'F', sizeof(buffer)); */
    showAPParam();
    /* ret = hfat_send_cmd("AT+WSMAC\r\n", sizeof("AT+WSMAC\r\n"), buffer, 64); */
    // buffer: +ok=123456789ABC
    // 设置C为结尾
    /* buffer[ 4 + 12] = '\0'; */
    // 传9的位置，只取最后4个字母
    sprintf(cmdbuf, "AT+WAP=11BGN,%s,AUTO\r\n", ap_name);
    GAgent_Printf(GAGENT_DEBUG, "Set AP Mode:%s", cmdbuf);
    //ret = hfat_send_cmd("AT+WAP=11BGN,XPG-GAgent,AUTO\r\n", sizeof("AT+WAP=11BGN,XPG-GAgent,AUTO\r\n"), buffer, 64);
    hfwifidown();
    ret = hfat_send_cmd(cmdbuf, strlen(cmdbuf), buffer, 64);
    if (ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_DEBUG, "Faild to set AP mode");
        return 0;
    }
    sprintf(cmdbuf, "AT+WAKEY=WPA2PSK,AES,%s\r\n", ap_password);
    ret = hfat_send_cmd("AT+WAKEY=WPA2PSK,AES,123456789\r\n", strlen(cmdbuf), buffer, 64);
    if (ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_DEBUG, "Faild to set AP mode");
        return 0;
    }

    ret = hfat_send_cmd("AT+WMODE=AP\r\n", sizeof("AT+WMODE=AP\r\n"), buffer, 64);
    if (ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_DEBUG, "Faild to set AP mode");
        return 0;
    }
    hfwifiup();
#if 0
    ret = hfat_send_cmd("AT+WADHCP=on\r\n", sizeof("AT+WADHCP=on\r\n"), buffer, 64);
    if (ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_DEBUG, "Faild to set AP mode");
        return 0;
    }
    sprintf(cmdbuf, "AT+WLANN=%s,%s\r\n", AP_LOCAL_IP, AP_NET_MASK);
    ret = hfat_send_cmd(cmdbuf, strlen(cmdbuf), buffer, 64);
    if (ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_DEBUG, "Faild to set AP mode");
        return 0;
    }
#endif
    wifiStatus = GAgent_DevCheckWifiStatus(WIFI_MODE_STATION, 0);
    wifiStatus = GAgent_DevCheckWifiStatus(WIFI_MODE_AP, 1);
    GAgent_DevReset();

    return WIFI_MODE_AP;
}

/* 成功返回1。失败返回0 */
/* 仅仅搜索热点是否存在 */
/* 连接热点时的参数由STA模式自己决定 */
/* 由于sr为全局使用，故会造成某些同步问题，目前不需要处理 */
int DRV_WiFi_SearchAP(u8* ssid)
{
    WIFI_SCAN_RESULT_ITEM wifi;
    memset(&wifi, 0x0, sizeof(wifi));
    memcpy(wifi.ssid, ssid, strlen(ssid));
    sr.allap = 0;
    scanWifi(&wifi);
    if(sr.result != 1)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

/* 断开模块和热点的连接。ABI */
int16 GAgent_DRVWiFi_StationDisconnect()
{
    return 0;
}

/* 信号强度扫描。ABI */
void GAgent_DRVWiFiPowerScan(pgcontext pgc)
{
    wifiLevel = wifislq();
    /* wifiLevel = 99; */
    return;
}
/* 信号强度结果。ABI */
int8 GAgent_DRVWiFiPowerScanResult(pgcontext pgc)
{
    int8 level = wifiLevel;
    return level;
}
/* 开始扫描周围热点。ABI */
void GAgent_DRVWiFiStartScan()
{
    scanWifi(NULL);
    return;
}
/* 停止扫描周围热点。ABI */
void GAgent_DRVWiFiStopScan()
{

}
/* 热点扫描结果。ABI */
NetHostList_str *GAgentDRVWiFiScanResult(NetHostList_str *aplist)
{
    int i;
    /* 申请内存，用于保存热点列表 */
    if(sr.num <= 0)
    {
        return NULL;
    }

    if(saplist.ApList == NULL)
    {
        saplist.ApList = (ApHostList_str*)malloc(sizeof(ApHostList_str) * 32);
        if(saplist.ApList == NULL)
        {
            return NULL;
        }
    }
    ZERO(saplist.ApList, sizeof(ApHostList_str) * 32);
    saplist.ApNum = sr.num;
    for(i = 0; i < sr.num; i++)
    {
        memcpy(saplist.ApList[i].ssid, sr.apbuf[i].ssid, strlen(sr.apbuf[i].ssid));
        saplist.ApList[i].ApPower = sr.apbuf[i].rssi;
    }
    return &saplist;
}


void GAgent_TimerHandler(unsigned int alarm, void *data);
/* 硬件初始化，为Gagent做准备 */
void CoreInit(void)
{
    char buffer[64];
    int ret;

    //init_gpio();
    hfnet_set_udp_broadcast_port_valid(1,65500); // 汉枫的SDK默认关闭UDP端口广播功能，需要手动打开。

    ret = hfat_send_cmd("AT+NDBGL=5,1\r\n", sizeof("AT+NDBGL=5,1\r\n"), buffer, 64);
    if(ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_DEBUG, "Faild to set uart function");
        //return ;
    }
    // 在产品阶段（Standard模式），串口使用的标准设置
    ret = hfat_send_cmd("AT+UART=9600,8,1,NONE,NFC,0\r\n", sizeof("AT+UART=9600,8,1,NONE,NFC,0\r\n"), buffer, 64);

    if(ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_DEBUG, "Failed to set uart0");
        //return ;
    }

    ret = hfat_send_cmd("AT+UARTTE=fast\r\n", sizeof("AT+UARTTE=fast\r\n"), buffer, 64);
    if(ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_DEBUG, "Faild to set uart function");
        //return ;
    }

    ret = hfat_send_cmd("AT+UARTF=disable\r\n", sizeof("AT+UARTF=disable\r\n"), buffer, 64);
    if(ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_DEBUG, "Faild to set uart function");
        //return ;
    }


    // open uart0.Save handler in uart_MCU
    uart_MCU = hfuart_open(0);
    if(NULL == uart_MCU)
    {
        GAgent_Printf(GAGENT_DEBUG, "Failed to open uart0");
        //return;
    }

    /* timers = hftimer_create("gagent",(int32_t)1000, true, (u32_t)GAgent_TimerHandler, GAgent_TimerHandler, 0); */
    /* hftimer_start(timers); */
    /* GAgent_CreateTimer(GAGENT_TIMER_PERIOD, ONE_MINUTE * 3, wifiStrengthcheck); */
    OTAflag = true;
    /* hfthread_hande_t ht = (hfthread_hande_t)hfthread_get_current_handle(); */
    int i = WATCHDOG_FEED_TIME;
    /* hfthread_enable_softwatchdog(NULL, i); */
    /* hfthread_reset_softwatchdog(NULL); */
    return;
}

/* 设备初始化。ABI */
void GAgent_DevInit(pgcontext pgc)
{

}

/* 获取Wifi是否连接到路由器 */
int GAgent_GetWiFiStatus()
{
    int ret;
    char buffer[64] = {0};

    ret = hfat_send_cmd("AT+WSLK\r\n", sizeof("AT+WSLK\r\n"), buffer, sizeof(buffer));
    if(ret != HF_SUCCESS)
    {
        return -1;
    }
    // 如果AP SSID刚好是 "Disconnected", 返回WIFI_STATE_DOWN
    ret = strncmp(buffer, "+ok=Disconnected", sizeof("+ok=Disconnected") - 2);
    GAgent_Printf(GAGENT_DEBUG, buffer);
    //showAPParam();
    if(ret == 0)
    {
        return WIFI_STATE_DOWN;
    }

    return WIFI_STATE_UP;
}

/* 获取Wifi工作模式。事实上，用来在上电时读取系统的工作模式。ABI */
int8 GAgent_DRVGetWiFiMode(pgcontext pgc)
{
    int ret = GAgent_DRVBootConfigWiFiMode();
    uint16 tempWiFiStatus = GAgent_DevCheckWifiStatus(0xFFFF, 1);
    switch(ret)
    {
    case 1:
        GAgent_SetWiFiStatus(pgc, WIFI_MODE_STATION, 1);
        GAgent_SetWiFiStatus(pgc, WIFI_MODE_AP, 0);
        /* tempWiFiStatus |= WIFI_MODE_STATION; */
        /* tempWiFiStatus &= ~WIFI_MODE_AP; */
        DRV_GAgent_GetSTAConfig(pgc->gc.wifi_ssid, pgc->gc.wifi_key);
        GAgent_DevCheckWifiStatus(WIFI_MODE_STATION, 1);
        GAgent_DevCheckWifiStatus(WIFI_MODE_AP, 0);
        break;
    case 2:
        GAgent_SetWiFiStatus(pgc, WIFI_MODE_STATION, 0);
        GAgent_SetWiFiStatus(pgc, WIFI_MODE_AP, 1);
        /* tempWiFiStatus |= WIFI_MODE_AP; */
        /* tempWiFiStatus &= ~WIFI_MODE_STATION; */
        GAgent_DevCheckWifiStatus(WIFI_MODE_AP, 1);
        GAgent_DevCheckWifiStatus(WIFI_MODE_STATION, 0);
        break;
    case 3:
        GAgent_SetWiFiStatus(pgc, WIFI_MODE_STATION, 1);
        GAgent_SetWiFiStatus(pgc, WIFI_MODE_AP, 1);
        tempWiFiStatus |= WIFI_MODE_STATION;
        tempWiFiStatus |= WIFI_MODE_AP;
        DRV_GAgent_GetSTAConfig(pgc->gc.wifi_ssid, pgc->gc.wifi_key);
        GAgent_DevCheckWifiStatus(WIFI_MODE_AP, 1);
        GAgent_DevCheckWifiStatus(WIFI_MODE_STATION, 1);
        break;
    default:
        return 0;
        break;
    }
    return ret;
}


// 读取gagent配置信息
uint32 GAgent_DevGetConfigData(gconfig *pConfig)
{
    /* AS; */
    hffile_userbin_read(CONFIG_OFFSET, (char *)pConfig, sizeof(gconfig));
    return 0;
}

int8 Dev_GAgentGetOldConfigData( GAgent_OldCONFIG_S *p_oldgc )
{
    hffile_userbin_read(CONFIG_OFFSET, (char *)p_oldgc, sizeof(GAgent_OldCONFIG_S));
    return 0;
}

// 保存gagent配置信息
uint32 GAgent_DevSaveConfigData(gconfig *pConfig)
{
    /* pConfig->magicNumber = GAGENT_MAGIC_NUMBER; */
    // 直接将配置信息结构体放入配置文件
    if(hffile_userbin_write(CONFIG_OFFSET, (char *)pConfig, sizeof(gconfig)) != sizeof(gconfig))
    {
        GAgent_Printf(GAGENT_DEBUG, "Save Config Error");
        return -1;
    }
    else
    {
        return 0;
    }
}

void setConfig(void)
{
    // 主要检查WebConfig导致的重启
    // 理论上来说，需要重启的状态还包括切换工作模式，工作异常
    // 在这些情况下均从系统读取配置不会导致问题
    // 此模型为前配置方式，即工作模式在重启前就设置好。仅在汉枫平台适用
    /* g_stGAgentConfigData.flag |= XPG_CFG_FLAG_CONFIG; */
    /* DRV_GAgent_SaveConfigData(&g_stGAgentConfigData); */
    return;
}

/* 进入Airlink配置模式。ABI */
void GAgent_AirLink(void)
{
// 支持V3和V4版本，不需要做区分
//#ifdef GAGENT_V3PROTOCOL
    char buf[64];
    int ret;

    GAgent_Printf(GAGENT_DEBUG, "Start SmartLink Mode");
    ret = hfat_send_cmd("AT+SMTLK\r\n", sizeof("AT+SMTLK\r\n"), buf, sizeof(buf));
    GAgent_Printf(GAGENT_DEBUG, "%s", buf);
//#endif
    return;
}

/* transform hexCode to string */
/* for example: 0x0a to "A" */
unsigned char * transcode(unsigned char *src, int size, unsigned char *buf)
{
    static unsigned char table[0x10] =
        {
            '0', '1', '2', '3', '4', '5', '6', '7',
            '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
        };
    unsigned char c; //current
    int i;
    for(i = 0; i < size; i++)
    {
        c = src[i];
        buf[3 * i] = table[c >> 4];
        buf[3 * i + 1] = table[c & 0x0f];
        buf[3 * i + 2] = ' ';
    }
    return buf;
}


// 通过串口发送数据给物理MCU
void LPB100_MCU_ReciveData(void *data, int datalen)
{
    hfuart_send(uart_MCU, (char*)data, datalen, 1000);
}

/* 从串口0发送数据 */
void uart0_printf(u8 *buffer, int len)
{
    hfuart_send(uart_MCU, buffer, len, 100);
    //msleep(10);
}

/* 转换OTA的URL，分割为路径和文件名两部分 */
static char* ota_buf = NULL;
u8* parseOTAurl(u8* url)
{
    // http://api.gitwiz.com/dev/ota/download/pk/type/hard_ver/soft_ver/filename
    // http://api.gitwiz.com/dev/ota/download/pk/type/hard_ver/soft_ver,filename
    u8* c = url;
    // find last '/'
    // replace with ','
    while(*url)
    {
        if(*url == '/')
            c = url;
        url++;
    }
    // if not found, return faild
    return c;
}
/* 执行OTA。ABI */
int32 GAgent_WIFIOTAByUrl(pgcontext pgc, int8* url)
{
    u8* c;
    int ret = 0;

    GAgent_Printf(GAGENT_DEBUG, "Start Wifi OTA");

    c = parseOTAurl(url);
    if(c == url)
    {
        GAgent_Printf(GAGENT_DEBUG, "Invalid OTA url:%s", url);
        return ret;
    }
    /* replace last / with 0, cut to tow str */
    *c = '\0';
    if(ota_buf == NULL)
    {
        ota_buf = (u8*)malloc(1024);
        if(ota_buf == NULL)
        {
            return ret;
        }
    }


    sprintf(rec, "AT+UPURL=%s/,%s\r\n", url, (c + 1));
    /* AS; */
    GAgent_Printf(GAGENT_DEBUG, "%s", rec);
    /* ret = hfat_send_cmd(buf, strlen(buf), rec, 128); */

    /* GAgent_DebugPacket((u8*)&g_stGAgentConfigData, sizeof(g_stGAgentConfigData)); */
    ret = hfat_send_cmd(rec, sizeof(rec), ota_buf, 1024);
    GAgent_Printf(GAGENT_DEBUG, "%s", ota_buf);

    /* GAgent_DebugPacket((u8*)&g_stGAgentConfigData, sizeof(g_stGAgentConfigData)); */
    if(memcmp(ota_buf, "+ok=Update success", 18) == 0)
    {
        ret = RET_SUCCESS;
        GAgent_DevReset();
    }
    else
    {
        ret = RET_FAILED;
    }
    free(ota_buf);
    ota_buf = NULL;

    return ret;
}

uint32 GAgent_getIpaddr(uint8 *ipaddr)
{
    char *s = NULL, *p_start = NULL, *p_end = NULL;
    s = atexec("WANN");
    if(GAgent_strstr(s, "+ok") == NULL)
    {
        /* memcpy(ipaddr,"123.456.7.8",12); */
        return RET_FAILED;
    }
    /* AT+WANN */
    /* +ok=<mode,address,mask,gateway> */
    p_start = GAgent_strstr(s, ",");
    p_start++;
    p_end = GAgent_strstr(p_start + 1, ",");
    memcpy(ipaddr, p_start, p_end - p_start);
    return RET_SUCCESS;
}

uint32 GAgent_sendWifiInfo( pgcontext pgc )
{
    int32 pos = 0;
    uint8 ip[16] = {0};
    if(0 != GAgent_getIpaddr(ip))
    {
        GAgent_Printf(GAGENT_WARNING,"GAgent get ip failed!");
    }

    /* ModuleType */
    pgc->rtinfo.Txbuf->ppayload[0] = 0x01;
    pos += 1;

    /* MCU_PROTOCOLVER */
    memcpy( pgc->rtinfo.Txbuf->ppayload+pos, pgc->mcu.protocol_ver, MCU_PROTOCOLVER_LEN );
    pos += MCU_PROTOCOLVER_LEN;

    /* HARDVER */
    memcpy( pgc->rtinfo.Txbuf->ppayload+pos, WIFI_HARDVER, 8 );
    pos += 8;

    /* SOFTVAR */
    memcpy( pgc->rtinfo.Txbuf->ppayload+pos, WIFI_SOFTVAR, 8 );
    pos += 8;

    /* MAC */
    memset( pgc->rtinfo.Txbuf->ppayload+pos, 0 , 16 );
    memcpy( pgc->rtinfo.Txbuf->ppayload+pos, pgc->minfo.szmac, 16 );
    pos += 16;

    /* IP */
    memset( pgc->rtinfo.Txbuf->ppayload+pos, 0 , 16 );
    memcpy( pgc->rtinfo.Txbuf->ppayload+pos, ip, strlen(ip) );
    pos += 16;

    /* MCU_MCUATTR */
    memcpy( pgc->rtinfo.Txbuf->ppayload+pos, pgc->mcu.mcu_attr, MCU_MCUATTR_LEN);
    pos += MCU_MCUATTR_LEN;

    pgc->rtinfo.Txbuf->pend = pgc->rtinfo.Txbuf->ppayload + pos;
    return RET_SUCCESS;
}

/****************************************************************
        FunctionName  :  GAgent_sendmoduleinfo.
        Description      :  send module info,according to the actual situation to choose one .
        return             :  0 successful other fail.
****************************************************************/
uint32 GAgent_sendmoduleinfo( pgcontext pgc )
{
    return GAgent_sendWifiInfo(pgc);

    //return GAgent_sendGprsInfo(pgc);
}
/* #define FLASH_START_POS (WEB_ADDRESS) */
#define FLASH_START_POS (WEB_ADDRESS)
#define FLASH_END_POS (UPGRADE_ADDRESS_END)
#define MAX_FLASH_LENGTH (FLASH_END_POS - FLASH_START_POS)

#define kCRLFNewLine "\r\n"
/* flash驱动相关 */
uint32 GAgent_DeleteFirmware(int32 offset, int32 len)
{
    static int erased = 0;
    /* len = 320 * KBYTE; */
    /* int ret = hfuflash_erase_page(offset, len / HFFLASH_PAGE_SIZE + 1); */
    int ret = flash_erase_page(FLASH_START_POS + offset % HFFLASH_PAGE_SIZE, len / HFFLASH_PAGE_SIZE + 1);
    return ret;
    /* return 0; */
}


uint32 GAgent_SaveFile(uint32 offset, uint8 *buf, uint32 len)
{
    static u8 bufread[2048];
    int ret = 0;
    if((offset + len) > MAX_FLASH_LENGTH ||
        offset < 0 ||
        len < 0)
    {
        GAgent_Printf(GAGENT_ERROR, "no enough flash range");
        return -1;
    }

    if(0 == len)
    {
        return 0;
    }
    memset(bufread, 0x00, sizeof(bufread));
    /* int ret = hfuflash_write(offset, buf, len); */
    ret = flash_write(FLASH_START_POS + offset, buf, len, 0);
    /* GAgent_Printf(GAGENT_DEBUG, "write buf"); */
    /* GAgent_DumpMemory(buf, len); */
    /* if(offset > 100 * KBYTE) */
    /* { */
        GAgent_ReadOTAFile(offset, bufread, len);
        /* GAgent_Printf(GAGENT_DEBUG, "flash read buf"); */
        /* GAgent_DumpMemory(bufread, len); */
    /* } */
    if(memcmp(buf, bufread, len) != 0)
    {
        GAgent_Printf(GAGENT_DEBUG, "flash write error");
    }
    if(ret < 0)
    {
        return RET_FAILED;
    }
    return len;
}

int32 GAgent_ReadOTAFile(uint32 offset, int8 *buf, uint32 len)
{
    /* int ret = hfuflash_read(offset, (char*)buf, len); */

    if((offset + len) > MAX_FLASH_LENGTH ||
        offset < 0 ||
        len < 0)
    {
        GAgent_Printf(GAGENT_ERROR, "offset:%x, len:%d, totalsize:%x, read out of flash range",
                      offset, len, MAX_FLASH_LENGTH);
        return -1;
    }
    if(0 == len)
    {
        return 0;
    }
    int ret = flash_read(FLASH_START_POS + offset, buf, len, 0);
    if(ret < 0)
    {
        return RET_FAILED;
    }
    return len;
}

uint32 GAgent_SaveUpgradFirmware( int offset,uint8 *buf,int len )
{
    return GAgent_SaveFile( offset, buf, len );
}

uint32 md5flash(u8 *dest, uint32 offset, uint32 len)
{
    int l = 0, i = 0;
    char rbuf[2048];
    MD5_CTX md5;
    if(NULL == dest)
    {
        AS;
        GAgent_Printf(GAGENT_DEBUG, "invaild argument");
        return -1;
    }
    GAgent_MD5Init(&md5);
    while(len > 0)
    {
        l = sizeof(rbuf);
        if(len < l)
        {
            l = len;
        }
        i = GAgent_ReadOTAFile(offset, rbuf, l);
        /* GAgent_DumpMemory(rbuf, l); */
        if(i < 0)
        {
            GAgent_Printf(GAGENT_ERROR, "read flash error");
            return -1;
        }
        GAgent_MD5Update(&md5, rbuf, l);
        len -= l;
        offset += l;
    }
    GAgent_MD5Final(&md5, dest);
    return 0;
}

uint32 Http_ResGetFirmware( pgcontext pgc,int32 socketid )
{
    int ret;
    uint8 *httpReceiveBuf = NULL;
    int headlen = 0;
    volatile uint32 filesize = 0;
    char MD5[17] = {0};
    uint8 md5_calc[17] = {0};
    int offset = 0;
    uint8 *buf = NULL;
    int writelen = 0, readlen = 0;
    MD5_CTX ctx;

    char md5_flash[17] = {0};
    httpReceiveBuf = malloc(SOCKET_RECBUFFER_LEN);

    if(httpReceiveBuf == NULL)
    {
        GAgent_Printf(GAGENT_INFO, "[CLOUD]%s malloc fail!len:%d", __func__, SOCKET_RECBUFFER_LEN);
        return RET_FAILED;
    }

    ret = Http_ReadSocket( socketid, httpReceiveBuf, SOCKET_RECBUFFER_LEN );  
    if(ret <=0 ) 
    { 
        free(httpReceiveBuf);
        return RET_FAILED;
    }
    readlen = ret;
    ret = Http_Response_Code( httpReceiveBuf );
    if(200 != ret)
    {
        free(httpReceiveBuf);
        return RET_FAILED;
    }
    headlen = Http_HeadLen( httpReceiveBuf );
    pgc->rtinfo.filelen = Http_BodyLen( httpReceiveBuf );
    //pgc->rtinfo.MD5 = (char *)malloc(32+1);
    // if( pgc->rtinfo.MD5 == NULL )
    // {
    //     return RET_FAILED;
    // }
    Http_GetMD5( httpReceiveBuf,MD5,pgc->mcu.MD5 );
    Http_GetSV( httpReceiveBuf,(char *)pgc->mcu.soft_ver);
    pgc->mcu.mcu_firmware_type = Http_GetFileType(httpReceiveBuf);
    offset = 0;
    buf = httpReceiveBuf + headlen;
    writelen = readlen - headlen;
    GAgent_DeleteFirmware(0, pgc->rtinfo.filelen);
    GAgent_MD5Init(&ctx);
    do
    {
        GAgent_Printf(GAGENT_DEBUG, "http Download buf");
        if(offset < 4 * KBYTE)
        {
            /* GAgent_DumpMemory(buf, writelen); */
        }
        ret = GAgent_SaveUpgradFirmware( offset, buf, writelen );
        GAgent_Printf(GAGENT_DEBUG, "  %x:%x ", offset, writelen);
        if(ret < 0)
        {
            GAgent_Printf(GAGENT_INFO, "[CLOUD]%s OTA upgrad fail at off:0x%x", __func__, offset);
            free(httpReceiveBuf);
            return RET_FAILED;
        }
        offset += writelen;
        GAgent_MD5Update(&ctx, buf, writelen);
        writelen = pgc->rtinfo.filelen - offset;
        if(0 == writelen)
            break;
        if(writelen > SOCKET_RECBUFFER_LEN)
        {
            writelen = SOCKET_RECBUFFER_LEN;
        }
        writelen = Http_ReadSocket( socketid, httpReceiveBuf, writelen );
        if(writelen <= 0 )
        {
            GAgent_Printf(GAGENT_INFO,"[CLOUD]%s, socket recv ota file fail!recived:0x%x", __func__, offset);
            free(httpReceiveBuf);
            return RET_FAILED;
        }
        buf = httpReceiveBuf;
    }while(offset < pgc->rtinfo.filelen);
    GAgent_MD5Final(&ctx, md5_calc);

    md5flash(md5_flash, 0, pgc->rtinfo.filelen);
    GAgent_Printf(GAGENT_DEBUG, "md5 from cloud:");
    GAgent_DumpMemory(MD5, sizeof(MD5));
    GAgent_Printf(GAGENT_DEBUG, "md5 from download:");
    GAgent_DumpMemory(md5_calc, sizeof(md5_calc));
    GAgent_Printf(GAGENT_DEBUG, "md5 from flash:");
    GAgent_DumpMemory(md5_flash, sizeof(md5_flash));
    if(memcmp(md5_calc, md5_flash, 16) != 0)
    {
        GAgent_Printf(GAGENT_ERROR, "md5 flash check error");
    }
    else
    {
        GAgent_Printf(GAGENT_ERROR, "md5 flash check right");
    }

    if(memcmp(MD5, md5_calc, 16) != 0)
    {
        GAgent_Printf(GAGENT_WARNING,"[CLOUD]md5 fail!");
        free(httpReceiveBuf);
        return RET_FAILED;
    }

    GAgent_Printf(GAGENT_DEBUG, "download success");
    free(httpReceiveBuf);
    return RET_SUCCESS;
}


/* trace系列函数。用来计算系统的执行时间 */
typedef struct tracetimer_t
{
    int time;
}tracer;
tracer tracers[9] = {0};
void timetrace(int index)
{
    return ;
    int ctime = GAgent_GetDevTime_MS();
    tracers[index].time += (ctime - MS_TIMER);
    MS_TIMER = ctime;
}

void checkMem(void)
{
    int ret;
    sprintf(ota_buf, "AT+SMEM\r\n");
    ret = hfat_send_cmd(ota_buf, strlen(ota_buf), rec, sizeof(rec));
    GAgent_Printf(GAGENT_DEBUG, "%s", rec);
    return;
}

void checkError(void)
{
    int ret;
    sprintf(ota_buf, "AT+ERRN\r\n");
    ret = hfat_send_cmd(ota_buf, strlen(ota_buf), rec, sizeof(rec));
    GAgent_Printf(GAGENT_DEBUG, "%s", rec);
    return;
}

/* void ForceConnect(void) */
/* { */
/*     DRV_WiFi_StationCustomModeStart("master", "252222222"); */
/*     g_stGAgentConfigData.flag |= XPG_CFG_FLAG_CONNECTED; */
/*     DRV_GAgent_SaveConfigData(&g_stGAgentConfigData); */
/* } */
void opennetdebug();

/* Wifi事件回调 */
static int hfsys_event_callback(uint32_t event_id,void * param)  
{
    uint16 tempWiFiStatus = GAgent_DevCheckWifiStatus(0xFFFF, 1);
    switch(event_id)
    {
    case HFE_WIFI_STA_CONNECTED:
        if(connectflag == 0)
        {
            connectflag == 1;
            /* break; */
        }
        GAgent_DevCheckWifiStatus(WIFI_MODE_STATION, 1);
        GAgent_DevCheckWifiStatus(WIFI_STATION_CONNECTED, 1);
        GAgent_Printf(GAGENT_INFO,"WIFI connected! ");
        break;
    case HFE_WIFI_STA_DISCONNECTED:
        tempWiFiStatus &=~ WIFI_STATION_CONNECTED;
        GAgent_DevCheckWifiStatus(WIFI_MODE_STATION, 1);
        GAgent_DevCheckWifiStatus(WIFI_STATION_CONNECTED, 0);
        GAgent_Printf(GAGENT_DEBUG, "hfsys_event_callback wifi sta disconnected!!\n");
        break;
    case HFE_DHCP_OK:
        GAgent_Printf(GAGENT_INFO,"WIFI DHCP OK! ");
        GAgent_DevCheckWifiStatus(WIFI_STATION_CONNECTED, 1);
        GAgent_DevCheckWifiStatus(WIFI_MODE_STATION, 1);
        break;
    case HFE_SMTLK_OK:
        GAgent_Printf(GAGENT_DEBUG, "hfsys_event_callback smtlk ok!\n");
        return -1;
        break;
    case HFE_CONFIG_RELOAD:
        GAgent_Printf(GAGENT_DEBUG, "hfsys_event_callback reload!\n");
        break;
    default:
        GAgent_Printf(GAGENT_DEBUG, "hfsys_event_callback --------other\n");
        break;
    }
    /* GAgent_DevCheckWifiStatus(tempWiFiStatus); */
    return 0;
}
/* 清除配置信息 */
void clearDID()
{
    /* memset(g_stGAgentConfigData.Cloud_DId,0,24); */
    /* memset(g_stGAgentConfigData.jdinfo.product_uuid,0,32); */
    /* memset(g_stGAgentConfigData.jdinfo.feed_id,0,64); */

    /* make_rand(g_stGAgentConfigData.wifipasscode); */

    /* DRV_GAgent_SaveConfigData(&g_stGAgentConfigData); */
    /* DIdLen = 0; */
    return;
}
void clearConfig()
{
    /* memset(&g_stGAgentConfigData, 0x0, sizeof(g_stGAgentConfigData)); */
    /* DRV_GAgent_SaveConfigData(&g_stGAgentConfigData); */
}
/* 检查系统启动模式。ABI */
int8 GAgent_DRVBootConfigWiFiMode()
{
    int ret;
    char *s;
    s = atexec("WMODE");
    if(strncmp(s, "+ok=STA", sizeof("+ok=STA") - 2) == 0)
    {
        /* return WIFI_MODE_STATION; */
        return 1;
    }
    else if(strncmp(s, "+ok=AP", sizeof("+ok=AP") - 2) == 0)
    {
        /* return WIFI_MODE_AP; */
        return 2;
    }
    // 混合模式
    else if(strncmp(s, "+ok=APSTA", sizeof("+ok=APSTA") - 2) == 0)
    {
        /* return (WIFI_MODE_AP | WIFI_MODE_STATION); */
        return 3;
    }

    // 理论上不存在
    return 0;
}
/* 串口测试 */
void uarttest(pgcontext pgc)
{
    char buf[1024];
    int ret = 0;
    fd_set readfd;
    struct timeval t;
    /* int uartfd = hfuart_open(0); */
    while(1)
    {
        /* uartselect(pgc, 0, 0); */
        FD_ZERO(&readfd);
        FD_SET((int)uart_MCU, &readfd);
        t.tv_sec = 0; // 秒
        t.tv_usec = 1000;  // 微秒
        GAgent_Printf(GAGENT_INFO," -1-Time ms %d",GAgent_GetDevTime_MS() );
        ret = hfuart_select((int)uart_MCU + 1, &readfd, NULL, NULL, &t);
        GAgent_Printf(GAGENT_INFO," -2-Time ms %d",GAgent_GetDevTime_MS() );
        if(FD_ISSET((int)uart_MCU, &readfd))
        {
            ZERO(buf, 1024);
            ret = serial_read(0, buf, 1024);
            GAgent_DebugPacket(buf, ret);
        }
    }
    /* uarttest(pgContextData); */
    /* while(1) */
    /* { */
    /*     uint8 get_Mcu_InfoBuf[9]= */
    /*         { */
    /*             0xff,0xff,0x00,0x05,0x01,0x01,0x00,0x00,0x07 */
    /*         }; */
    /*     u8 buf[1024]; */
    /*     msleep(1000); */
    /*     serial_write(0, get_Mcu_InfoBuf, sizeof(get_Mcu_InfoBuf)); */
    /*     serial_read(0, buf, sizeof(buf)); */
    /* } */
    /* pgContextData->rtinfo.featrue |= (1 << GAGENT_FEATRUE_CONFIG_MODE); */

}

int32 gLocalDataLen = 0;
extern uint32 pos_start, pos_current;
void flashtest()
{
    char buf[256] = {0};
    GAgent_DeleteFirmware(0, 256);
    memset(buf, 0xAA, 256);

    GAgent_SaveUpgradFirmware(0, buf, 256);
    /* flash_erase_page_no_lock(0, 1); */
}

/* gagent入口函数 */
void gagent(void *a)
{
#ifdef EVIEL
    clearConfig();
    return;
#endif


    pgcontext pgc = NULL;
    showAPParam();
    CoreInit();

    if(hfsys_register_system_event((hfsys_event_callback_t)hfsys_event_callback) != HF_SUCCESS)
    {
        u_printf("register system event fail\n");
    }
    /* flashtest(); */
    GAgent_Init( &pgContextData);

    GAgent_dumpInfo(pgContextData);
    while(1)
    {
        int32 ret;
        /* u_printf("\n cycle\n"); */

        GAgent_Tick(pgContextData);
        GAgent_SelectFd(pgContextData,0,0);

        GAgent_Lan_Handle(pgContextData, pgContextData->rtinfo.Rxbuf , GAGENT_BUF_LEN);
        GAgent_Cloud_Handle(pgContextData, pgContextData->rtinfo.Rxbuf, GAGENT_BUF_LEN);
        GAgent_Local_Handle(pgContextData, pgContextData->rtinfo.Rxbuf, GAGENT_BUF_LEN);
    }

    return;
}
