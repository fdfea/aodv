
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "libconfig.h"

/* 
 *
 * 
 * 
 * 
 * 
 */

#include "aodv.h"

#define dbg_printf(f, a...)                                       	 \
    do {                                                          	 \
        { fprintf(stderr, "%s(%d): " f, __func__, __LINE__, ## a); } \
    } while (0)

//Packet tx/rx
static int AODV_SendPacket(tAODV *pAodv, tAODVPacket *pPkt);
static int AODV_SendXXMessage(tAODV *pAodv, tAODVPacket *pPkt);
static int AODV_SendUDPMessage(tAODV *pAodv, tAODVPacket *pPkt);
/*
 *
 */
static void AODV_TxCtrlThreadProc(void *pArg);
static void AODV_RxCtrlThreadProc(void *pArg);
static void AODV_TxDataThreadProc(void *pArg);
static void AODV_UDPServerThreadProc(void *pArg);

//Routing control
static void AODV_HandleMessage(tAODV *pAodv, tAODVPacket *pPkt);
static void AODV_InitRREQ(tAODV *pAodv, tAODVPacket *pPkt, uint16_t DestAddr);
static void AODV_HandleRREQ(tAODV *pAodv, tAODVPacket *pPkt);
static void AODV_InitRREP(tAODV *pAodv, tAODVPacket *pPkt, uint16_t SrcAddr, uint16_t DestAddr);
static void AODV_HandleRREP(tAODV *pAodv, tAODVPacket *pPkt);
static void AODV_InitRERR(tAODV *pAodv, tAODVPacket *pPkt, uint16_t DestAddr, uint16_t SrcAddr);
static void AODV_HandleRERR(tAODV *pAodv, tAODVPacket *pPkt);
static void AODV_InitHELLO(tAODV *pAodv, tAODVPacket *pPkt);
static void AODV_HandleHELLO(tAODV *pAodv, tAODVPacket *pPkt);
static void AODV_InitDATA(tAODV *pAodv, tAODVPacket *pPkt, uint16_t Addr, uint16_t DataLen, char *data);
static void AODV_HandleDATA(tAODV *pAodv, tAODVPacket *pPkt);

//Utils
static int AODV_LoadConfig(tAODV *pAodv, char *pConfigFileName);
static int AODV_UDPSendSocketInit(tAODV *pAodv);
static void AODV_PrintRouteTable(tAODV *pAodv);
static void AODV_PrintPacket(tAODVPacket *pPkt);
static void AODV_SwapPacketEndianness(tAODVPacket *pPkt);
static bool AODV_IsAddressValid(uint16_t Addr);
static bool AODV_HasRoute(tAODV *pAodv, uint16_t Addr);
static void AODV_CheckRouteExpiry(tAODV *pAodv);
static uint8_t AODV_DeviceToNetLink(uint8_t DeviceType);
static uint64_t AODV_CurrentTimeMillis();
static uint16_t bswap16(uint16_t b);

int AODV_Open(tAODV **ppAodv, pthread_attr_t *pAttr, char *pConfigFileName)
{
	int Res = -ENOSYS;

	tAODV* pAodv = (tAODV *) malloc(sizeof(tAODV));
	if (pAodv == NULL)
	{
		Res = -ENOMEM;
	    goto Error;
	}

	memset(pAodv, 0, sizeof(tAODV));

	Res = AODV_LoadConfig(pAodv, pConfigFileName);
	if (Res != 0)
	{
		dbg_printf("Error loading config\n");
		goto Error;
	}

/*
 *
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 */

    //initialize route entries to false;
    int i;
    for (i = 0; i < AODV_MAX_NETWORK_SIZE; i++)
    {
    	pAodv->RouteTable[i].Valid = false;
    }

    //initalize queue items to null
    for (i = 0; i < AODV_TX_QUEUE_SIZE; i++)
    {
    	pAodv->TxQueue[i] = NULL;
    }

    Res = AODV_UDPSendSocketInit(pAodv);
    if (Res < 0)
    {
		dbg_printf("Error initializing send socket\n");
		goto Error;
    }

    //initialize mutexes
	pthread_mutexattr_init(&pAodv->RouteTableMutexAttr);
	pthread_mutexattr_settype(&pAodv->RouteTableMutexAttr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutexattr_init(&pAodv->TxMutexAttr);
	pthread_mutexattr_init(&pAodv->DataMutexAttr);

	Res = pthread_mutex_init(&pAodv->RouteTableMutex, &pAodv->RouteTableMutexAttr);
	if (Res != 0)
	{
		dbg_printf("Error initializing Tx Mutex\n");
		goto Error;
	}

	Res = pthread_mutex_init(&pAodv->TxMutex, &pAodv->TxMutexAttr);
	if (Res != 0)
	{
		dbg_printf("Error initializing Tx Mutex\n");
		goto Error;
	}

	Res = pthread_mutex_init(&pAodv->DataMutex, &pAodv->DataMutexAttr);
	if (Res != 0)
	{
		dbg_printf("Error initializing Data Mutex\n");
		goto Error;
	}

	pAodv->TxCtrlThreadAttr = *pAttr;
	pAodv->RxCtrlThreadAttr = *pAttr;
	pAodv->TxDataThreadAttr = *pAttr;
	pAodv->UDPServerThreadAttr = *pAttr;

	pAodv->TxCtrlThreadState |= AODV_THREAD_STATE_INIT;

	Res = pthread_create(&pAodv->TxCtrlThread,
	                     &pAodv->TxCtrlThreadAttr,
	                     (void *) AODV_TxCtrlThreadProc,
	                     pAodv);
	if (Res != 0)
	{
		dbg_printf("Error initializing Tx Ctrl Thread\n");
	    pAodv->TxCtrlThreadState = AODV_THREAD_STATE_NONE;
	    goto Error;
	}

	pAodv->RxCtrlThreadState |= AODV_THREAD_STATE_INIT;

	Res = pthread_create(&pAodv->RxCtrlThread,
						 &pAodv->RxCtrlThreadAttr,
						 (void *) AODV_RxCtrlThreadProc,
						 pAodv);
	if (Res != 0)
	{
		dbg_printf("Error initializing Rx Ctrl Thread\n");
	    pAodv->RxCtrlThreadState = AODV_THREAD_STATE_NONE;
	    goto Error;
	}

	pAodv->TxDataThreadState |= AODV_THREAD_STATE_INIT;

	Res = pthread_create(&pAodv->TxDataThread,
						 &pAodv->TxDataThreadAttr,
						 (void *) AODV_TxDataThreadProc,
						 pAodv);
	if (Res != 0)
	{
		dbg_printf("Error initializing Tx Data Thread\n");
	    pAodv->TxDataThreadState = AODV_THREAD_STATE_NONE;
	    goto Error;
	}

	pAodv->UDPServerThreadState |= AODV_THREAD_STATE_INIT;

	Res = pthread_create(&pAodv->UDPServerThread,
						 &pAodv->UDPServerThreadAttr,
						 (void *) AODV_UDPServerThreadProc,
						 pAodv);
	if (Res != 0)
	{
		dbg_printf("Error initializing UDP Server Thread\n");
	    pAodv->UDPServerThreadState = AODV_THREAD_STATE_NONE;
	    goto Error;
	}

	*ppAodv = pAodv;
	Res = 0;
	goto Success;

Error:
	dbg_printf("AODV Open Failed\n");
	AODV_Close(pAodv);

Success:
	return Res;
}

int AODV_Close(tAODV* pAodv)
{
	int Res = -ENOSYS;

	if (pAodv == NULL)
	{
		Res = -EINVAL;
	    goto Error;
	}

	pAodv->TxCtrlThreadState |= AODV_THREAD_STATE_STOP;
	pAodv->RxCtrlThreadState |= AODV_THREAD_STATE_STOP;
	pAodv->TxDataThreadState |= AODV_THREAD_STATE_STOP;
	pAodv->UDPServerThreadState |= AODV_THREAD_STATE_STOP;

	if (pAodv->TxCtrlThreadState & AODV_THREAD_STATE_INIT)
	{
	    pthread_join(pAodv->TxCtrlThread, NULL);
	}

	if (pAodv->RxCtrlThreadState & AODV_THREAD_STATE_INIT)
	{
	    pthread_join(pAodv->RxCtrlThread, NULL);
	}

	if (pAodv->TxDataThreadState & AODV_THREAD_STATE_INIT)
	{
	    pthread_join(pAodv->TxDataThread, NULL);
	}

	if (pAodv->UDPServerThreadState & AODV_THREAD_STATE_INIT)
	{
	    pthread_join(pAodv->UDPServerThread, NULL);
	}

	dbg_printf("AODV Threads stopped\n");

	pthread_mutex_destroy(&pAodv->TxMutex);
	pthread_mutex_destroy(&pAodv->DataMutex);
	pthread_mutexattr_destroy(&pAodv->TxMutexAttr);
	pthread_mutexattr_destroy(&pAodv->DataMutexAttr);

/*
 * 
 * 
 * 
 */

	//free tx queue
	tAODVTxData *pTxData;
	int i;
	for (i = 0; i < AODV_TX_QUEUE_SIZE; i++)
	{
		pTxData = pAodv->TxQueue[i];
		if (pTxData != NULL)
		{
			if (pTxData->pData != NULL)
			{
				free(pTxData->pData);
			}
			free(pTxData);
            pAodv->TxQueue[i] = NULL;
		}
	}

Error:
	free(pAodv);
	return Res;
}

int AODV_SendMessage(tAODV *pAodv, tAODVTxData *pTxData)
{
	int Res = -ENOSYS;

	if (pTxData == NULL)
	{
		dbg_printf("pTxData was NULL\n");
		Res = -EINVAL;
		goto Error;
	}

	uint16_t Address = pTxData->Address;

	if (!AODV_IsAddressValid(Address) || Address == pAodv->Self.Addr)
	{
		dbg_printf("Invalid Address for AODV Send\n");
		Res = -EINVAL;
		goto Error;
	}

	//if no route to address, send RREQ
	if (!AODV_HasRoute(pAodv, Address))
	{
		pAodv->Self.BcastSeqNum++;

		tAODVPacket *pMsg = (tAODVPacket *) calloc(1, AODV_PACKET_SIZE);

		AODV_InitRREQ(pAodv, pMsg, Address);

		Res = AODV_SendPacket(pAodv, pMsg);
		if (Res != 0)
		{
			pAodv->Stats.RREQTxFail++;
			dbg_printf("AODV Send RREQ Failed, Src=%#x, Dest=%x\n", pAodv->Self.Addr, Address);
		}
		else
		{
			pAodv->Stats.RREQTxOkay++;
		}

		free(pMsg);
	}

	//add TxData to first empty spot in queue
	bool Added = false;
	pthread_mutex_lock(&pAodv->DataMutex);
	for (int i = 0; i < AODV_TX_QUEUE_SIZE; ++i)
	{
		if (pAodv->TxQueue[i] == NULL)
		{
			tAODVTxData *pTxDataQ = (tAODVTxData *) malloc(sizeof(tAODVTxData));
			pTxDataQ->Address = Address;
			pTxDataQ->DataLen = pTxData->DataLen;
			pTxDataQ->pData = strndup(pTxData->pData, pTxData->DataLen);
			pTxDataQ->Lifetime = AODV_CurrentTimeMillis() + AODV_QUEUE_DATA_LIFETIME_MS;
			pAodv->TxQueue[i] = pTxDataQ;
			Added = true;
			break;
		}
	}
	pthread_mutex_unlock(&pAodv->DataMutex);

	if (!Added)
	{
		dbg_printf("Data Queue is full");
		Res = -EAGAIN;
		goto Error;
	}

	Res = 0;

Error:
	return Res;
}

/* 
 *
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 */

void AODV_PrintStats(tAODV *pAodv)
{
    dbg_printf("AODV HELLO: Tx (Okay %d, Fail %d) | Rx (Okay %d)\n"
    				"AODV DATA : Tx (Okay %d, Fail %d) | Rx (Okay %d)\n"
    				"AODV RREQ : Tx (Okay %d, Fail %d) | Rx (Okay %d)\n"
    				"AODV RREP : Tx (Okay %d, Fail %d) | Rx (Okay %d)\n"
    				"AODV RERR : Tx (Okay %d, Fail %d) | Rx (Okay %d)\n"
    				"AODV UDP  : Tx (Okay %d, Fail %d) | Rx (Okay %d)\n",
    		pAodv->Stats.HelloTxOkay, pAodv->Stats.HelloTxFail, pAodv->Stats.HelloRxOkay,
			pAodv->Stats.DATATxOkay, pAodv->Stats.DATATxFail, pAodv->Stats.DATARxOkay,
			pAodv->Stats.RREQTxOkay, pAodv->Stats.RREQTxFail, pAodv->Stats.RREQRxOkay,
			pAodv->Stats.RREPTxOkay, pAodv->Stats.RREPTxFail, pAodv->Stats.RREPRxOkay,
			pAodv->Stats.RERRTxOkay, pAodv->Stats.RERRTxFail, pAodv->Stats.RERRRxOkay,
			pAodv->Stats.UDPTxOkay, pAodv->Stats.UDPTxFail, pAodv->Stats.UDPRxOkay);

    AODV_PrintRouteTable(pAodv);
}

static void AODV_TxCtrlThreadProc(void *pArg)
{
	int Res = -ENOSYS;
	struct timespec Time;
	tAODVPacket *pHelloPkt;
	tAODV *pAodv;

	dbg_printf("Started AODV Tx Ctrl thread\n");

	pAodv = (tAODV *) pArg;

	pHelloPkt = (tAODVPacket *) calloc(1, AODV_PACKET_HDR_SIZE + AODV_PACKET_DATA_SIZE);

	pAodv->TxCtrlThreadState |= AODV_THREAD_STATE_RUN;

	clock_gettime(CLOCK_MONOTONIC, &Time);

	//Thread loop, send a hello packet to neighbors every hello interval
	while ((pAodv->TxCtrlThreadState & AODV_THREAD_STATE_STOP) == 0)
	{
	    AODV_InitHELLO(pAodv, pHelloPkt);

	    Res = AODV_SendPacket(pAodv, pHelloPkt);
	    if (Res == 0)
	    {
	    	pAodv->Stats.HelloTxOkay++;
	    }
	    else
	    {
	    	pAodv->Stats.HelloTxFail++;
	    }

/*
 *
 */
	}

	free(pHelloPkt);
	(void) pthread_exit(NULL);
}

static void AODV_RxCtrlThreadProc(void *pArg)
{
	tAODV* pAodv;
	struct timespec Time;
/*
 *
 */

	dbg_printf("Started AODV Rx Ctrl thread\n");

	pAodv = (tAODV *) pArg;

	pAodv->RxCtrlThreadState |= AODV_THREAD_STATE_RUN;

	clock_gettime(CLOCK_MONOTONIC, &Time);

/*
 *
 * 
 * 
 * 
 * 
 * 
 * 
 */

	while ((pAodv->RxCtrlThreadState & AODV_THREAD_STATE_STOP) == 0)
	{
		// check for route expirations
		AODV_CheckRouteExpiry(pAodv);

/*
 *
 */
	}
/*
 *
 */

Error:
	(void) pthread_exit(NULL);
}

static void AODV_TxDataThreadProc(void *pArg)
{
	int Res = -ENOSYS;
	struct timespec Time;
	tAODV *pAodv;
	tAODVPacket *pMsg;

	dbg_printf("Started AODV Tx Data thread\n");

	pAodv = (tAODV *) pArg;

	pMsg = (tAODVPacket *) calloc(1, AODV_PACKET_SIZE);

	pAodv->TxDataThreadState |= AODV_THREAD_STATE_RUN;

	clock_gettime(CLOCK_MONOTONIC, &Time);

	//thread loop, wait for data in queue and then send it
	while ((pAodv->TxDataThreadState & AODV_THREAD_STATE_STOP) == 0)
	{
		pthread_mutex_lock(&pAodv->DataMutex);

		uint64_t TimeMillis = AODV_CurrentTimeMillis();

		for (int i = 0; i < AODV_TX_QUEUE_SIZE; ++i)
		{
			tAODVTxData *pTxData = pAodv->TxQueue[i];

			if (pTxData != NULL)
			{
				bool SentData = false;
				uint16_t Address = pTxData->Address;

				//if a route exists send the data
				if (AODV_HasRoute(pAodv, Address))
				{
					uint16_t LenSending = 0;

					char *DataStart = pTxData->pData;
					char *DataEnd = DataStart + pTxData->DataLen;
					while (DataStart < DataEnd)
					{
						LenSending = (DataEnd - DataStart) >= AODV_PACKET_DATA_SIZE
								? AODV_PACKET_DATA_SIZE : (DataEnd - DataStart);

						memset(pMsg, 0, sizeof(tAODVPacket));

						AODV_InitDATA(pAodv, pMsg, Address, LenSending, DataStart);

						Res = AODV_SendPacket(pAodv, pMsg);
						if (Res != 0)
						{
							pAodv->Stats.DATATxFail++;
							dbg_printf("AODV Send Data Failed, Src=%#x, Dest=%#x, Len=%d\n",
									pAodv->Self.Addr, Address, LenSending);
						}
						else
						{
							pAodv->Stats.DATATxOkay++;
						}

						DataStart += LenSending;
					}

					SentData = true;
				}

				//remove expired data, route was never found
				if (SentData || pTxData->Lifetime <= TimeMillis)
				{
					if (pTxData->pData != NULL)
					{
						free(pTxData->pData);
					}

					free(pTxData);
					pAodv->TxQueue[i] = NULL;
				}
			}
		}

		pthread_mutex_unlock(&pAodv->DataMutex);

/*
 *
 */
	}

	free(pMsg);
	(void) pthread_exit(NULL);
}

static void AODV_UDPServerThreadProc(void *pArg)
{
	int Res = -ENOSYS;
	char buffer[AODV_UDP_BUFSIZE];
	struct sockaddr_in ServerAddr, ClientAddr;
	int sockfd;
	tAODV *pAodv;

	dbg_printf("Started AODV UDP Server thread\n");

	pAodv = (tAODV *) pArg;

	tAODVPacket *pPkt = calloc(1, sizeof(tAODVPacket));

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
	{
	    dbg_printf("ERROR: socket open failed\n");
	    goto Error;
	}

	int ReuseAddr = 1;
	Res = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &ReuseAddr, sizeof(int));
	if (Res < 0)
	{
	    dbg_printf("ERROR: set socket opt reuseaddr failed\n");
	    goto Error;
	}

    int BcastEnable=1;
    Res = setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &BcastEnable, sizeof(int));
    if (Res < 0) {
        dbg_printf("ERROR: set socket opt broadcast failed\n");
        goto Error;
    }

	memset(&ServerAddr, 0, sizeof(ServerAddr));

	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	ServerAddr.sin_port = htons(AODV_UDP_PORT);

	Res = bind(sockfd, (struct sockaddr *) &ServerAddr, sizeof(struct sockaddr));
	if (Res < 0)
	{
	    dbg_printf("ERROR: socket bind failed\n");
	    goto Error;
	}

    dbg_printf("AODV UDP Server listening on port %d...\n", AODV_UDP_PORT);

	pAodv->UDPServerThreadState |= AODV_THREAD_STATE_RUN;

	while ((pAodv->UDPServerThreadState & AODV_THREAD_STATE_STOP) == 0)
	{

		int ClientSize = sizeof(ClientAddr);

		memset(&ClientAddr, 0, ClientSize);
	    memset(buffer, 0, AODV_UDP_BUFSIZE);

	    Res = recvfrom(sockfd, buffer, AODV_UDP_BUFSIZE, 0,
	    		(struct sockaddr *) &ClientAddr, (socklen_t *) &ClientSize);
	    if (Res < 0)
	    {
	    	dbg_printf("ERROR: receive failed\n");
	    	continue;
	    }
	    if (Res < AODV_PACKET_HDR_SIZE)
	    {
	    	dbg_printf("ERROR: received packet with incorrect structure\n");
	    	continue;
	    }

	    memset(pPkt, 0, sizeof(tAODVPacket));
	    memcpy(pPkt, buffer, Res);

    	//drop packets from same device type, as UDP is not preffered link
	    if (pPkt->Hdr.SendDevType == AODV_DEVICE_XX)
	    {
	    	dbg_printf("Dropping packet from same device type\n");
	    	continue;
	    }
	    if(pPkt->Hdr.NextAddr != pAodv->Self.Addr && pPkt->Hdr.NextAddr != AODV_BROADCAST_ADDR)
	    {
	    	dbg_printf("Not intended packet recipient: %#x\n", pPkt->Hdr.NextAddr);
	    	continue;
	    }

	    dbg_printf("AODV UDP Server received %d bytes from %s\n",
	    		Res, inet_ntoa(ClientAddr.sin_addr));

	    pAodv->Stats.UDPRxOkay++;

	    //the endianness is swapped from Androids
        if (pPkt->Hdr.SendDevType == AODV_DEVICE_AND)
        {
	        AODV_SwapPacketEndianness(pPkt);
        }

	    //print packet for debug
	    AODV_PrintPacket(pPkt);

	    AODV_HandleMessage(pAodv, pPkt);
	}

Error:
	close(sockfd);
	free(pPkt);
	(void) pthread_exit(NULL);
}

static int AODV_LoadConfig(tAODV *pAodv, char* pConfigFileName)
{
	int Res = -ENOSYS;
	config_t Config;
	config_t *pConfig;
	config_setting_t *pSetting;
	int ConfigVal = 0;
	pConfig = &Config;
	config_init(pConfig);

	pAodv->Self.Addr = AODV_CONFIG_VALUE_DEFAULT_ADDRESS;
	pAodv->Self.BcastSeqNum = 0;
	pAodv->Self.SeqNum = 0;

	if (config_read_file(pConfig, pConfigFileName) != CONFIG_TRUE)
	{
		dbg_printf("Could not open %s\n", pConfigFileName);
	    Res = -EINVAL;
	    goto Error;
	}

	pSetting = config_lookup(pConfig, AODV_CONFIG_PATH_NAME);
	if (pSetting == NULL)
	{
	    dbg_printf("config_lookup(%s) failed\n", AODV_CONFIG_PATH_NAME);
	    Res = -EINVAL;
	    goto Error;
	}

	if (config_setting_lookup_int(pSetting,
	                              AODV_CONFIG_VALUE_NAME_ADDRESS,
	                              &ConfigVal))
	{
		pAodv->Self.Addr = (uint16_t) ConfigVal;
	}
    dbg_printf("AODV Node Addr set to %d\n", pAodv->Self.Addr);

	config_destroy(pConfig);

	Res = 0; 

Error:
	return Res;
}

static int AODV_SendXXMessage(tAODV *pAodv, tAODVPacket *pPkt)
{
/* 
 *
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 */
}

static int AODV_SendPacket(tAODV *pAodv, tAODVPacket *pPkt)
{
	int Res = -ENOSYS;

	pPkt->Hdr.SendAddr = pAodv->Self.Addr;
	pPkt->Hdr.SendDevType = AODV_DEVICE_XX;

	switch(pPkt->Hdr.NetLinkType) {
		case AODV_NETLINK_ALL: {
			Res = AODV_SendXXMessage(pAodv, pPkt);
			Res |= AODV_SendUDPMessage(pAodv, pPkt);
			break;
		}
		case AODV_NETLINK_XX: {
			Res = AODV_SendXXMessage(pAodv, pPkt);
			break;
		}
		case AODV_NETLINK_UDP: {
			Res = AODV_SendUDPMessage(pAodv, pPkt);
			break;
		}
		default: {
			dbg_printf("Dropping packet with unknown NetLinkType\n");
		}
	}

	return Res;
}

/* 
 *
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 */

static int AODV_UDPSendSocketInit(tAODV *pAodv)
{
	int Res = -ENOSYS;

    pAodv->SendSockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (pAodv->SendSockfd < 0) {
        dbg_printf("ERROR: socket open failed\n");
        goto Error;
    }

	int ReuseAddr = 1;
	Res = setsockopt(pAodv->SendSockfd, SOL_SOCKET, SO_REUSEADDR, &ReuseAddr, sizeof(int));
	if (Res < 0)
	{
	    dbg_printf("ERROR: set socket opt reuseaddr failed\n");
	    goto Error;
	}

    int BcastEnable=1;
    Res = setsockopt(pAodv->SendSockfd, SOL_SOCKET, SO_BROADCAST, &BcastEnable, sizeof(int));
    if (Res < 0) {
        dbg_printf("ERROR: set socket opt broadcast failed\n");
        goto Error;
    }

    memset(&pAodv->SendSockAddr, 0, sizeof(struct sockaddr_in));
    pAodv->SendSockAddr.sin_family = AF_INET;
    //broadcast IP for wifi, make this configurable at some point
    Res = inet_aton("192.168.10.255", &pAodv->SendSockAddr.sin_addr);
    pAodv->SendSockAddr.sin_port = htons(AODV_UDP_PORT);
    if (Res < 0) {
        dbg_printf("ERROR: invalid socket address\n");
    	goto Error;
    }

	Res = bind(pAodv->SendSockfd, (struct sockaddr*) &pAodv->SendSockAddr, sizeof(struct sockaddr));
	if (Res < 0)
	{
	    dbg_printf("ERROR: socket bind failed\n");
	    goto Error;
	}

	Res = 0;
	dbg_printf("Started UDP Sender Socket\n");

Error:
	return Res;
}

static int AODV_SendUDPMessage(tAODV *pAodv, tAODVPacket *pPkt)
{
	int Res = -ENOSYS;
    char buffer[AODV_UDP_BUFSIZE];

	memset(buffer, 0, AODV_UDP_BUFSIZE);
	//copy message into buffer
    //int StrLen = strlen(pStr);
	int PacketSize = sizeof(tAODVPacketHdr) + pPkt->Hdr.Length;

	//swap endianness for Androids
	AODV_SwapPacketEndianness(pPkt);

	memcpy(buffer, pPkt, PacketSize);
	//memcpy(buffer, pStr, StrLen);

	pthread_mutex_lock(&pAodv->TxMutex);
	Res = sendto(pAodv->SendSockfd, buffer, PacketSize, 0,
			(struct sockaddr*) &pAodv->SendSockAddr, sizeof(struct sockaddr));
	pthread_mutex_unlock(&pAodv->TxMutex);
	//Res = sendto(pAodv->SendSockfd, buffer, StrLen, 0,
			//(struct sockaddr*) &pAodv->SendSockAddr, sizeof(struct sockaddr));
	if (Res < 0) {
		pAodv->Stats.UDPTxFail++;
		dbg_printf("ERROR: send message failed: %d\n", errno);
		goto Error;
	}

	dbg_printf("Sent AODV UDP Packet: %d bytes to %s\n",
			Res, inet_ntoa(pAodv->SendSockAddr.sin_addr));

	Res = 0;
	pAodv->Stats.UDPTxOkay++;

Error:
	return Res;
}

static void AODV_HandleMessage(tAODV *pAodv, tAODVPacket *pPkt)
{
	switch (pPkt->Hdr.PacketType)
	{
		case AODV_PACKET_HELLO:
		{
			pAodv->Stats.HelloRxOkay++;
			AODV_HandleHELLO(pAodv, pPkt);
			break;
		}
		case AODV_PACKET_DATA:
		{
			pAodv->Stats.DATARxOkay++;
			AODV_HandleDATA(pAodv, pPkt);
			break;
		}
		case AODV_PACKET_RREQ:
		{
			pAodv->Stats.RREQRxOkay++;
			AODV_HandleRREQ(pAodv, pPkt);
			break;
		}
		case AODV_PACKET_RREP:
		{
			pAodv->Stats.RREPRxOkay++;
			AODV_HandleRREP(pAodv, pPkt);
			break;
		}
		case AODV_PACKET_RERR:
		{
			pAodv->Stats.RERRRxOkay++;
			AODV_HandleRERR(pAodv, pPkt);
			break;
		}
		default:
		{
			dbg_printf("Received packet of unknown AODV type %d\n", pPkt->Hdr.PacketType);
		}
	}
}

static void AODV_InitRREQ(tAODV *pAodv, tAODVPacket *pPkt, uint16_t DestAddr)
{
	tAODVRoute *pRoute 		= &pAodv->RouteTable[DestAddr];
	pPkt->Hdr.PacketType 	= AODV_PACKET_RREQ;
	pPkt->Hdr.NetLinkType	= AODV_NETLINK_ALL;
	pPkt->Hdr.SrcAddr 		= pAodv->Self.Addr;
	pPkt->Hdr.SrcSeqNum 	= pAodv->Self.SeqNum;
	pPkt->Hdr.SendAddr 		= pAodv->Self.Addr;
	pPkt->Hdr.BcastSeqNum 	= pAodv->Self.BcastSeqNum;
	pPkt->Hdr.DestAddr 		= DestAddr;
	pthread_mutex_lock(&pAodv->RouteTableMutex);
	pPkt->Hdr.DestSeqNum	= pRoute->DestSeqNum;
	pthread_mutex_unlock(&pAodv->RouteTableMutex);
	pPkt->Hdr.NextAddr		= AODV_BROADCAST_ADDR;
	pPkt->Hdr.HopCnt		= 0;
	dbg_printf("Initialized RREQ\n");
}

static void AODV_HandleRREQ(tAODV *pAodv, tAODVPacket *pPkt)
{

	uint16_t SrcAddr 	= pPkt->Hdr.SrcAddr;
	uint16_t DestAddr 	= pPkt->Hdr.DestAddr;

	if (!AODV_IsAddressValid(DestAddr) || !AODV_IsAddressValid(SrcAddr))
	{
		return;
	}

	if (pAodv->Self.Addr == SrcAddr)
	{
		return;
	}

	tAODVRoute *pRouteSrc 	= &pAodv->RouteTable[SrcAddr];
	tAODVRoute *pRouteDest	= &pAodv->RouteTable[DestAddr];

	//set up reverse route if one doesn't exist
	if (!AODV_HasRoute(pAodv, SrcAddr))
	{
		pthread_mutex_lock(&pAodv->RouteTableMutex);
		pRouteSrc->Valid		= true;
		pRouteSrc->NetLinkType  = AODV_DeviceToNetLink(pPkt->Hdr.SendDevType);
		pRouteSrc->DestAddr 	= pPkt->Hdr.SrcAddr;
		pRouteSrc->DestSeqNum 	= pPkt->Hdr.SrcSeqNum;
		pRouteSrc->NextHopAddr	= pPkt->Hdr.SendAddr;
		pRouteSrc->HopCnt		= pPkt->Hdr.HopCnt + 1;
		pthread_mutex_unlock(&pAodv->RouteTableMutex);
	}
	//update time since route might be used in rrep
	pthread_mutex_lock(&pAodv->RouteTableMutex);
	pRouteSrc->Timeout = AODV_CurrentTimeMillis() + AODV_REVERSE_PATH_TIMEOUT_MS;
	uint16_t SrcRouteBcastSeq = pRouteSrc->BcastSeqNum;
	pthread_mutex_unlock(&pAodv->RouteTableMutex);

	if (pPkt->Hdr.BcastSeqNum <= SrcRouteBcastSeq)
	{
		return;
	}

	pthread_mutex_lock(&pAodv->RouteTableMutex);
	pRouteSrc->BcastSeqNum = pPkt->Hdr.BcastSeqNum;
	uint16_t DestRouteDestSeq = pRouteDest->DestSeqNum;
	pthread_mutex_unlock(&pAodv->RouteTableMutex);

	int Res = 0;

	if (pAodv->Self.Addr == DestAddr || (AODV_HasRoute(pAodv, DestAddr)
	    && DestRouteDestSeq >= pPkt->Hdr.DestSeqNum))
	{
		pAodv->Self.SeqNum++;

		AODV_InitRREP(pAodv, pPkt, DestAddr, SrcAddr);

		Res = AODV_SendPacket(pAodv, pPkt);
		if (Res != 0)
		{
			pAodv->Stats.RREPTxFail++;
		}
		else
		{
			pAodv->Stats.RREPTxOkay++;
		}
	}
	else
	{
		pPkt->Hdr.SendAddr = pAodv->Self.Addr;
		pPkt->Hdr.HopCnt++;

		Res = AODV_SendPacket(pAodv, pPkt);
		if (Res != 0)
		{
			pAodv->Stats.RREQTxFail++;
		}
		else
		{
			pAodv->Stats.RREQTxOkay++;
		}
	}
}

static void AODV_InitRREP(tAODV *pAodv, tAODVPacket *pPkt, uint16_t SrcAddr, uint16_t DestAddr)
{
	tAODVRoute *pRoute		= &pAodv->RouteTable[DestAddr];
	pPkt->Hdr.PacketType 	= AODV_PACKET_RREP;
	pPkt->Hdr.SrcAddr 		= SrcAddr;
	pPkt->Hdr.SrcSeqNum 	= pAodv->Self.SeqNum;
	pPkt->Hdr.SendAddr 		= pAodv->Self.Addr;
	pPkt->Hdr.DestAddr 		= DestAddr;
	pthread_mutex_lock(&pAodv->RouteTableMutex);
	pPkt->Hdr.NetLinkType	= pRoute->NetLinkType;
	pPkt->Hdr.NextAddr		= pRoute->NextHopAddr;
	pPkt->Hdr.DestSeqNum 	= pRoute->DestSeqNum;
	pPkt->Hdr.HopCnt 		= pRoute->HopCnt;
	pthread_mutex_unlock(&pAodv->RouteTableMutex);
	dbg_printf("Initialized RREP\n");
}

static void AODV_HandleRREP(tAODV *pAodv, tAODVPacket *pPkt)
{
	uint16_t DestAddr = pPkt->Hdr.DestAddr;
	uint16_t SrcAddr = pPkt->Hdr.SrcAddr;

	if (!AODV_IsAddressValid(DestAddr) || !AODV_IsAddressValid(SrcAddr))
	{
		return;
	}

	tAODVRoute *pRoute 	= &pAodv->RouteTable[SrcAddr];

	pthread_mutex_lock(&pAodv->RouteTableMutex);
	uint16_t RouteDestSeq = pRoute->DestSeqNum;
	pthread_mutex_unlock(&pAodv->RouteTableMutex);

	//drop if seq num is not valid, only forward one rrep
	//should this be pPkt->Hdr.DestSeqNum?
	if (pPkt->Hdr.SrcSeqNum <= RouteDestSeq)
	{
		dbg_printf("Dropping RREP from seq nums\n");
		return;
	}

	//make route if not exists
	if (!AODV_HasRoute(pAodv, SrcAddr))
	{
		pthread_mutex_lock(&pAodv->RouteTableMutex);
		pRoute->Valid		= true;
		pRoute->NetLinkType = AODV_DeviceToNetLink(pPkt->Hdr.SendDevType);
		pRoute->DestAddr 	= SrcAddr;
		pRoute->HopCnt 		= pPkt->Hdr.HopCnt - 1;
		pRoute->NextHopAddr = pPkt->Hdr.SendAddr;
		pRoute->DestSeqNum 	= pPkt->Hdr.SrcSeqNum;
		pthread_mutex_unlock(&pAodv->RouteTableMutex);
		dbg_printf("Created route to %#x from RREP\n", SrcAddr);
	}
	//update timer for active route to src
	pthread_mutex_lock(&pAodv->RouteTableMutex);
	pRoute->Timeout	= AODV_CurrentTimeMillis() + AODV_ACTIVE_ROUTE_TIMEOUT_MS;
	pthread_mutex_unlock(&pAodv->RouteTableMutex);

	int Res = 0;

	//Route discovered, dest is ready to transmit data
	if (pAodv->Self.Addr == DestAddr)
	{
		return;
	}
	//else forward packet to next hop
	else if (AODV_HasRoute(pAodv, DestAddr))
	{
		tAODVRoute *pRoute 		= &pAodv->RouteTable[DestAddr];
		pPkt->Hdr.SendAddr 		= pAodv->Self.Addr;
		pthread_mutex_lock(&pAodv->RouteTableMutex);
		pPkt->Hdr.NetLinkType 	= pRoute->NetLinkType;
		pPkt->Hdr.NextAddr 		= pRoute->NextHopAddr;
		pthread_mutex_unlock(&pAodv->RouteTableMutex);
		pPkt->Hdr.HopCnt--;

		Res = AODV_SendPacket(pAodv, pPkt);
		if (Res != 0)
		{
			pAodv->Stats.RREPTxFail++;
		}
		else
		{
			pAodv->Stats.RREPTxOkay++;
		}
	}
	else
	{
		dbg_printf("RERR: Route %#x to %#x\n", SrcAddr, DestAddr);

		pAodv->Self.SeqNum++;

		if (!AODV_HasRoute(pAodv, SrcAddr))
		{
			dbg_printf("No route for RERR\n");
			return;
		}

		AODV_InitRERR(pAodv, pPkt, DestAddr, SrcAddr);

		Res = AODV_SendPacket(pAodv, pPkt);
		if (Res != 0)
		{
			pAodv->Stats.RERRTxFail++;
		}
		else
		{
			pAodv->Stats.RERRTxOkay++;
		}
	}
}

static void AODV_InitRERR(tAODV *pAodv, tAODVPacket *pPkt,
		           uint16_t SrcAddr, uint16_t DestAddr)
{
	tAODVRoute *pRoute 		= &pAodv->RouteTable[DestAddr];
	pPkt->Hdr.PacketType 	= AODV_PACKET_RERR;
	pPkt->Hdr.SrcAddr 		= SrcAddr;
	pPkt->Hdr.SrcSeqNum 	= pAodv->Self.SeqNum;
	pPkt->Hdr.SendAddr 		= pAodv->Self.Addr;
	pPkt->Hdr.DestAddr 		= DestAddr;
	pthread_mutex_lock(&pAodv->RouteTableMutex);
	pPkt->Hdr.NetLinkType	= pRoute->NetLinkType;
	pPkt->Hdr.NextAddr		= pRoute->NextHopAddr;
	pthread_mutex_unlock(&pAodv->RouteTableMutex);
	pPkt->Hdr.HopCnt		= UINT8_MAX;
	dbg_printf("Initialized RERR\n");
}

static void AODV_HandleRERR(tAODV *pAodv, tAODVPacket *pPkt)
{
	int Res = 0;
	uint16_t DestAddr = pPkt->Hdr.DestAddr;
	uint16_t SrcAddr = pPkt->Hdr.SrcAddr;

	//clear route table entry to src
	pthread_mutex_lock(&pAodv->RouteTableMutex);
	pAodv->RouteTable[SrcAddr].Valid = false;
	pthread_mutex_unlock(&pAodv->RouteTableMutex);

	//RERR has reached destination
	if (DestAddr == pAodv->Self.Addr)
	{
		return;
	}
	//else forward to next hop
	else if (AODV_HasRoute(pAodv, DestAddr))
	{
		tAODVRoute *pRoute 		= &pAodv->RouteTable[DestAddr];
		pPkt->Hdr.SendAddr 		= pAodv->Self.Addr;
		pthread_mutex_lock(&pAodv->RouteTableMutex);
		pPkt->Hdr.NetLinkType 	= pRoute->NetLinkType;
		pPkt->Hdr.NextAddr 		= pRoute->NextHopAddr;
		pthread_mutex_unlock(&pAodv->RouteTableMutex);

		Res = AODV_SendPacket(pAodv, pPkt);
		if (Res != 0)
		{
			pAodv->Stats.RERRTxFail++;
		}
		else
		{
			pAodv->Stats.RERRTxOkay++;
		}
	}
	else
	{
		dbg_printf("Dropping invalid RERR: No route to %#x", DestAddr);
	}
}

static void AODV_InitHELLO(tAODV *pAodv, tAODVPacket *pPkt)
{
	pPkt->Hdr.PacketType	= AODV_PACKET_HELLO;
	pPkt->Hdr.NetLinkType	= AODV_NETLINK_ALL;
	pPkt->Hdr.SrcAddr 		= pAodv->Self.Addr;
	pPkt->Hdr.SrcSeqNum 	= pAodv->Self.SeqNum;
	pPkt->Hdr.DestAddr 		= AODV_BROADCAST_ADDR;
	pPkt->Hdr.NextAddr		= AODV_BROADCAST_ADDR;
	pPkt->Hdr.SendAddr		= pAodv->Self.Addr;
	pPkt->Hdr.HopCnt 		= 0;
	dbg_printf("Initialized HELLO\n");
}

static void AODV_HandleHELLO(tAODV *pAodv, tAODVPacket *pPkt)
{
	uint16_t SrcAddr = pPkt->Hdr.SrcAddr;
	if (!AODV_IsAddressValid(SrcAddr))
	{
		return;
	}

	tAODVRoute *pRoute = &pAodv->RouteTable[SrcAddr];
	if (!AODV_HasRoute(pAodv, SrcAddr))
	{
			//new neighbor, add to route table;
			pRoute->Valid 		= true;
			pRoute->NetLinkType = AODV_DeviceToNetLink(pPkt->Hdr.SendDevType);
			pRoute->DestAddr 	= SrcAddr;
			pRoute->NextHopAddr = pPkt->Hdr.SendAddr;
			pRoute->HopCnt 		= pPkt->Hdr.HopCnt + 1;
	}
	//update these on every packet
	pthread_mutex_lock(&pAodv->RouteTableMutex);
	pRoute->DestSeqNum 	= pPkt->Hdr.SrcSeqNum;
	pRoute->Timeout = AODV_CurrentTimeMillis() + AODV_HELLO_TIMEOUT_MS;
	pthread_mutex_unlock(&pAodv->RouteTableMutex);
}

static void AODV_InitDATA(tAODV *pAodv, tAODVPacket *pPkt,
		uint16_t Addr, uint16_t DataLen, char *pData)
{
	tAODVRoute *pRoute 		= &pAodv->RouteTable[Addr];
	pPkt->Hdr.PacketType 	= AODV_PACKET_DATA;
	pPkt->Hdr.SrcAddr 		= pAodv->Self.Addr;
	pPkt->Hdr.SrcSeqNum 	= pAodv->Self.SeqNum;
	pPkt->Hdr.SendAddr 		= pAodv->Self.Addr;
	pthread_mutex_lock(&pAodv->RouteTableMutex);
	pPkt->Hdr.NetLinkType	= pRoute->NetLinkType;
	pPkt->Hdr.DestAddr 		= pRoute->DestAddr;
	pPkt->Hdr.DestSeqNum 	= pRoute->DestSeqNum;
	pPkt->Hdr.NextAddr		= pRoute->NextHopAddr;
	pthread_mutex_unlock(&pAodv->RouteTableMutex);
	pPkt->Hdr.Length 		= DataLen;
	memcpy(pPkt->Data, pData, DataLen);
	dbg_printf("Initialized DATA\n");
}

static void AODV_HandleDATA(tAODV *pAodv, tAODVPacket *pPkt)
{
	//if destination, pass data up to higher layer
	uint16_t DestAddr = pPkt->Hdr.DestAddr;
	uint16_t SrcAddr  = pPkt->Hdr.SrcAddr;

	int Res = 0;

	if (pAodv->Self.Addr == DestAddr)
	{
		dbg_printf("DATA Received! Src=%#x, Dest=%#x, Length=%d, Data=%s\n",
				SrcAddr, DestAddr, pPkt->Hdr.Length, pPkt->Data);
	}
	//if has route, forward to next hop
	else if (AODV_HasRoute(pAodv, DestAddr))
	{
		tAODVRoute *pRoute 		= &pAodv->RouteTable[DestAddr];
		pPkt->Hdr.SendAddr 		= pAodv->Self.Addr;
		pthread_mutex_lock(&pAodv->RouteTableMutex);
		pPkt->Hdr.NetLinkType	= pRoute->NetLinkType;
		pPkt->Hdr.NextAddr 		= pRoute->NextHopAddr;
		pthread_mutex_unlock(&pAodv->RouteTableMutex);

		Res = AODV_SendPacket(pAodv, pPkt);
		if (Res != 0)
		{
			pAodv->Stats.DATATxFail++;
		}
		else
		{
			pAodv->Stats.DATATxOkay++;
		}
	}
	else
	{
		dbg_printf("RERR: Route %#x to %#x\n", SrcAddr, DestAddr);

		pAodv->Self.SeqNum++;

		AODV_InitRERR(pAodv, pPkt, DestAddr, SrcAddr);

		Res = AODV_SendPacket(pAodv, pPkt);
		if (Res != 0)
		{
			pAodv->Stats.RERRTxFail++;
		}
		else
		{
			pAodv->Stats.RERRTxOkay++;
		}
	}
}

static void AODV_PrintRouteTable(tAODV *pAodv)
{
	pthread_mutex_lock(&pAodv->RouteTableMutex);
	for (int i = 1; i < AODV_MAX_NETWORK_SIZE; i++)
	{
		dbg_printf("Addr=%#x, Valid=%d, NextHop=%#x, Timestamp=%lu\n",
				i, pAodv->RouteTable[i].Valid,
				pAodv->RouteTable[i].NextHopAddr,
				pAodv->RouteTable[i].Timeout);
	}
	pthread_mutex_unlock(&pAodv->RouteTableMutex);
}

static void AODV_PrintPacket(tAODVPacket *pPkt)
{
	tAODVPacketHdr *pHdr = &pPkt->Hdr;
	dbg_printf("### AODV PACKET ### Type=%d\n"
			"DestAddr=%#x, DestSeqNum=%d, SendAddr=%#x\n"
			"SrcAddr=%#x, SrcSeqNum=%d, NextAddr=%#x\n"
			"Length=%d, HopCnt=%d, BcastSeqNum=%d\n",
			pHdr->PacketType, pHdr->DestAddr, pHdr->DestSeqNum, pHdr->SendAddr,
			pHdr->SrcAddr, pHdr->SrcSeqNum, pHdr->NextAddr,
			pHdr->Length, pHdr->HopCnt, pHdr->BcastSeqNum);
}

static void AODV_SwapPacketEndianness(tAODVPacket *pPkt)
{
	tAODVPacketHdr *pHdr = &pPkt->Hdr;
	pHdr->SrcAddr = bswap16(pHdr->SrcAddr);
	pHdr->SrcSeqNum = bswap16(pHdr->SrcSeqNum);
	pHdr->DestAddr = bswap16(pHdr->DestAddr);
	pHdr->DestSeqNum = bswap16(pHdr->DestSeqNum);
	pHdr->NextAddr = bswap16(pHdr->NextAddr);
	pHdr->SendAddr = bswap16(pHdr->SendAddr);
	pHdr->BcastSeqNum = bswap16(pHdr->BcastSeqNum);
	pHdr->Length = bswap16(pHdr->Length);
}

static bool AODV_IsAddressValid(uint16_t Addr)
{
	return Addr > 0 && Addr < AODV_MAX_NETWORK_SIZE;
}

static bool AODV_HasRoute(tAODV *pAodv, uint16_t Addr)
{
	bool Valid = false;
	if (AODV_IsAddressValid(Addr)) {
		pthread_mutex_lock(&pAodv->RouteTableMutex);
		Valid = pAodv->RouteTable[Addr].Valid;
		pthread_mutex_unlock(&pAodv->RouteTableMutex);
	}
	return Valid;
}

static void AODV_CheckRouteExpiry(tAODV *pAodv)
{
	tAODVRoute *pRoute;
	uint64_t ms = AODV_CurrentTimeMillis();
	pthread_mutex_lock(&pAodv->RouteTableMutex);
	for (int i = 1; i < AODV_MAX_NETWORK_SIZE; i++)
	{
		pRoute = &pAodv->RouteTable[i];
		if (pRoute->Valid)
		{
			if (ms > pRoute->Timeout)
			{
				pRoute->Valid = false;
			}
		}
	}
	pthread_mutex_unlock(&pAodv->RouteTableMutex);
}

static uint8_t AODV_DeviceToNetLink(uint8_t DeviceType)
{
	uint8_t NetLinkType = AODV_NETLINK_NONE;

	switch (DeviceType) {
		case AODV_DEVICE_AND: {
			NetLinkType = AODV_NETLINK_UDP;
			break;
		}
		case AODV_DEVICE_XX: {
			NetLinkType = AODV_NETLINK_XX;
			break;
		}
	}

	return NetLinkType;
}

static uint64_t AODV_CurrentTimeMillis()
{
	struct timespec tm;
	clock_gettime(CLOCK_REALTIME, &tm);
	return ((uint64_t) tm.tv_sec * 1e3) + ((uint64_t) tm.tv_nsec / 1e6);
}

static uint16_t bswap16(uint16_t b)
{
	return (b >> 8) | (b << 8);
}
