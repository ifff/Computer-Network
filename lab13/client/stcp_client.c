//文件名: client/stcp_client.c
//
//描述: 这个文件包含STCP客户端接口实现 
//
//创建日期: 2013年1月

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <assert.h>
#include <strings.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include "../topology/topology.h"
#include "stcp_client.h"
#include "../common/seg.h"

#define MAX_TRANSPORT_sip_connS 10
//声明tcbtable为全局变量
client_tcb_t* tcbtable[MAX_TRANSPORT_sip_connS];
//声明到SIP进程的TCP连接为全局变量
int sip_conn;

/*********************************************************************/
//
//STCP API实现
//
/*********************************************************************/

// 这个函数初始化TCB表, 将所有条目标记为NULL.  
// 它还针对TCP套接字描述符conn初始化一个STCP层的全局变量, 该变量作为sip_sendseg和sip_recvseg的输入参数.
// 最后, 这个函数启动seghandler线程来处理进入的STCP段. 客户端只有一个seghandler.
void stcp_client_init(int conn) {
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

// 创建一个客户端TCB条目, 返回套接字描述符
//
// 这个函数查找客户端TCB表以找到第一个NULL条目, 然后使用malloc()为该条目创建一个新的TCB条目.
// 该TCB中的所有字段都被初始化. 例如, TCB state被设置为CLOSED，客户端端口被设置为函数调用参数client_port. 
// TCB表中条目的索引号应作为客户端的新套接字ID被这个函数返回, 它用于标识客户端的连接. 
// 如果TCB表中没有条目可用, 这个函数返回-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_sock(unsigned int client_port) {
	int i;
	for (i = 0; i < MAX_TRANSPORT_sip_connS; i ++) {
		if (tcbtable[i] == NULL)
			break;
	}
	if (i == MAX_TRANSPORT_sip_connS) return -1;
	tcbtable[i] = (client_tcb_t *)malloc(sizeof(client_tcb_t));
	/* initialize the tcb item */
	tcbtable[i]->server_nodeID = 0;
	tcbtable[i]->server_portNum = 0;
	tcbtable[i]->client_nodeID = 0;
	tcbtable[i]->client_portNum = client_port;
	tcbtable[i]->state = CLOSED;
	tcbtable[i]->next_seqNum = 10;
	tcbtable[i]->bufMutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(tcbtable[i]->bufMutex, NULL);
	tcbtable[i]->sendBufHead = NULL;
	tcbtable[i]->sendBufunSent = NULL;
	tcbtable[i]->sendBufTail = NULL;
	tcbtable[i]->unAck_segNum = 0;
	printf("Client: return sock is %d\n", i);
	return i;
}

// 连接STCP服务器
//
// 这个函数用于连接服务器. 它以套接字ID和服务器的端口号作为输入参数. 套接字ID用于找到TCB条目.  
// 这个函数设置TCB的服务器端口号,  然后使用sip_sendseg()发送一个SYN段给服务器.  
// 在发送了SYN段之后, 一个定时器被启动. 如果在SYNSEG_TIMEOUT时间之内没有收到SYNACK, SYN 段将被重传. 
// 如果收到了, 就返回1. 否则, 如果重传SYN的次数大于SYN_MAX_RETRY, 就将state转换到CLOSED, 并返回-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_connect(int sockfd, int nodeID, unsigned int server_port)
{	
	int resend_syn_times = 0;
	seg_t *segPtr1;
	tcbtable[sockfd]->server_portNum = server_port;
	tcbtable[sockfd]->server_nodeID = nodeID;
	segPtr1 = (seg_t *)malloc(sizeof(seg_t));
	segPtr1->header.src_port = tcbtable[sockfd]->client_portNum;
	segPtr1->header.dest_port = tcbtable[sockfd]->server_portNum;
	segPtr1->header.seq_num = tcbtable[sockfd]->next_seqNum;
	segPtr1->header.ack_num = 0;
	segPtr1->header.length = sizeof(seg_t);//???
	segPtr1->header.type = SYN;
	segPtr1->header.rcv_win = 0;
	segPtr1->header.checksum = 0;
	sip_sendseg(sip_conn, nodeID, segPtr1);
	printf("Client(SYNSENT): send a SYN segment data(src_port: %d, dest_port: %d)\n", segPtr1->header.src_port, segPtr1->header.dest_port);
	tcbtable[sockfd]->state = SYNSENT;
	// set timer and check state to confirm the ack
	while (resend_syn_times < SYN_MAX_RETRY ) {
		  //usleep(SYN_TIMEOUT / 1000);
		  sleep(1);
		if (tcbtable[sockfd]->state == CONNECTED) {
			return 1;
		}
		printf("Client(SYNSENT): Resend a SYN segment data(src_port: %d, dest_port: %d)\n", 
		segPtr1->header.src_port, segPtr1->header.dest_port);
		sip_sendseg(sip_conn, nodeID, segPtr1);
		resend_syn_times ++;
	}
	tcbtable[sockfd]->state = CLOSED;
	printf("Client(CLOSED): after send SYN(src_port: %d, dest_port: %d), cann't receive SYNACK \n",
	segPtr1->header.src_port, segPtr1->header.dest_port);
	return -1;
}

// 发送数据给STCP服务器
//

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//int seq_num = 10;
int stcp_client_send(int sockfd, void* data, unsigned int length) {
	printf("Client starts to send data to server...\n\n");
	if (tcbtable[sockfd]->state == CONNECTED) {
		char *p = (char *)data;
		while (length / MAX_SEG_LEN > 0) {
			segBuf_t *segbuf = (segBuf_t *)malloc(sizeof(segBuf_t));
			segbuf->seg.header.src_port = tcbtable[sockfd]->client_portNum;
			segbuf->seg.header.dest_port = tcbtable[sockfd]->server_portNum;
			segbuf->seg.header.seq_num = tcbtable[sockfd]->next_seqNum;	//??
			tcbtable[sockfd]->next_seqNum ++;
			segbuf->seg.header.ack_num = 0; // ??
			segbuf->seg.header.length = MAX_SEG_LEN;
			segbuf->seg.header.type = DATA;
			segbuf->seg.header.rcv_win = 0;
			segbuf->seg.header.checksum = 0;	// ??
			memcpy(segbuf->seg.data, p, MAX_SEG_LEN);
			p = p + MAX_SEG_LEN;
			length -= MAX_SEG_LEN;
			time_t current;
			time(&current);
			segbuf->sentTime = (int)current;	// ??
			segbuf->next = NULL;
			// 添加到segBuf链表中
			if (tcbtable[sockfd]->sendBufHead == NULL) {	// 链表为空
				pthread_mutex_lock(tcbtable[sockfd]->bufMutex);
				tcbtable[sockfd]->sendBufTail = segbuf;
				tcbtable[sockfd]->sendBufHead = segbuf;
				tcbtable[sockfd]->sendBufunSent = segbuf;
				pthread_mutex_unlock(tcbtable[sockfd]->bufMutex);
				pthread_t thread;	// 创建线程
				int *param = (int *)malloc(sizeof(int));
				*param = sockfd;
				int rc = pthread_create(&thread,NULL,sendBuf_timer,(void *)param);
				if (rc) {
					printf("create thread for sendBuf_timer error!\n");
				}
			}
			else {	// 添加到链表尾
				pthread_mutex_lock(tcbtable[sockfd]->bufMutex);
				if (tcbtable[sockfd]->sendBufunSent == NULL) {
					tcbtable[sockfd]->sendBufunSent = segbuf;
				}
				tcbtable[sockfd]->sendBufTail->next = segbuf;
				tcbtable[sockfd]->sendBufTail = segbuf;
				pthread_mutex_unlock(tcbtable[sockfd]->bufMutex);
			}
		}
		// 长度不到MAX_SEG_LEN的部分
		segBuf_t *segbuf = (segBuf_t *)malloc(sizeof(segBuf_t));
		segbuf->seg.header.src_port = tcbtable[sockfd]->client_portNum;
		segbuf->seg.header.dest_port = tcbtable[sockfd]->server_portNum;
		segbuf->seg.header.seq_num = tcbtable[sockfd]->next_seqNum;	//??
		tcbtable[sockfd]->next_seqNum ++;
		segbuf->seg.header.ack_num = 0; // ??
		segbuf->seg.header.length = MAX_SEG_LEN;
		segbuf->seg.header.type = DATA;
		segbuf->seg.header.rcv_win = 0;
		segbuf->seg.header.checksum = 0;	// ??
		memcpy(segbuf->seg.data, p, length);
		time_t current;
		time(&current);
		segbuf->sentTime = (int)current;
		segbuf->next = NULL;
		// 添加到segBuf链表中
		pthread_mutex_lock(tcbtable[sockfd]->bufMutex);
		if (tcbtable[sockfd]->sendBufHead == NULL) {	// 链表为空
			//pthread_mutex_lock(tcbtable[sockfd]->bufMutex);
			tcbtable[sockfd]->sendBufTail = segbuf;
			tcbtable[sockfd]->sendBufHead = segbuf;
			tcbtable[sockfd]->sendBufunSent = segbuf;
			//pthread_mutex_unlock(tcbtable[sockfd]->bufMutex);
			pthread_t thread;	// 创建线程
			printf("create thread for function sendBuf_timer\n");
			int *param = (int *)malloc(sizeof(int));
			*param = sockfd;
			int rc = pthread_create(&thread,NULL,sendBuf_timer,(void *)param);
			if (rc) {
				printf("create thread for sendBuf_timer error!\n");
			}
		}
		else {	// 添加到链表尾
			if (tcbtable[sockfd]->sendBufunSent == NULL) {
				tcbtable[sockfd]->sendBufunSent = segbuf;
			}
			tcbtable[sockfd]->sendBufTail->next = segbuf;
			tcbtable[sockfd]->sendBufTail = segbuf;
		}
		//  发送数据段直到已发送但未被确认的段数量到达GBN_WINDOW为止 
		//pthread_mutex_lock(tcbtable[sockfd]->bufMutex);

		int ack_sum = 0;
		segBuf_t *q = tcbtable[sockfd]->sendBufHead;
		for (;q != tcbtable[sockfd]->sendBufunSent; q = q->next)
			ack_sum ++;
		while (ack_sum < GBN_WINDOW) {
			if (tcbtable[sockfd]->sendBufunSent == NULL) break;
			sip_sendseg(sip_conn, tcbtable[sockfd]->server_nodeID, &tcbtable[sockfd]->sendBufunSent->seg);
			ack_sum ++;
		//	printf("Client(CONNECTED):Send a data to server(seq_num is %d, src_port is %d, dest_port is %d)\n",
			//tcbtable[sockfd]->sendBufunSent->seg.header.seq_num,tcbtable[sockfd]->sendBufunSent->seg.header.src_port,
			//tcbtable[sockfd]->sendBufunSent->seg.header.dest_port);
			tcbtable[sockfd]->sendBufunSent = tcbtable[sockfd]->sendBufunSent->next;
		}
		pthread_mutex_unlock(tcbtable[sockfd]->bufMutex);
		return 1;
	}
	else 
		return -1;
}

// 断开到STCP服务器的连接
//
// 这个函数用于断开到服务器的连接. 它以套接字ID作为输入参数. 套接字ID用于找到TCB表中的条目.  
// 这个函数发送FIN segment给服务器. 在发送FIN之后, state将转换到FINWAIT, 并启动一个定时器.
// 如果在最终超时之前state转换到CLOSED, 则表明FINACK已被成功接收. 否则, 如果在经过FIN_MAX_RETRY次尝试之后,
// state仍然为FINWAIT, state将转换到CLOSED, 并返回-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_disconnect(int sockfd) {
	int resend_fin_times = 0;
	seg_t *segPtr2;
	segPtr2 = (seg_t *)malloc(sizeof(seg_t));
	segPtr2->header.src_port = tcbtable[sockfd]->client_portNum;
	segPtr2->header.dest_port = tcbtable[sockfd]->server_portNum;
	segPtr2->header.seq_num = 0;
	segPtr2->header.ack_num = 0;
	segPtr2->header.length = sizeof(seg_t);//???
	segPtr2->header.type = FIN;
	segPtr2->header.rcv_win = 0;
	segPtr2->header.checksum = 0;
	sip_sendseg(sip_conn, tcbtable[sockfd]->server_nodeID, segPtr2);
	printf("Client(FINWAIT): send a FIN segment data(src_port: %d, dest_port: %d)\n",segPtr2->header.src_port,segPtr2->header.dest_port);
	tcbtable[sockfd]->state = FINWAIT;
	// set timer and check state
	while (resend_fin_times < FIN_MAX_RETRY) {
		usleep(FIN_TIMEOUT/1000);
		if (tcbtable[sockfd]->state == CLOSED) {
			return 1;
		}
		sip_sendseg(sip_conn, tcbtable[sockfd]->server_nodeID, segPtr2);
		printf("Client(FINWAIT): Resend a FIN segment data(src_port: %d, dest_port: %d)\n",
		segPtr2->header.src_port,segPtr2->header.dest_port);
		resend_fin_times ++;
	}
	tcbtable[sockfd]->state = CLOSED;
	printf("Client(CLOSED): cann't receive FINACK after send FIN(src_port: %d, dest_port: %d)\n",
	segPtr2->header.src_port,segPtr2->header.dest_port);
	// 清空发送缓冲区
	pthread_mutex_lock(tcbtable[sockfd]->bufMutex);
	segBuf_t *p = tcbtable[sockfd]->sendBufHead;
	while (p != NULL) {
		segBuf_t *q = p;
		p = p->next;
		free(q);
	}
	tcbtable[sockfd]->sendBufHead = NULL;
	tcbtable[sockfd]->sendBufunSent = NULL;
	tcbtable[sockfd]->sendBufTail = NULL;
	pthread_mutex_unlock(tcbtable[sockfd]->bufMutex);
	return -1;
}

// 关闭STCP客户
//
// 这个函数调用free()释放TCB条目. 它将该条目标记为NULL, 成功时(即位于正确的状态)返回1,
// 失败时(即位于错误的状态)返回-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_close(int sockfd) {
	free(tcbtable[sockfd]->bufMutex);
	tcbtable[sockfd]->bufMutex = NULL;
	free(tcbtable[sockfd]);
	tcbtable[sockfd] = NULL;
	return 1;
}

// 处理进入段的线程
//
// 这是由stcp_client_init()启动的线程. 它处理所有来自服务器的进入段. 
// seghandler被设计为一个调用sip_recvseg()的无穷循环. 如果sip_recvseg()失败, 则说明重叠网络连接已关闭,
// 线程将终止. 根据STCP段到达时连接所处的状态, 可以采取不同的动作. 请查看客户端FSM以了解更多细节.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

void *seghandler(void* arg) {
	seg_t *segPtr = (seg_t*)malloc(sizeof(seg_t));
	int *src_nodeID = (int *)malloc(sizeof(int));
	while (1) {
		//printf("before count\n");
		int count = sip_recvseg(sip_conn, src_nodeID, segPtr);
		if ( count > 0) {
			unsigned short int type = segPtr->header.type;
			printf("-----------seghandler header.type is %d, src_nodeId is %d--------\n", segPtr->header.type, *src_nodeID);
			// 查找该数据段的TCB
			unsigned int client_port = segPtr->header.dest_port;
			int sockfd;
			for (sockfd = 0; sockfd < MAX_TRANSPORT_sip_connS; sockfd ++) {
				if (tcbtable[sockfd] != NULL) {
					if (tcbtable[sockfd]->client_portNum == client_port) {
						break;
					}
				}
			}
			// client FSM
			switch(tcbtable[sockfd]->state) {
				case SYNSENT:
				if (type == SYNACK) {
					tcbtable[sockfd]->state = CONNECTED;
					printf("Client(SYNSENT): Get a SYNACK, state changes: SYNSENT---->CONNECTED\n");
				}
				break;
				case CONNECTED:
					if (type == DATAACK) {
						int ack_num = segPtr->header.ack_num;	// !!!!!!!!!!!!!!!
						printf("Client: Get a DATAACK from Server(expect_num is %d)\n", ack_num);
						pthread_mutex_lock(tcbtable[sockfd]->bufMutex);
						segBuf_t *q = tcbtable[sockfd]->sendBufHead;
						segBuf_t *p = NULL;
						while (q != tcbtable[sockfd]->sendBufunSent) { //释放发送缓冲区
							if (q->seg.header.seq_num < ack_num) {	
								p = q;
								q = q->next;
								free(p);
							}
							else 
								break;
						}
						tcbtable[sockfd]->sendBufHead = q;
						//  发送数据段直到已发送但未被确认的段数量到达GBN_WINDOW为止 
						int ack_sum = 0;
						segBuf_t *q1 = tcbtable[sockfd]->sendBufHead;
						for (;q1 != tcbtable[sockfd]->sendBufunSent && q1 != NULL; q1 = q1->next)
							ack_sum ++;
	
						while (ack_sum < GBN_WINDOW) {
							if (tcbtable[sockfd]->sendBufunSent == NULL) break;
							printf("Client:send data to server(seq_num is %d)\n", tcbtable[sockfd]->sendBufunSent->seg.header.seq_num);
							sip_sendseg(sip_conn, *src_nodeID, &tcbtable[sockfd]->sendBufunSent->seg);
							ack_sum ++;
							tcbtable[sockfd]->sendBufunSent = tcbtable[sockfd]->sendBufunSent->next;
						}
						//printf("ack_sum is %d-----------------------------\n", ack_sum);
						pthread_mutex_unlock(tcbtable[sockfd]->bufMutex);
					}
				break;
				case FINWAIT:
				if (type == FINACK) {
					tcbtable[sockfd]->state = CLOSED;
					printf("Client(FINWAIT): GET a FINACK, state changes: FINWAIT----->CLOSED\n");
				}
				break;
				
			}
		}
		else if (count == 0);	// 段数据丢失
		else		// TCP 连接断开
			break;
	}
	printf("------------------client seghandler exit----------------\n");
	free(segPtr);
	segPtr = NULL;
	pthread_exit(0);
}





// ÕâžöÏß³Ì³ÖÐøÂÖÑ¯·¢ËÍ»º³åÇøÒÔŽ¥·¢³¬Ê±ÊÂŒþ. Èç¹û·¢ËÍ»º³åÇø·Ç¿Õ, ËüÓŠÒ»Ö±ÔËÐÐ.
// Èç¹û(µ±Ç°Ê±Œä - µÚÒ»žöÒÑ·¢ËÍµ«ÎŽ±»È·ÈÏ¶ÎµÄ·¢ËÍÊ±Œä) > DATA_TIMEOUT, ŸÍ·¢ÉúÒ»ŽÎ³¬Ê±ÊÂŒþ.
// µ±³¬Ê±ÊÂŒþ·¢ÉúÊ±, ÖØÐÂ·¢ËÍËùÓÐÒÑ·¢ËÍµ«ÎŽ±»È·ÈÏ¶Î. µ±·¢ËÍ»º³åÇøÎª¿ÕÊ±, ÕâžöÏß³Ìœ«ÖÕÖ¹.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//


void* sendBuf_timer(void* clienttcb)
{
	// 设置定时器
	int sockfd = *(int *)clienttcb;
	while (1) {
		if (tcbtable[sockfd]->state == CONNECTED && tcbtable[sockfd]->sendBufHead != NULL) {
			// set timer 
			time_t now;
			time(&now);
			//printf("-----------------------sendBuf_timer----------------\n");
	       // printf("now is %d, interval is %d,sentTime is %d\n", (int)now,(int)now - tcbtable[sockfd]->sendBufHead->sentTime,tcbtable[sockfd]->sendBufHead->sentTime);
	        pthread_mutex_lock(tcbtable[sockfd]->bufMutex);
   	        if (((int)now - tcbtable[sockfd]->sendBufHead->sentTime) >= 1) {
		       printf("Timeout and resend data\n");
		       segBuf_t *q = tcbtable[sockfd]->sendBufHead;
		       for (;q != tcbtable[sockfd]->sendBufunSent; q = q->next) {
				   //printf("Client send data to server(seq_num is %d)\n", q->seg.header.seq_num);
			       sip_sendseg(sip_conn, tcbtable[sockfd]->server_nodeID, &q->seg);
			     //  q->sentTime = clock();
				   }
	         }
            pthread_mutex_unlock(tcbtable[sockfd]->bufMutex);
			usleep(SENDBUF_POLLING_INTERVAL/1000000);
		}
		else {
			break;
		}
	}
	//printf("-------------------------timer exit(1)-------------------\n");
  	pthread_exit(NULL);
}