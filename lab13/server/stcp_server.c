//文件名: server/stcp_server.c
//
//描述: 这个文件包含STCP服务器接口实现. 
//
//创建日期: 2013年1月

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
//声明tcbtable为全局变量
server_tcb_t* tcbtable[MAX_TRANSPORT_sip_connS];
//声明到SIP进程的连接为全局变量
int sip_conn;

/*********************************************************************/
//
//STCP API实现
//
/*********************************************************************/

// 这个函数初始化TCB表, 将所有条目标记为NULL. 它还针对TCP套接字描述符conn初始化一个STCP层的全局变量, 
// 该变量作为sip_sendseg和sip_recvseg的输入参数. 最后, 这个函数启动seghandler线程来处理进入的STCP段.
// 服务器只有一个seghandler.
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

// 创建服务器套接字
//
// 这个函数查找服务器TCB表以找到第一个NULL条目, 然后使用malloc()为该条目创建一个新的TCB条目.
// 该TCB中的所有字段都被初始化, 例如, TCB state被设置为CLOSED, 服务器端口被设置为函数调用参数server_port. 
// TCB表中条目的索引应作为服务器的新套接字ID被这个函数返回, 它用于标识服务器的连接. 
// 如果TCB表中没有条目可用, 这个函数返回-1.

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
	pthread_mutex_init(tcbtable[i]->bufMutex, NULL);	// 初始化互斥锁
	printf("Server: in function stcp_server_sock, sock is %d\n", i);
	return i;
}

// 接受来自STCP客户端的连接
//
// 这个函数使用sockfd获得TCB指针, 并将连接的state转换为LISTENING. 它然后进入忙等待(busy wait)直到TCB状态转换为CONNECTED 
// (当收到SYN时, seghandler会进行状态的转换). 该函数在一个无穷循环中等待TCB的state转换为CONNECTED,  
// 当发生了转换时, 该函数返回1. 你可以使用不同的方法来实现这种阻塞等待.
//

int stcp_server_accept(int sockfd) {
	tcbtable[sockfd]->state = LISTENING;
	while (tcbtable[sockfd]->state != CONNECTED);
	return 1;
}

// 接收来自STCP客户端的数据
//
int stcp_server_recv(int sockfd, void* buf, unsigned int length) {
	printf("Server starts to receive data from client...\n\n");
	if (length <= tcbtable[sockfd]->usedBufLen) {	// 缓冲区包含足够的数据
		pthread_mutex_lock(tcbtable[sockfd]->bufMutex);
		memcpy((char*)buf, tcbtable[sockfd]->recvBuf, length);
		//更新缓冲区
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
				//更新缓冲区
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

// 关闭STCP服务器
//
// 这个函数调用free()释放TCB条目. 它将该条目标记为NULL, 成功时(即位于正确的状态)返回1,
// 失败时(即位于错误的状态)返回-1.
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

// 处理进入段的线程
//
// 这是由stcp_server_init()启动的线程. 它处理所有来自客户端的进入数据. seghandler被设计为一个调用sip_recvseg()的无穷循环, 
// 如果sip_recvseg()失败, 则说明重叠网络连接已关闭, 线程将终止. 根据STCP段到达时连接所处的状态, 可以采取不同的动作.
// 请查看服务端FSM以了解更多细节.
//

void *seghandler(void* arg) {
	seg_t *segPtr = (seg_t*)malloc(sizeof(seg_t));
	while(1) {
		int *src_nodeID = (int *)malloc(sizeof(int));
		int count = sip_recvseg(sip_conn, src_nodeID, segPtr);
		printf("src_nodeID is %d\n", *src_nodeID);
		if (count > 0) {
			unsigned short int type = segPtr->header.type;
			// 查找该数据段的TCB
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
					tcbtable[sockfd]->expect_seqNum = segPtr->header.seq_num;	//设置server的expect_seqNum
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
						if (tcbtable[sockfd]->expect_seqNum == seq_num) {	// 序号和expect_seq_num相同
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
		else if (count == 0);	// 段数据丢失
		else break;	// TCP连接断开
	}
	pthread_exit(0);
}
