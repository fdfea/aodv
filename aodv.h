
#ifndef AODV_H
#define AODV_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <netinet/in.h>

#define AODV_CONFIG_PATH_NAME				"Aodv"
#define AODV_CONFIG_VALUE_NAME_ADDRESS		"Address"
#define AODV_CONFIG_VALUE_DEFAULT_ADDRESS	(0x1)

#define AODV_PACKET_HDR_SIZE 	(20)
#define AODV_PACKET_DATA_SIZE 	(1000)
#define AODV_PACKET_SIZE		(1020)
#define AODV_TX_QUEUE_SIZE		(10)
#define AODV_MAX_NETWORK_SIZE 	(5)

#define AODV_BROADCAST_ADDR		(0xFFFF)

#define AODV_RX_INTERVAL_MS				(1000)
#define AODV_HELLO_INTERVAL_MS			(2000)
#define AODV_TX_QUEUE_POLL_INTERVAL_MS	(500)

#define AODV_HELLO_TIMEOUT_MS			(4000)
#define AODV_ACTIVE_ROUTE_TIMEOUT_MS	(7000)
#define AODV_REVERSE_PATH_TIMEOUT_MS	(3000)
#define AODV_QUEUE_POLL_TIMEOUT_MS		(3000)
#define AODV_QUEUE_DATA_LIFETIME_MS		(6000)

#define AODV_UDP_BUFSIZE	(2048)
#define AODV_UDP_PORT 		(5055)
#define AODV_UDP_TIMEOUT_MS	(1000)

/* 
 *
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 */

typedef enum AODVPacketType
{
	AODV_PACKET_NONE =  0,

	AODV_PACKET_RREQ = 	120,

	AODV_PACKET_RREP = 	121,

	AODV_PACKET_RERR = 	122,

	AODV_PACKET_HELLO = 123,

	AODV_PACKET_DATA = 	124,

} eAODVPacketType;


typedef enum AODVDeviceType
{
	AODV_DEVICE_NONE =  0,

	AODV_DEVICE_AND = 	1, //Android

	AODV_DEVICE_XX = 	2, 

} eAODVDeviceType;


typedef enum AODVNetLinkType
{
	AODV_NETLINK_NONE = 0,

	AODV_NETLINK_ALL  = 1,

	AODV_NETLINK_XX = 2,

	AODV_NETLINK_UDP  = 3,

} eAODVNetLinkType;


typedef struct AODVPacketHdr
{
	uint8_t PacketType;

	uint8_t NetLinkType;

	uint16_t SrcAddr; //origination addr

	uint16_t SrcSeqNum;

	uint16_t DestAddr; //final destination

	uint16_t DestSeqNum;

	uint16_t NextAddr; //address of next hop

	uint16_t SendAddr; //address of previous hop

	uint8_t SendDevType; //device type of sender

	uint16_t BcastSeqNum;

	uint8_t HopCnt;

	uint16_t Length; //length of data

} __attribute__ ((packed)) tAODVPacketHdr;


typedef struct AODVPacket
{
	tAODVPacketHdr Hdr; //20 bytes

	uint8_t Data[0]; //pointer to data

} __attribute__((packed)) tAODVPacket;


typedef enum AODVThreadState
{

  AODV_THREAD_STATE_NONE = 0x00,

  AODV_THREAD_STATE_INIT = 0x01,

  AODV_THREAD_STATE_RUN  = 0x02,

  AODV_THREAD_STATE_STOP = 0x04,

  AODV_THREAD_STATE_END  = 0x08,

} eAODVThreadState;


typedef struct AODVNode
{
	uint16_t Addr;

	uint16_t SeqNum;

	uint16_t BcastSeqNum;

} tAODVNode;


typedef struct AODVRoute
{
	bool Valid;

	uint16_t DestAddr;

	uint16_t DestSeqNum;

	uint16_t BcastSeqNum;

	uint16_t NextHopAddr;

	uint8_t NetLinkType;

	uint16_t HopCnt;

	uint64_t Timeout;

} tAODVRoute;


typedef struct AODVStats
{
	uint16_t HelloTxOkay;
	uint16_t HelloTxFail;
	uint16_t HelloRxOkay;

	uint16_t DATATxOkay;
	uint16_t DATATxFail;
	uint16_t DATARxOkay;

	uint16_t RREQTxOkay;
	uint16_t RREQTxFail;
	uint16_t RREQRxOkay;

	uint16_t RREPTxOkay;
	uint16_t RREPTxFail;
	uint16_t RREPRxOkay;

	uint16_t RERRTxOkay;
	uint16_t RERRTxFail;
	uint16_t RERRRxOkay;

	uint16_t UDPTxOkay;
	uint16_t UDPTxFail;
	uint16_t UDPRxOkay;

} tAODVStats;

typedef struct AODVTxData
{
	uint16_t Address;

	uint16_t DataLen;

	uint64_t Lifetime;

	char *pData;

} tAODVTxData;


typedef struct AODV
{
	tAODVNode Self;

	tAODVRoute RouteTable[AODV_MAX_NETWORK_SIZE];

/*
 *
 * 
 * 
 * 
 */

	pthread_t TxCtrlThread;
	pthread_attr_t TxCtrlThreadAttr;
	uint8_t TxCtrlThreadState;

	pthread_t RxCtrlThread;
	pthread_attr_t RxCtrlThreadAttr;
	uint8_t RxCtrlThreadState;

	pthread_t TxDataThread;
	pthread_attr_t TxDataThreadAttr;
	uint8_t TxDataThreadState;

	pthread_t UDPServerThread;
	pthread_attr_t UDPServerThreadAttr;
	uint8_t UDPServerThreadState;

	int SendSockfd; //socket for UDP network transmissions
	struct sockaddr_in SendSockAddr;

	tAODVTxData *TxQueue[AODV_TX_QUEUE_SIZE]; //"queue" for processing data

	pthread_mutex_t RouteTableMutex; //mutex for whenever RouteTable is read/modified
	pthread_mutexattr_t RouteTableMutexAttr;

	pthread_mutex_t TxMutex; //mutex to synchronize network transmissions
	pthread_mutexattr_t TxMutexAttr;

	pthread_mutex_t DataMutex;
	pthread_mutexattr_t DataMutexAttr;

	tAODVStats Stats;

} tAODV;

int AODV_Open(tAODV **ppAodv, pthread_attr_t *pAttr, char *pConfigFileName);
int AODV_Close(tAODV *pAodv);
int AODV_SendMessage(tAODV *pAodv, tAODVTxData *pTxData);
/*
 *
 */
void AODV_PrintStats(tAODV *pAodv);

#endif // AODV_H
