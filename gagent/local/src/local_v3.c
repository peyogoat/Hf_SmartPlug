#include "gagent.h"
#include "utils.h"
#include "lan.h"
#include "local.h"
#include "hal_receive.h"
#include "cloud.h"
#include "bufman.h"
#define BUFMAN_CAP 4096
#define MCU_CTRL_CMD_V3 0x0301
#define WIFI_MCU_INFO_CMD_V3 0x0306
#define MCU_INFO_CMD_ACK_V3 0x0307
#define MCU_REPORT_V3 0x0305
#define WIFI_STATUS2MCU_V3 0x030C
#define WIFI_CMD_FROM_CLIENT_V3 0x0301
#define WIFI_CMD_FROM_CLOUD_V3 0x0302
#define WIFI_MCU_HEARTBEAT_V3 0x030E
#define WIFI_MCU_HEARTBEAT_ACK_V3 0x030F
BufMan *pbm=NULL;
int32 GAgent_Local_WaitDataReady(pgcontext pgc, uint32 timeoutS, uint32 timeoutMs)
{
    int32 maxFd;
    int32 ret=0;

    FD_ZERO( &(pgc->rtinfo.readfd) );

    if( pgc->rtinfo.local.uart_fd>0 )
    {
        FD_SET( pgc->rtinfo.local.uart_fd,&(pgc->rtinfo.readfd));
        maxFd = pgc->rtinfo.local.uart_fd + 1;
        ret = GAgent_select( maxFd, &(pgc->rtinfo.readfd), NULL, NULL,
           timeoutS,timeoutMs*1000 );
    }

    return ret;
}


int32 GAgent_Local_IsDataValid(pgcontext pgc)
{
    // if(FD_ISSET( pgc->rtinfo.local.uart_fd, &(pgc->rtinfo.local.readfd)))
    // {
    //     return RET_SUCCESS;
    // }
    // else
    // {
    //     return RET_FAILED;
    // }
}


int GAgent_Local_CheckDataIn(pgcontext pgc)
{
    /* 此处，不能使用全局FdSet，会导致被清除，另外，两个接口应该合并 */
    if(GAgent_Local_WaitDataReady(pgc, 0, 0) > 0 &&
       GAgent_Local_IsDataValid(pgc) == RET_SUCCESS)
    {
        return RET_SUCCESS;
    }
    return RET_FAILED;
}

/* 取数据包 */
/* 每次调用都会从底层重写抓包，即自动刷新 */
/* 另：使用Rxbuf作为串口读取缓存，会破坏原有数据 */
int32 GAgent_Local_GetPacket( pgcontext pgc, ppacket Rxbuf )
{
    int len = 0;
    /* 检测串口是否有数据 */
    if( FD_ISSET( pgc->rtinfo.local.uart_fd, &(pgc->rtinfo.readfd)) )
    {
        /* 将数据放入缓冲区 */
        len = serial_read(pgc->rtinfo.local.uart_fd, Rxbuf->phead, Rxbuf->totalcap);
        if(len > 0)
        {
            GAgent_Printf( GAGENT_DEBUG,"local data in len =%d",len );
            mcuData_push(pbm, Rxbuf->phead, len);
        }
    }
    /* 此接口仅面向缓存，而非包 */
    len = mcuData_get(pbm, Rxbuf->phead);
    /* 在收到一包数据后，要进一步解析为包格式 */
    if(len > 0)
    {
        Rxbuf->type = LOCAL_DATA_IN;
        setlocalpacket(Rxbuf, len);
    }
    return len;
}

int Local_AddClient(int tcpclient, ppacket pp)
{
    u32 Sid = 0;
    Sid = htonl(tcpclient);

    pp->ppayload -= sizeof(Sid);
    memcpy(pp->ppayload, &Sid, sizeof(Sid));
    return sizeof(Sid);
}

int Local_AddP0Head( int tcpclient,int P0Len, u8 buf[], uint16 cmd)
{
    /* the length of uart head is 12; */
    int i;
    int UartLen=0;
    /* char Sid[4]; */
    u32 Sid;
    u8 *pSid=NULL;
    UartLen = P0Len + 6;
    /* encodeInt32(tcpclient,Sid); */
    Sid = htonl( tcpclient );
    pSid = (u8*)&Sid;

    /* HUart.pro[0] = 0x00; */
    /* HUart.pro[1] = 0x00; */
    /* HUart.pro[2] = 0x00; */
    /* HUart.pro[3] = 0x03; */
    buf[0] = 0x00;
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0x03;

    /* HUart.len[0] = (char)(UartLen>>8); */
    /* HUart.len[1] = (char)(UartLen); */
    buf[4] = (char)(UartLen>>8);
    buf[5] = (char)(UartLen);

    /* HUart.cmd[0] = 0x03; */
    /* HUart.cmd[1] = 0x01; */
    buf[6] = (cmd >> 8) & 0xFF;
    buf[7] = cmd & 0xFF;

    for( i=0;i<4;i++ )
    {
        buf[8 + i] = pSid[i];
    }

    return 12;
}
pfMasterMCU_ReciveData PF_ReceiveDataformMCU = NULL;
pfMasertMCU_SendData   PF_SendData2MCU = NULL;
/* 注册GAgent接收local  数据函数 */
void GAgent_RegisterReceiveDataHook(pfMasterMCU_ReciveData fun)
{
    PF_ReceiveDataformMCU = fun;
    return;
}
/* 注册GAgent发送local  数据函数 */
void GAgent_RegisterSendDataHook(pfMasertMCU_SendData fun)
{
    PF_SendData2MCU = fun;
    return;
}


uint32 Local_SendData( int32 fd,uint8 *pData, int32 bufferMaxLen )
{
    int32 i=0;
    if( PF_SendData2MCU!=NULL )
    {
        GAgent_Printf( GAGENT_DUMP,"local send len = %d:\r\n",bufferMaxLen );
        for( i=0;i<bufferMaxLen;i++ )
            GAgent_Printf( GAGENT_DUMP," %02x",pData[i]);
        GAgent_Printf( GAGENT_DUMP,"\r\n");
        PF_SendData2MCU( fd,pData,bufferMaxLen );
    }
    return 0;
}

/* 在给定p0（payload~pend）前添加对应命令的head */
void Local_AddCommonHead(ppacket pp, uint16 cmd)
{
    u16 len = 0;
    u8 common[] = {0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x00, 0x00};
    pp->phead = pp->ppayload - 8;
    len = pp->pend - pp->ppayload + 2;
    /* len = htons(len); */
    common[4] = len >> 8 & 0xFF;
    common[5] = len & 0xFF;
    /* cmd = htons(cmd); */
    common[6] = cmd >> 8 & 0xFF;
    common[7] = cmd & 0xFF;
    memcpy(pp->phead, common, sizeof(common));
}

/* 转换v3和v4版命令 */
u16 translatecmd423(u8 cmd)
{
    u16 s;
    switch(cmd)
    {
    case WIFI_STATUS2MCU:
        s = WIFI_STATUS2MCU_V3;
        break;
    case MCU_CTRL_CMD:
        s = WIFI_CMD_FROM_CLIENT_V3;
        break;
    case WIFI_PING2MCU:
        s = WIFI_MCU_HEARTBEAT_V3;
    default:
        s = -1;
        break;
    }
    return s;
}

u8 translatecmd324(u16 cmd)
{
    u8 c;
    switch(cmd)
    {
    case 0:
        break;
    default:
        break;
    }
    return c;
}

/* 按长度填充packet */
int putinpacket(ppacket pp, u8 *buf, int len)
{
    if(len > pp->bufcap)
    {
        return -1;
    }
    memcpy(pp->ppayload, buf, len);
    pp->pend = pp->ppayload + len;
    return len;
}

/* 发送数据到设备 */
/* pTxBuf，有效数据P0在phead位置 */
int32 GAgent_LocalDataWriteP0( pgcontext pgc,int32 fd,ppacket pp,uint8 cmd )
{
    uint16 c = translatecmd423(cmd);
    if(-1 == c)
    {
        /* 不支持的命令。MCU升级 */
        return -1;
    }
    /* 在发送报文时，模糊掉大小循环的区别 */
    /* 接收到的报文，全部发送到对端 */
    /* int len = Local_AddP0Head(0, pp->pend - pp->ppayload, pp->ppayload - 12, cmd); */
    if(c == MCU_CTRL_CMD_V3)
    {
        Local_AddClient(0, pp);
    }
    Local_AddCommonHead(pp, c);
    Local_SendData( fd, pp->phead, pp->pend - pp->phead);
    return 0;
}

/* 获取设备信息 */
void Local_GetInfo(pgcontext pgc)
{
    u8 GetMcuInfoCMD[8]={0X00,0X00,0X00,0X03,0X00,0X02,0X03,0X06};
    int ctime = 0, etime = 0;
    int retry = 0;
    while(strlen(pgc->mcu.product_key)==0)
    {
        GAgent_SelectFd( pgc,1,0 );
        if(etime < GAgent_GetDevTime_MS())
        {
            ctime = GAgent_GetDevTime_MS();
            etime = ctime + 200;
            Local_SendData(pgc->rtinfo.local.uart_fd, GetMcuInfoCMD,8);
            retry++;
            GAgent_Local_WaitDataReady(pgc, 0, 10);
        }
        /* UART_handlePacket(); */
        GAgent_Local_Handle(pgc, pgc->rtinfo.Rxbuf, GAGENT_BUF_LEN);

        if(retry > 20 * 3)
        {
            GAgent_DevReset();
        }
    }

    GAgent_Printf(GAGENT_INFO,"GAgent_get hard_ver: %s.",pgc->mcu.hard_ver);
    GAgent_Printf(GAGENT_INFO,"GAgent_get soft_ver: %s.",pgc->mcu.soft_ver);
    GAgent_Printf(GAGENT_INFO,"GAgent_get p0_ver: %s.",pgc->mcu.p0_ver);
    GAgent_Printf(GAGENT_INFO,"GAgent_get protocal_ver: %s.",pgc->mcu.protocol_ver);
    GAgent_Printf(GAGENT_INFO,"GAgent_get product_key: %s.",pgc->mcu.product_key);
}


void GAgent_Reset( pgcontext pgc )
{
    GAgent_Clean_Config(pgc);
    GAgent_DevReset();
}

/* 清除配置信息 */
void GAgent_Clean_Config( pgcontext pgc )
{
    memset( pgc->gc.old_did,0,DID_LEN);
    memset( pgc->gc.old_wifipasscode,0,PASSCODE_MAXLEN + 1);

    memcpy( pgc->gc.old_did,pgc->gc.DID,DID_LEN );
    memcpy( pgc->gc.old_wifipasscode,pgc->gc.wifipasscode,PASSCODE_MAXLEN + 1 );
    GAgent_Printf(GAGENT_INFO,"Reset GAgent and goto Disable Device !");
    Cloud_ReqDisable( pgc );
    GAgent_SetCloudConfigStatus( pgc,CLOUD_RES_DISABLE_DID );

    memset( pgc->gc.wifipasscode,0,PASSCODE_MAXLEN + 1);
    memset( pgc->gc.wifi_ssid,0,SSID_LEN_MAX + 1 );
    memset( pgc->gc.wifi_key,0, WIFIKEY_LEN_MAX + 1 );
    memset( pgc->gc.DID,0,DID_LEN);

    memset( (uint8*)&(pgc->gc.cloud3info),0,sizeof( GAgent3Cloud ) );

    memset( pgc->gc.GServer_ip,0,IP_LEN_MAX + 1);
    memset( pgc->gc.m2m_ip,0,IP_LEN_MAX + 1);
    make_rand(pgc->gc.wifipasscode);

    pgc->gc.flag &=~XPG_CFG_FLAG_CONNECTED;
    GAgent_DevSaveConfigData( &(pgc->gc) );
}

/* 发送wifi状态 */
void GAgent_LocalSendGAgentstatus(pgcontext pgc,uint32 dTime_s )
{
    uint16 GAgentStatus = 0; 
    uint16 LastGAgentStatus = 0; 
    if( (pgc->rtinfo.GAgentStatus) != (pgc->rtinfo.lastGAgentStatus) )
    {
          GAgentStatus = pgc->rtinfo.GAgentStatus&LOCAL_GAGENTSTATUS_MASK;
          LastGAgentStatus = pgc->rtinfo.lastGAgentStatus&LOCAL_GAGENTSTATUS_MASK;
          GAgent_Printf( GAGENT_INFO,"GAgentStatus change, lastGAgentStatus=0x%04x, newGAgentStatus=0x%04x", LastGAgentStatus, GAgentStatus);
          pgc->rtinfo.lastGAgentStatus = pgc->rtinfo.GAgentStatus&LOCAL_GAGENTSTATUS_MASK;
          GAgentStatus = htons(GAgentStatus);
          memcpy((pgc->rtinfo.Rxbuf->ppayload), (uint8 *)&GAgentStatus, 2);
          pgc->rtinfo.Rxbuf->pend =  (pgc->rtinfo.Rxbuf->ppayload)+2;
          pgc->rtinfo.updatestatusinterval =  0; 
          //GAgent_Printf(GAGENT_CRITICAL,"updateGagentstatusLast time=%d", (pgc->rtinfo.send2LocalLastTime));
         GAgent_LocalDataWriteP0( pgc,pgc->rtinfo.local.uart_fd, (pgc->rtinfo.Rxbuf), WIFI_STATUS2MCU );
    }

    pgc->rtinfo.updatestatusinterval+= dTime_s;

    if( (pgc->rtinfo.updatestatusinterval)  > LOCAL_GAGENTSTATUS_INTERVAL)
    {
        pgc->rtinfo.updatestatusinterval = 0;
        GAgentStatus = pgc->rtinfo.GAgentStatus&LOCAL_GAGENTSTATUS_MASK;
        GAgentStatus = htons(GAgentStatus);
        memcpy((pgc->rtinfo.Rxbuf->ppayload), (uint8 *)&GAgentStatus, 2);
        pgc->rtinfo.Rxbuf->pend =  (pgc->rtinfo.Rxbuf->ppayload)+2;
        GAgent_LocalDataWriteP0( pgc,pgc->rtinfo.local.uart_fd, (pgc->rtinfo.Rxbuf), WIFI_STATUS2MCU );
    }
    return;
}

int setlocalpacket(ppacket pp, int length)
{
    u16 len = 0;
    u8 *buf = pp->phead;
    if((pp->type & LOCAL_DATA_IN) == LOCAL_DATA_IN)
    {
        len = buf[4] << 8 & 0xFF + buf[5] & 0xFF;
        pp->ppayload = pp->phead + len;
        pp->pend = pp->ppayload + length;
        return length;
    }
    return -1;
}

/* 初始化local模块 */
int filter_MCUPacket_v3(BufMan* bm, u8* buf);
void GAgent_LocalInit( pgcontext pgc )
{
    int totalCap = BUF_LEN + BUF_HEADLEN;
    int bufCap = BUF_LEN;
    GAgent_LocalDataIOInit( pgc );
    pbm = (pBufMan)malloc(sizeof(BufMan));
    initBufMan(pbm, filter_MCUPacket_v3, BUFMAN_CAP);

    // pgc->mcu.Txbuf = (ppacket)malloc( sizeof(packet) );
    // pgc->mcu.Txbuf->allbuf = (uint8 *)malloc(totalCap);
    // pgc->mcu.Txbuf->totalcap = totalCap;
    // pgc->mcu.Txbuf->bufcap = bufCap;
    // resetPacket( pgc->mcu.Txbuf );

    pgc->mcu.isBusy = 0;
    GAgent_RegisterReceiveDataHook( GAgent_Local_GetPacket);
    GAgent_RegisterSendDataHook( serial_write );

    Local_GetInfo( pgc );
}

int Local_sendconfigresult(pgcontext pgc)
{
    u8 buf[] = {0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x03, 0x09, 0x00};
    Local_SendData(pgc->rtinfo.local.uart_fd, buf, sizeof(buf));
    return sizeof(buf);
}

int Local_DoMcuInfoPacket(pgcontext pgc, const u8* buf)
{
    /* header(4b), len(2b), cmd(2b), pklen(2b), pk(nb), bverlen(2b), bver */
    int productKeyLen=0;
    unsigned short busiProtocolVerLen=0;

    productKeyLen = buf[9];

    busiProtocolVerLen =
        buf[9+productKeyLen+1]*256 +
        buf[9+productKeyLen+2];

    /* strcpy((char *)pgc->mcu.hard_ver,mcu_hard_ver); */
    /* strcpy((char *)pgc->mcu.soft_ver,mcu_soft_ver); */
    /* strcpy((char *)pgc->mcu.p0_ver,mcu_p0_ver); */
    /* strcpy((char *)pgc->mcu.protocol_ver,mcu_protocol_ver); */
    /* strcpy((char *)pgc->mcu.product_key,mcu_product_key); */

    memcpy(pgc->mcu.product_key, buf+10, productKeyLen);
    memcpy(pgc->mcu.protocol_ver, buf+(9+productKeyLen+2+1), busiProtocolVerLen);

    if(strcmp( (int8 *)pgc->mcu.product_key,pgc->gc.old_productkey )!=0)
    {
        GAgent_UpdateInfo(pgc,pgc->mcu.product_key);
        GAgent_Printf( GAGENT_INFO,"2 MCU old product_key:%s.",pgc->gc.old_productkey);
    }

    return productKeyLen;
}

int Local_HandlePacket(pgcontext pgc, ppacket pp, int bufferlen)
{
    u16 *pcmd=NULL;
    short UCMD = 0, tmp = 0;
    int ret = 0;
    int ProdLen;
    u8 testmodeack[] = {0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x03, 0x11};
    u8 *buffer = pp->phead;
    int headlen = 0;
    pcmd = (u16*)&buffer[6];

    UCMD = ntohs(*pcmd);

    GAgent_Printf(GAGENT_INFO,"DO MCU PACKET CMD=%02X[len: %d]",UCMD, bufferlen);
    /* 同样模糊数据方向，发送给所有对端 */
    /* 根据不同的命令，head的长度需要分别计算 */
    pgc->rtinfo.local.timeoutCnt=0;
    switch(UCMD)
    {
    case 0x0303://send mcu cmd to TCP client
        /* MCU_SendPacket2Phone( buffer,1); */
        /* break; */
        /* version(4b), len(2b), cmd(2b, 0x0303), socketid(4b), p0 */
        headlen = 12;
        goto senddata;
    case 0x0304 ://send pload to cloud
        /* MCU_SendPacket2Cloud(buffer, bufferlen ); */
        /* break; */
        /* version(4b), len(2b), cmd(2b, 0x0304), phoneclientidlen(2b), phoneclientid(nb), p0 */
        /* 计算客户端id长度 */
        tmp = buffer[8] << 8 + buffer[9];
        headlen = 10 + tmp;
        goto senddata;
    case 0x0305: //send pload to all TCP clients
        /* MCU_SendPacket2Phone(buffer,0); */
        /* 需要增加一个WAN发送接口 */
        /* MCU_SendPublishPacket(buffer, bufferlen); */
        /* 发送数据时，做为全向发送。仅需要关注p0起始位置和长度 */
        /* 起始位置，由headlen给出；长度则由pend计算 */
        /* 若是接收的接口可以使用packet类型，则可以直接调用，不需要做额外转换 */
        headlen = 8;
    senddata:
        /* ParsePacket(); */
        /* TransPacket(); */
        pp->ppayload = pp->phead + headlen;
        setChannelAttrs(pgc, NULL, NULL, 1);
        pp->type |= LAN_TCP_DATA_OUT;
        GAgent_Lan_SendTcpData(pgc, pp);
        /* TransPacket(); */
        pp->type |= CLOUD_DATA_OUT;
        GAgent_Cloud_SendData(pgc, pp, pp->pend - pp->phead);
        break;
    case 0x0307: //had got the productKey  TemProductKey
        if(strlen(pgc->mcu.product_key) > 0)
        {
            break;
        }
        Local_sendconfigresult(pgc);
        ProdLen = Local_DoMcuInfoPacket(pgc, buffer);
        break;
    case 0x0308: //Into Easylink mode
        GAgent_Config(2, pgc);
        break;
    case 0x030a:
        pgc->mcu.passcodeTimeout = pgc->mcu.passcodeEnableTime;
        GAgent_SetWiFiStatus( pgc,WIFI_MODE_BINDING,1 );
        GAgent_Printf(GAGENT_DEBUG,"Passcode Enable 10s.");
        break;
    case 0x030b:
        GAgent_Clean_Config(pgc);
        /* GAgent_Config(1, pgc); */
        pgc->gc.flag |= XPG_CFG_FLAG_CONFIG_AP;
        GAgent_DevSaveConfigData( &(pgc->gc) );
        GAgent_DRV_WiFi_SoftAPModeStart( pgc->minfo.ap_name,AP_PASSWORD, 0xFFFF);
        GAgent_DevReset();
        break;
    case 0x0310:
        u8 testmodeack[] = {0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x03, 0x11};
        Local_SendData(pgc->rtinfo.local.uart_fd, testmodeack, 8);
        GAgent_EnterTest( pgc );
        break;
    case WIFI_MCU_HEARTBEAT_ACK_V3:
         pgc->rtinfo.local.timeoutCnt=0;
          GAgent_Printf(GAGENT_CRITICAL,"local ping akc...");
        break;
    default :
        break;
    }

    return ret;
}
void GAgent_BigDataTick( pgcontext pgc )
{
    
}
void GAgent_LocalTick( pgcontext pgc,uint32 dTime_s )
{

    u8 heartbeatcmd[] = {0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x03, 0x0E};
    pgc->rtinfo.local.oneShotTimeout+=dTime_s;
    if( pgc->rtinfo.local.oneShotTimeout >= MCU_HEARTBEAT )
    {
        if( pgc->rtinfo.local.timeoutCnt> 3 )
        {
            GAgent_Printf(GAGENT_CRITICAL,"Local heartbeat time out ...");
            /* GAgent_DevReset(); */
        }
        // else
        {
            pgc->rtinfo.local.oneShotTimeout = 0;
            pgc->rtinfo.local.timeoutCnt++;
            GAgent_Printf(GAGENT_CRITICAL,"Local ping...");
            /* GAgent_LocalDataWriteP0( pgc,pgc->rtinfo.local.uart_fd, pgc->mcu.Txbuf,WIFI_PING2MCU ); */
            Local_SendData(pgc->rtinfo.local.uart_fd, heartbeatcmd, sizeof(heartbeatcmd));
        }
    }
    return;
}

/* 数据处理 */
void GAgent_Local_Handle( pgcontext pgc,ppacket Rxbuf,int32 length )
{
    int len = 0;
    while((len = GAgent_Local_GetPacket(pgc, Rxbuf)) > 0)
    {
        Local_HandlePacket(pgc, Rxbuf, len);
    }
    return;
}
