//�ļ���: server/stcp_server.c
//
//����: ����ļ�����STCP�������ӿ�ʵ��. 
//
//��������: 2013��1��

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/select.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "stcp_server.h"
#include "../topology/topology.h"
#include "../common/constants.h"

#define MAX_TRANSPORT_sip_connS 10
//����tcbtableΪȫ�ֱ���
server_tcb_t* tcbtable[MAX_TRANSPORT_sip_connS];
//������SIP���̵�����Ϊȫ�ֱ���
int sip_conn;

/*********************************************************************/
//
//STCP APIʵ��
//
/*********************************************************************/

// ���������ʼ��TCB��, ��������Ŀ���ΪNULL. �������TCP�׽���������conn��ʼ��һ��STCP���ȫ�ֱ���, 
// �ñ�����Ϊsip_sendseg��sip_recvseg���������. ���, �����������seghandler�߳�����������STCP��.
// ������ֻ��һ��seghandler.
void stcp_server_init(int conn) {
	int i;
	for (i = 0; i < MAX_TRANSPORT_sip_connS; i ++) {
		tcbtable[i] = NULL;
	}
	sip_conn = conn;
	pthread_t thread;
	int rc = pthread_create(&thread,NULL,seghandler,NULL);
	if (rc) {
		printf("create thread for seghandler error!\n");
	}
  	return;
}

// �����������׽���
//
// ����������ҷ�����TCB�����ҵ���һ��NULL��Ŀ, Ȼ��ʹ��malloc()Ϊ����Ŀ����һ���µ�TCB��Ŀ.
// ��TCB�е������ֶζ�����ʼ��, ����, TCB state������ΪCLOSED, �������˿ڱ�����Ϊ�������ò���server_port. 
// TCB������Ŀ������Ӧ��Ϊ�����������׽���ID�������������, �����ڱ�ʶ������������. 
// ���TCB����û����Ŀ����, �����������-1.

int stcp_server_sock(unsigned int server_port) {
	int i;
	for (i = 0; i < MAX_TRANSPORT_sip_connS; i ++) {
		if (tcbtable[i] == NULL)
			break;
	}
	if (i == MAX_TRANSPORT_sip_connS) return -1;
	tcbtable[i] = (server_tcb_t *)malloc(sizeof(server_tcb_t));
	/* initialize the tcb item */
	tcbtable[i]->server_nodeID = 0;
	tcbtable[i]->server_portNum = server_port;
	tcbtable[i]->client_nodeID = 0;
	tcbtable[i]->client_portNum = 0;
	tcbtable[i]->state = CLOSED;
	tcbtable[i]->expect_seqNum = 10;
	tcbtable[i]->recvBuf = (char *)malloc(RECEIVE_BUF_SIZE);
	tcbtable[i]->usedBufLen = 0;
	tcbtable[i]->bufMutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(tcbtable[i]->bufMutex, NULL);	// ��ʼ��������
	printf("Server: in function stcp_server_sock, sock is %d\n", i);
	return i;
}

// ��������STCP�ͻ��˵�����
//
// �������ʹ��sockfd���TCBָ��, �������ӵ�stateת��ΪLISTENING. ��Ȼ�����æ�ȴ�(busy wait)ֱ��TCB״̬ת��ΪCONNECTED 
// (���յ�SYNʱ, seghandler�����״̬��ת��). �ú�����һ������ѭ���еȴ�TCB��stateת��ΪCONNECTED,  
// ��������ת��ʱ, �ú�������1. �����ʹ�ò�ͬ�ķ�����ʵ�����������ȴ�.
//

int stcp_server_accept(int sockfd) {
	tcbtable[sockfd]->state = LISTENING;
	while (tcbtable[sockfd]->state != CONNECTED);
	return 1;
}

// ��������STCP�ͻ��˵�����
//
int stcp_server_recv(int sockfd, void* buf, unsigned int length) {
	printf("Server starts to receive data from client...\n\n");
	if (length <= tcbtable[sockfd]->usedBufLen) {	// �����������㹻������
		pthread_mutex_lock(tcbtable[sockfd]->bufMutex);
		memcpy((char*)buf, tcbtable[sockfd]->recvBuf, length);
		//���»�����
		int i;
		for (i = 0; i < (tcbtable[sockfd]->usedBufLen - MAX_SEG_LEN); i ++) {
			tcbtable[sockfd]->recvBuf[i] = tcbtable[sockfd]->recvBuf[i + MAX_SEG_LEN];
		}
		tcbtable[sockfd]->usedBufLen -= MAX_SEG_LEN;
		pthread_mutex_unlock(tcbtable[sockfd]->bufMutex);
	}
	else {
		int checkFlag = 0;
		while (checkFlag == 0) {
			sleep(RECVBUF_POLLING_INTERVAL);
			pthread_mutex_lock(tcbtable[sockfd]->bufMutex);
			if (length <= tcbtable[sockfd]->usedBufLen) {
				memcpy((char*)buf, tcbtable[sockfd]->recvBuf, length);
				//���»�����
				int removeLength = (length / MAX_SEG_LEN + 1) * MAX_SEG_LEN;
				int i;
				for (i = 0; i < (tcbtable[sockfd]->usedBufLen - removeLength); i ++) {
					tcbtable[sockfd]->recvBuf[i] = tcbtable[sockfd]->recvBuf[i + removeLength];
				}
				tcbtable[sockfd]->usedBufLen -= removeLength;
				checkFlag = 1;
			}
			pthread_mutex_unlock(tcbtable[sockfd]->bufMutex);			
		}
	}
	return 1;
}

// �ر�STCP������
//
// �����������free()�ͷ�TCB��Ŀ. ��������Ŀ���ΪNULL, �ɹ�ʱ(��λ����ȷ��״̬)����1,
// ʧ��ʱ(��λ�ڴ����״̬)����-1.
//

int stcp_server_close(int sockfd) {
	if (tcbtable[sockfd]->state != CLOSED) {
		sleep(CLOSEWAIT_TIMEOUT);
		tcbtable[sockfd]->state = CLOSED;
		tcbtable[sockfd]->usedBufLen = 0;
		printf("(Server)state changes: CLOSEWAIT--->CLOSED\n"); 
	}
	free(tcbtable[sockfd]->recvBuf);
	tcbtable[sockfd]->recvBuf = NULL;
	free(tcbtable[sockfd]->bufMutex);
	tcbtable[sockfd]->bufMutex = NULL;
	free(tcbtable[sockfd]);
	tcbtable[sockfd] = NULL;
	return 1;
}

// �������ε��߳�
//
// ������stcp_server_init()�������߳�. �������������Կͻ��˵Ľ�������. seghandler�����Ϊһ������sip_recvseg()������ѭ��, 
// ���sip_recvseg()ʧ��, ��˵���ص����������ѹر�, �߳̽���ֹ. ����STCP�ε���ʱ����������״̬, ���Բ�ȡ��ͬ�Ķ���.
// ��鿴�����FSM���˽����ϸ��.
//

void *seghandler(void* arg) {
	seg_t *segPtr = (seg_t*)malloc(sizeof(seg_t));
	while(1) {
		int *src_nodeID = (int *)malloc(sizeof(int));
		int count = sip_recvseg(sip_conn, src_nodeID, segPtr);
		printf("src_nodeID is %d\n", *src_nodeID);
		if (count > 0) {
			unsigned short int type = segPtr->header.type;
			// ���Ҹ����ݶε�TCB
			unsigned int server_port = segPtr->header.dest_port;
			int sockfd;
			for (sockfd = 0; sockfd < MAX_TRANSPORT_sip_connS; sockfd ++) {
				if (tcbtable[sockfd] != NULL) {
					if (tcbtable[sockfd]->server_portNum == server_port) {
						break;
					}
				}
			}
			// server FSM
			switch(tcbtable[sockfd]->state) {
				case LISTENING:  	// receive syn and send synack
				if (type == SYN) {
					tcbtable[sockfd]->client_portNum = segPtr->header.src_port;	// update client_portNum
					seg_t *synack = (seg_t *)malloc(sizeof(seg_t));
					synack->header.src_port = tcbtable[sockfd]->server_portNum;
					synack->header.dest_port = tcbtable[sockfd]->client_portNum;
					synack->header.seq_num = 0;
					synack->header.ack_num = 0;
					synack->header.length = 0;
					synack->header.type = SYNACK;
					synack->header.rcv_win = 0;
					synack->header.checksum = 0;
					sip_sendseg(sip_conn, *src_nodeID, synack);   // send synack
					printf("Server(CONNECTED): receive a SYN from client and resend a SYNACK(dest_nodeID: %d)\n", *src_nodeID); 
					tcbtable[sockfd]->state = CONNECTED;	// change the state
					printf("state changes: LISTENING--->CONNECTED\n");
				}
				break;
				case CONNECTED:
				if (type == SYN) {
					tcbtable[sockfd]->expect_seqNum = segPtr->header.seq_num;	//����server��expect_seqNum
					seg_t *synack = (seg_t *)malloc(sizeof(seg_t));
					synack->header.src_port = tcbtable[sockfd]->server_portNum;
					synack->header.dest_port = tcbtable[sockfd]->client_portNum;
					synack->header.seq_num = 0;
					synack->header.ack_num = 0;
					synack->header.length = 0;
					synack->header.type = SYNACK;
					synack->header.rcv_win = 0;
					synack->header.checksum = 0;
					sip_sendseg(sip_conn, *src_nodeID, synack);   // send synack
					printf("Server(CONNECTED): receive a SYN from client and resend a SYNACK(dest_nodeID: %d)\n", *src_nodeID); 
				}
				else if (type == FIN) {		// receive fin and send finack
					seg_t *finack = (seg_t *)malloc(sizeof(seg_t));
					finack->header.src_port = tcbtable[sockfd]->server_portNum;
					finack->header.dest_port = tcbtable[sockfd]->client_portNum;
					finack->header.seq_num = 0;
					finack->header.ack_num = 0;
					finack->header.length = 0;
					finack->header.type = FINACK;
					finack->header.rcv_win = 0;
					finack->header.checksum = 0;
					sip_sendseg(sip_conn, *src_nodeID, finack);   // send FINACK
					printf("Server(CONNECTED): receive a FIN from client and send a FINACK(src_port: %d, dest_port: %d)\n",finack->header.src_port, finack->header.dest_port);
					tcbtable[sockfd]->state = CLOSEWAIT;	// change the state
					printf("state changes: CONNECTED--->CLOSEWAIT\n");
				}
				else if (type == DATA) {
					if (tcbtable[sockfd]->usedBufLen + MAX_SEG_LEN < RECEIVE_BUF_SIZE) {
						int seq_num = segPtr->header.seq_num;
						printf("Server get DATA(seq_num is %d), expect_num is %d\n",seq_num,tcbtable[sockfd]->expect_seqNum);
						if (tcbtable[sockfd]->expect_seqNum == seq_num) {	// ��ź�expect_seq_num��ͬ
							pthread_mutex_lock(tcbtable[sockfd]->bufMutex);
							memcpy(tcbtable[sockfd]->recvBuf + tcbtable[sockfd]->usedBufLen, segPtr->data,MAX_SEG_LEN);
							tcbtable[sockfd]->usedBufLen += MAX_SEG_LEN;
							pthread_mutex_unlock(tcbtable[sockfd]->bufMutex);
							tcbtable[sockfd]->expect_seqNum ++;
							// send DATAACK
							seg_t *dataack = (seg_t *)malloc(sizeof(seg_t));
							dataack->header.src_port = tcbtable[sockfd]->server_portNum;
							dataack->header.dest_port = tcbtable[sockfd]->client_portNum;
							dataack->header.seq_num = 0;
							dataack->header.ack_num = tcbtable[sockfd]->expect_seqNum;
							dataack->header.length = 0; 	// ?
							dataack->header.type = DATAACK;
							dataack->header.rcv_win = 0;
							dataack->header.checksum = 0;	//?
							sip_sendseg(sip_conn, *src_nodeID, dataack);
							printf("Server(CONNECTED): receive a DATA(seq_num is %d) from client and send a DATAACK(src_port: %d, dest_port: %d, expect_num is %d)\n",seq_num, dataack->header.src_port, dataack->header.dest_port, tcbtable[sockfd]->expect_seqNum);
						}
						else {
							// send DATAACK
							seg_t *dataack = (seg_t *)malloc(sizeof(seg_t));
							dataack->header.src_port = tcbtable[sockfd]->server_portNum;
							dataack->header.dest_port = tcbtable[sockfd]->client_portNum;
							dataack->header.seq_num = 0;
							dataack->header.ack_num = tcbtable[sockfd]->expect_seqNum;
							dataack->header.length = 0; 	// ?
							dataack->header.type = DATAACK;
							dataack->header.rcv_win = 0;
							dataack->header.checksum = 0;	//?
							sip_sendseg(sip_conn, *src_nodeID, dataack);
							printf("Server(CONNECTED): send a DATAACK(expect_seqNum: %d,src_port: %d, dest_port: %d)\n",tcbtable[sockfd]->expect_seqNum, dataack->header.src_port, dataack->header.dest_port);
						}
					}
				}
				break;
				case CLOSEWAIT:
				if (type == FIN) {		// receive fin and send finack
					seg_t *finack = (seg_t *)malloc(sizeof(seg_t));
					finack->header.src_port = tcbtable[sockfd]->server_portNum;
					finack->header.dest_port = tcbtable[sockfd]->client_portNum;
					finack->header.seq_num = 0;
					finack->header.ack_num = 0;
					finack->header.length = 0;
					finack->header.type = FINACK;
					finack->header.rcv_win = 0;
					finack->header.checksum = 0;
					sip_sendseg(sip_conn, *src_nodeID, finack);   // send FINACK
				printf("Server(CLOSEWAIT):receive a FIN from client and resend a FINACK(src_port: %d, dest_port: %d)\n",
					finack->header.src_port, finack->header.dest_port);
				}
				break;
			}
		}
		else if (count == 0);	// �����ݶ�ʧ
		else break;	// TCP���ӶϿ�
	}
	pthread_exit(0);
}
