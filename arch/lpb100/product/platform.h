#ifndef __PRODUCT_H__
#define __PRODUCT_H__

#include <hsf.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <hfthread.h>

#define malloc(a)   hfmem_malloc(a)
/* #define select hfuart_select */

#define free(a)     hfmem_free(a)
#define NULL 0x00
#include "lwip/sockets.h"
#define ZERO(name,size) memset(name,0x0,size)
#define GAGENT_WITH_HF                  1

#define GAGENT_CONNECT_WITH_ETHERNET    0
#define GAGENT_FEATURE_SENDAPSTATUS2MCU 0
#define GAGENT_FEATURE_JINGDONGDISCOVER 1
#define GAGENT_FEATURE_OTA              1
#define GAGENT_FEATURE_HTTP_OTA	        1

#define timeval_t timeval
#define sockaddr_t sockaddr_in

#define SOCK_DGRM SOCK_DGRAM

#define CONFIG_OFFSET       128


#define WIFI_HARDVER    "00HFLPB1"

#ifdef GAGENT_PROTOCOL_V3
#define WIFI_SOFTVAR    "03020001" /* update at 150914 from 03020000 */
#endif
#ifdef GAGENT_PROTOCOL_V4
#define WIFI_SOFTVAR    "04020006" /* update at 151021 from 04020004 */
#endif



typedef struct _JD_INFO_
{
    char product_uuid[32];
    char feed_id[64];
    char access_key[64];
    char ischanged;
    char tobeuploaded;
}old_jd_info;
/* XPG GAgent Global Config data*/
typedef struct _GAGENT_CONFIG
{
    unsigned int magicNumber; //0x12345678
    unsigned int flag;
    char wifipasscode[16]; /* gagent passcode */
    char wifi_ssid[32]; /* WiFi AP SSID */
    char wifi_key[32]; /* AP key */
    char FirmwareId[8];  /* Firmware ID,identity application version */
    char Cloud_DId[24]; /* Device, generate by server, unique for devices */
    char FirmwareVerLen[2];
    char FirmwareVer[32];
    old_jd_info jdinfo;
    char old_did[24];
    char old_wifipasscode[16];
    char old_productkey[33];
    char m2m_ip[17];
    char api_ip[17];
    char airkiss_value; //airkiss BC value  to app.
}GAgent_OldCONFIG_S;


int serial_open(char  *comport, int bandrate,int nBits,char nEvent,int nStop );
int serial_write( int serial_fd,unsigned char *buf,int buflen );
int serial_read( int serial_fd, unsigned char *buf,int buflen );


#endif //__PRODUCT_H__
