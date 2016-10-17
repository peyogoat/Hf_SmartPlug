#include <hsf.h>
#include <hfnet.h>
#include <hfwifi.h>
#include "hfgpio.h"
#include "hftimer.h"
#include "gagent.h"
static hfuart_handle_t uart_MCU;
u32 MS_TIMER;
#define MAX_TIMER_COUNT 10
hftimer_handle_t timers;
unsigned int ledstatus = 0;
unsigned char * transcode(unsigned char *src, int size, unsigned char *buf);
static hfuart_handle_t uart_MCU;
static int OTAflag = TRUE;
static char rec[256] = {0};


// DNS解析函数
int gethostbyname(char *name, char ipaddress[], int len)
{
    int ret;
    ip_addr_t addr;
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

// LED控制函数
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

// 获取Wifi模块的MAC地址
void DRV_GAgent_GetWiFiMacAddress(char *pMacAddress)
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
    GAgent_String2MAC(buffer + 4, pMacAddress);
    return ;
}

// 显示Wifi模块的配置信息
void showAPParam(void)
{
    int ret;
    char buffer[128];
    // 当前工作模式
    ret = hfat_send_cmd("AT+WMODE\r\n", sizeof("AT+WMODE\r\n"), buffer, 128);
    GAgent_Printf(GAGENT_DEBUG, "WAPParam:%s", buffer);
    // AP模式SSID
    ret = hfat_send_cmd("AT+WAP\r\n", sizeof("AT+WAP\r\n"), buffer, 128);
    GAgent_Printf(GAGENT_DEBUG, "WAPParam:%s", buffer);
    // AP模式KEY
    ret = hfat_send_cmd("AT+WAKEY\r\n", sizeof("AT+WAKEY\r\n"), buffer, 128);
    GAgent_Printf(GAGENT_DEBUG, "WAPParam:%s", buffer);
    // 软件版本
    ret = hfat_send_cmd("AT+VER\r\n", sizeof("AT+VER\r\n"), buffer, 128);
    GAgent_Printf(GAGENT_DEBUG, "WAPParam:%s", buffer);
    // STA模式SSID
    ret = hfat_send_cmd("AT+WSSSID\r\n", sizeof("AT+WSSSID\r\n"), buffer, 128);
    GAgent_Printf(GAGENT_DEBUG, "WAPParam:%s", buffer);
    // STA模式KEY
    ret = hfat_send_cmd("AT+WSKEY\r\n", sizeof("AT+WSKEY\r\n"), buffer, 128);
    GAgent_Printf(GAGENT_DEBUG, "WAPParam:%s", buffer);
    // 设备MAC地址
    DRV_GAgent_GetWiFiMacAddress(buffer);
    GAgent_Printf(GAGENT_DEBUG, "WAPParam:%s", buffer);
    return;
}

// restart device
void DRV_GAgent_Reset(void)
{
    int ret;
    char buffer[128];
    msleep(1000);
    GAgent_Printf(GAGENT_DEBUG, "------------Reboot now");

    // 当前工作模式
    ret = hfat_send_cmd("AT+Z\r\n", sizeof("AT+Z\r\n"), buffer, 128);
    return;
}

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


int wifislq(void)
{
    int ret;
    char buffer[128];
    /* if((wifiStatus & WIFI_STATION_STATUS) != WIFI_STATION_STATUS) */
    /* { */
    /*     return 0; */
    /* } */
    // 当前工作模式
    ret = hfat_send_cmd("AT+WSLQ\r\n", sizeof("AT+WSLQ\r\n"), buffer, 128);
    GAgent_Printf(GAGENT_DEBUG, "%s", buffer);
    //+ok=Normal, 57%
    // Disconnected
    if(GAgent_strstr(buffer + 4, "Disconnected") != NULL)
    {
        return 0;
    }
    *(GAgent_strstr(buffer + 4, "%")) = '\0';
    ret = atoi(GAgent_strstr(buffer + 4, ",") + 1);
    return ret;
}

void CoreInit(void)
{
    char buffer[64];
    int ret;

    //init_gpio();
    hfnet_set_udp_broadcast_port_valid(1,65500); // 汉枫的SDK默认关闭UDP端口广播功能，需要手动打开。

#ifdef GAGENT_DEMO // 在Demo模式下，由于使用软件模拟MCU，物理串口会用来输出调试信息
    ret = hfat_send_cmd("AT+NDBGL=5,1\r\n", sizeof("AT+NDBGL=5,1\r\n"), buffer, 64);
    if(ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_DEBUG, "Faild to set uart function");
        //return ;
    }
    ret = hfat_send_cmd("AT+UART=9600,8,1,NONE,NFC,0\r\n", sizeof("AT+UART=9600,8,1,NONE,NFC,0\r\n"), buffer, 64);
    if(ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_DEBUG, "Failed to set uart0");
        //return ;
    }

#else // 在Standard模式下，物理串口用来和MCU通信，需要把调试信息输出到其他串口
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
#endif // endof #ifdef GAGENT_DEMO
    // open uart0.Save handler in uart_MCU
    uart_MCU = hfuart_open(0);
    if( NULL == uart_MCU)
    {
        GAgent_Printf(GAGENT_DEBUG, "Failed to open uart0");
        //return;
    }

    timers = hftimer_create("gagent",(int32_t)1000, true, (u32_t)GAgent_TimerHandler, GAgent_TimerHandler, 0);
    hftimer_start(timers);
    GAgent_CreateTimer(GAGENT_TIMER_PERIOD, ONE_MINUTE * 3, wifiStrengthcheck);
    OTAflag = true;
    return;
}

typedef struct scan_result
{
    int num;
    int allap;
    int result;
    u8* apbuf;
    PWIFI_SCAN_RESULT_ITEM pwifi;
}scan_res;
scan_res sr;

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
        memcpy(sr.apbuf, scan_ret, sizeof(WIFI_SCAN_RESULT_ITEM));
        sr.apbuf += sizeof(WIFI_SCAN_RESULT_ITEM);
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

void scanWifi(PWIFI_SCAN_RESULT_ITEM pwifi)
{
    if(NULL == pwifi)
    {
        sr.allap = 1;
        sr.apbuf = g_Wifihotspots;
    }
    else
    {
        sr.allap = 0;
        sr.apbuf = NULL;
        sr.pwifi = pwifi;
    }
    GAgent_Printf(GAGENT_DEBUG, "ssid,auth,encry,channel,rssi,mac\r\n");
    sr.num = hfwifi_scan(hfwifi_scan_test);
}

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

// 设置为STA模式
void DRV_WiFi_StationCustomModeStart(char *StaSsid,char *StaPass )
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
    if(sr.result != 1)
    {
        wifi.auth = 3;
        wifi.encry = 3;
    }
    else
    {
        if(wifi.auth == 4)
        {
            wifi.auth = 3;
        }
        if(wifi.encry == 4)
        {
            wifi.encry = 3;
        }
    }

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
    DRV_Led_Red(0);
    DRV_Led_Green(1);

    GAgent_CreateTimer(GAGENT_TIMER_CLOCK, 1000 * 30, GAgent_WiFiStatusCheckTimer);

    return;
}

// 成功返回1。失败返回0
// 仅仅搜索热点是否存在
// 连接热点时的参数由STA模式自己决定
// 由于sr为全局使用，故会造成某些同步问题，目前不需要处理
int DRV_WiFi_SearchAP(u8* ssid)
{
    WIFI_SCAN_RESULT_ITEM wifi;
    memset(&wifi, 0x0, sizeof(wifi));
    memcpy(wifi.ssid, StaSsid, strlen(StaSsid));
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

// Wifi模块工作在软AP模式
void DRV_WiFi_SoftAPModeStart(void)
{
    // As a cmd,it must be end with '\r\n'.reference by AT cmd guide.
    int ret;
    char buffer[64];
    char cmdbuf[64];
    // 如果获取不到MAC地址，默认为FFFF FFFF FFFF
    memset(buffer, 'F', sizeof(buffer));
    showAPParam();
    ret = hfat_send_cmd("AT+WSMAC\r\n", sizeof("AT+WSMAC\r\n"), buffer, 64);
    // buffer: +ok=123456789ABC
    // 设置C为结尾
    buffer[ 4 + 12] = '\0';
    // 传9的位置，只取最后4个字母
    sprintf(cmdbuf, "AT+WAP=11BGN,XPG-GAgent-%s,AUTO\r\n", buffer + 4 + 8);
    GAgent_Printf(GAGENT_DEBUG, "Set AP Mode:%s", cmdbuf);
    //ret = hfat_send_cmd("AT+WAP=11BGN,XPG-GAgent,AUTO\r\n", sizeof("AT+WAP=11BGN,XPG-GAgent,AUTO\r\n"), buffer, 64);
    ret = hfat_send_cmd(cmdbuf, strlen(cmdbuf), buffer, 64);
    if (ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_DEBUG, "Faild to set AP mode");
        return;
    }

    ret = hfat_send_cmd("AT+WAKEY=WPA2PSK,AES,123456789\r\n", sizeof("AT+WAKEY=WPA2PSK,AES,123456789\r\n"), buffer, 64);
    if (ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_DEBUG, "Faild to set AP mode");
        return;
    }

    ret = hfat_send_cmd("AT+WMODE=AP\r\n", sizeof("AT+WMODE=AP\r\n"), buffer, 64);
    if (ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_DEBUG, "Faild to set AP mode");
        return;
    }
                #if 0
    ret = hfat_send_cmd("AT+WADHCP=on\r\n", sizeof("AT+WADHCP=on\r\n"), buffer, 64);
    if (ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_DEBUG, "Faild to set AP mode");
        return;
    }
    sprintf(cmdbuf, "AT+WLANN=%s,%s\r\n", AP_LOCAL_IP, AP_NET_MASK);
    ret = hfat_send_cmd(cmdbuf, strlen(cmdbuf), buffer, 64);
    if (ret != HF_SUCCESS)
    {
        GAgent_Printf(GAGENT_DEBUG, "Faild to set AP mode");
        return;
    }
                #endif

    GAgent_Printf(GAGENT_DEBUG, "Success to set AP mode, retval = %d", ret);
    DRV_Led_Red(1);
    DRV_Led_Green(0);

    return;
}

// 获取Wifi状态
int DRV_GAgent_GetWiFiStatus()
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

// 获取Wifi工作模式
int DRV_GAgent_GetWifiMode()
{
    int ret;
    char buffer[128];
    ret = hfat_send_cmd("AT+WMODE\r\n", sizeof("AT+WMODE\r\n"), buffer, 128);
    if(strncmp(buffer, "+ok=STA", sizeof("+ok=STA") - 2) == 0)
    {
        return WIFI_MODE_STATION;
    }
    else if(strncmp(buffer, "+ok=AP", sizeof("+ok=AP") - 2) == 0)
    {
        return WIFI_MODE_AP;
    }
    // 混合模式
    else if(strncmp(buffer, "+ok=APSTA", sizeof("+ok=APSTA") - 2) == 0)
    {
        return (WIFI_MODE_AP | WIFI_MODE_STATION);
    }

    // 理论上不存在
    return 0;
}


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

int serial_open(char *comport, int bandrate,int nBits,char nEvent,int nStop )
{
    return uart_MCU;
}

int serial_read( int serial_fd, unsigned char *buf,int buflen )
{
    int ret;
    u8 buf[0x4] = {0x11, 0x22, 0x33, 0x44};

    fd_set readfds, exceptfds;

    FD_ZERO(&readfds);
    FD_ZERO(&exceptfds);
    FD_SET((int)uart_MCU, &readfds);
    struct timeval_t t;
    t.tv_sec = 0;
    t.tv_usec = 0;

    ret = hfuart_select(((int)uart_MCU + 1), &readfds, NULL, &exceptfds, &t);
    if(ret < 0)
    {
        return -1;
    }
    if( FD_ISSET((int)uart_MCU, &readfds))
    {
        ret = hfuart_recv(uart_MCU, data, 1024, 1000);
        if(ret <= 0)
        {
            return 0;
        }
        //ledswitchonoff();
        //hfuart_send(uart_MCU, data, ret, 100);
        //hfuart_send(uart_MCU, buf, 4, 100);
    }

    return ret;


}

int serial_write( int serial_fd,unsigned char *buf,int buflen )
{
    hfuart_send(uart_MCU, (char*)data, datalen, 1000);
    return buflen;
}
