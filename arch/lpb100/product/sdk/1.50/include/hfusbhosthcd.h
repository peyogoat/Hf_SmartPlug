/**
 *****************************************************************************
 * @file     hfusbhosthcd.h
 * @author   Jim
 * @version  V1.0.0
 * @date     27-3-2014
 * @brief    usb host module driver interface
 *****************************************************************************
 * @attention  
 *
 * <h2><center>&copy; COPYRIGHT 2014 Hi-flying </center></h2>
 */
 
#ifndef _HF_HOST_HCD_H_
#define	_HF_HOST_HCD_H_

typedef	void(*HFFPCALLBACK)(void);


/**
 * Max packet size. Fixed, user should not modify.
 */
#define	HF_HOST_FS_CONTROL_MPS		64		
#define	HF_HOST_FS_INT_IN_MPS		64		
#define	HF_HOST_FS_BULK_IN_MPS		64		
#define	HF_HOST_FS_BULK_OUT_MPS	64
#define	HF_HOST_FS_ISO_OUT_MPS		192	
#define	HF_HOST_FS_ISO_IN_MPS		192		


/**
 * Endpoint number. Fixed, user should not modify.
 * ��ΪHOSTʱ�������Ķ˵�Ÿ�����˵�ſ��Բ���ͬ
 */
#define	HF_HOST_CONTROL_EP			0
#define	HF_HOST_BULK_IN_EP			1
#define	HF_HOST_BULK_OUT_EP		2
#define	HF_HOST_INT_IN_EP			3
#define	HF_HOST_ISO_OUT_EP			4
#define	HF_HOST_ISO_IN_EP			5


/**
 * pipe type
 */
#define HF_PIPE_TYPE_CONTROL		0
#define HF_PIPE_TYPE_ISO_IN		1
#define HF_PIPE_TYPE_ISO_OUT		2
#define HF_PIPE_TYPE_INT_IN		3
#define HF_PIPE_TYPE_INT_OUT		4
#define HF_PIPE_TYPE_BULK_IN		5
#define HF_PIPE_TYPE_BULK_OUT		6


/**
 * define OTG endpoint descriptor, used for configuration endpoint information
 */
typedef struct _HFU_PIPE_INFO
{
	uint8_t	EndpointNum	;	//endpoint number
	uint8_t	PipeType;		//endpoint type: ctrl, bulk, interrupt, isochronous
	uint8_t	MaxPacketSize;	//max packet size

} HFU_PIPE_INFO;


/**
 * @brief  usb host init
 * @param  NONE
 * @return 1-�ɹ���0-ʧ��
 */
int hfusb_host_open(void);

/**
 * @brief  ѡ��ǰUSB�˿�
 * @param  NONE
 * @return NONE
 */
void hfusb_set_current_port(uint8_t port_num);

/**
 * @brief  ���USB1�˿����Ƿ���һ��U���豸����
 * @param  NONE
 * @return 1-��U���豸���ӣ�0-��U���豸����
 */
int hfusb_host1_is_link(void);

/**
 * @brief  ���USB2�˿����Ƿ���һ��U���豸����
 * @param  NONE
 * @return 1-��U���豸���ӣ�0-��U���豸����
 */
int hfusb_host2_is_link(void);

/**
 * @brief  ��鵱ǰѡ���USB�˿����Ƿ���һ��U���豸����
 * @param  NONE
 * @return 1-��U���豸���ӣ�0-��U���豸����
 */
int hfusb_host_is_link(void);

/**
 * @brief  usb���߸�λ
 * @param  NONE
 * @return NONE
 */
void hfusb_host_port_reset(void);

/**
 * @brief  �ӿ��ƶ˵����һ�ο����������
 * @param  SetupPacket SETUP��ָ��
 * @param  Buf OUT���ݻ�����ָ��
 * @param  Len OUT���ݳ���
 * @return 1-�ɹ���0-ʧ��
 */
int hfusb_host_control_out_transfer(uint8_t* SetupPacket, uint8_t* Buf, uint16_t Len);

/**
 * @brief  �ӿ��ƶ˵����һ�ο������봫��
 * @param  SetupPacket SETUP��ָ��
 * @param  Buf IN���ݻ�����ָ��
 * @param  Len IN���ݳ���
 * @return 1-�ɹ���0-ʧ��
 */
int hfusb_host_control_in_transfer(uint8_t* SetupPacket, uint8_t* Buf, uint16_t Len);

/**
 * @brief  ����һ�����ݰ�
 * @param  Pipe pipeָ��
 * @param  Buf �������ݻ�����ָ��
 * @param  Len �������ݳ���
 * @param  TimeOut �������ݳ�ʱʱ�䣬��λΪ����
 * @return ʵ�ʽ��յ������ݳ���
 */
uint16_t hfusb_host_rcv_packet(HFU_PIPE_INFO* Pipe, uint8_t* Buf, uint16_t Len, uint16_t TimeOut);

/**
 * @brief  ����һ�����ݰ�
 * @param  Pipe pipeָ��
 * @param  Buf �������ݻ�����ָ��
 * @param  Len �������ݳ���
 * @param  TimeOut �������ݳ�ʱʱ�䣬��λΪ����
 * @return 1-�ɹ���0-ʧ��
 */
int hfusb_host_send_packet(HFU_PIPE_INFO* Pipe, uint8_t* Buf, uint16_t Len, uint16_t TimeOut);

/**
 * @brief  ����ĳ���˵㷢��һ�����ݰ�
 * @brief  �첽��ʽ�������󣬲��ȴ���������
 * @brief  Ӳ�����Զ����Ϸ���OUT���ƣ�ֱ��������һ�����ݰ�
 * @param  Pipe pipeָ��
 * @param  Buf �������ݻ�����ָ��
 * @param  Len �������ݳ���
 * @return NONE
 */
void hfusb_host_start_send_packet(HFU_PIPE_INFO* Pipe, uint8_t* Buf, uint16_t Len);

/**
 * @brief  �жϷ����Ƿ����
 * @param  Pipe pipeָ��
 * @return 1-�ɹ���0-ʧ��
 */
int hfusb_host_is_send_ok(HFU_PIPE_INFO* Pipe);

/**
 * @brief  ����ĳ���˵����һ�����ݰ�
 * @brief  �첽��ʽ�������󣬲��ȴ���������
 * @brief  Ӳ�����Զ����Ϸ���IN���ƣ�ֱ�����յ�һ�����ݰ�
 * @param  Pipe pipeָ��
 * @return NONE
 */
void hfusb_host_start_rcv_packet(HFU_PIPE_INFO* Pipe);

/**
 * @brief  HOSTģʽ��ʹ��ĳ���˵��ж�
 * @param  Pipe Pipeָ��
 * @param  Func �жϻص�����ָ��
 * @return NONE
 */
void hfusb_host_enable_int(HFU_PIPE_INFO* Pipe, HFFPCALLBACK Func);

/**
 * @brief  HOSTģʽ�½�ֹĳ���˵��ж�
 * @param  Pipe Pipeָ��
 * @return NONE
 */
void hfusb_host_disable_int(HFU_PIPE_INFO* Pipe);

/**
 * @brief  ����������豸��ַ
 * @param  Address ������豸��ַ
 * @return 1-�ɹ���0-ʧ��
 */
int  hfusb_host_set_address(uint8_t Address);

/**
 * @brief  ��ȡ������
 * @param  Type ����������
 * @param  Index index
 * @param  Buf ���������ջ�����ָ��
 * @param  Size �������󳤶�
 * @return 1-�ɹ���0-ʧ��
 */
int hfusb_host_get_descriptor(uint8_t Type, uint8_t Index, uint8_t* Buf, uint16_t Size);

/**
 * @brief  ��������
 * @param  ConfigurationNum ���ú�
 * @return 1-�ɹ���0-ʧ��
 */
int hfusb_host_set_configuration(uint8_t ConfigurationNum);

/**
 * @brief  ���ýӿ�
 * @param  InterfaceNum �ӿں�
 * @return 1-�ɹ���0-ʧ��
 */
int hfusb_host_set_interface(uint8_t InterfaceNum);

#endif


