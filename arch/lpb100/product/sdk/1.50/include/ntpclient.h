/*
 *  Copyright 2012-2016, by L.B.
 *  All Rights Reserved.
 */

/** ntpclient.h: Functions for NTP
 */
 
#ifndef _NTPCLIENT_H_
#define _NTPCLIENT_H_

struct ntp_timestamp
{
	unsigned int secondsFrom1900;//1900��1��1��0ʱ0������������
	unsigned int fraction;//΢���4294.967296(=2^32/10^6)��
};

struct ntp_header
{
	union
	{
	   struct 
	   {
		    char local_precision;//��ʾ����ʱ�Ӿ���Ϊ2^local_precision�롣local_precisionͨ��Ϊ������
		    char poll_intervals;//��ʾ���Լ��Ϊ2^poll_intervals�롣
		    unsigned char stratum;//NTP�������׼���0��ʾ��ָ����1��ʾУ׼��ԭ���ӡ�Ӧ��Ϊ0��
		    unsigned char mode : 3;//ͨ��ģʽ��Ӧ��Ϊ3����ʾ��client��
		    unsigned char version_number : 3;//NTPЭ��汾�š�Ӧ��Ϊ3��
		    unsigned char leap_indicator : 2;//����ָʾ��һ����0��
	   }s;
	   int noname;
	}u;
	int root_delay;//�����ɸ���2^16��ʾһ�롣���庬��μ�RFC1305��
	int root_dispersion;//ֻ��Ϊ����2^16��ʾһ�롣���庬��μ�RFC1305��
	int reference_clock_identifier;//���庬��μ�RFC1305��һ����0��
};//û�д���Ļ���ntp_header�Ĵ�СӦ��Ϊ16�ֽڡ�

typedef struct
{
	struct ntp_header header;
	//�����ĸ�ʱ���Ϊ����ʱ�䡣��������ʱ��λ�õġ�
	struct ntp_timestamp reference;//���庬��μ�RFC1305��һ����0��
	struct ntp_timestamp originate;//�ϴη���ʱ��
	struct ntp_timestamp receive;//����ʱ��
	struct ntp_timestamp transmit;//����ʱ��
}ntp_packet;//û�д���Ļ���ntp_header�Ĵ�СӦ��Ϊ48�ֽڡ�

int NTP_update_time(void);

#endif/*_NTPCLIENT_H_*/
