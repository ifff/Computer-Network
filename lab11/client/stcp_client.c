#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include "stcp_client.h"
#include <unistd.h>
#include <signal.h>
#include <string.h>
/*面向应用层的接口*/

//
//  我们在下面提供了每个函数调用的原型定义和细节说明, 但这些只是指导性的, 你完全可以根据自己的想法来设计代码.
//
//  注意: 当实现这些函数时, 你需要考虑FSM中所有可能的状态, 这可以使用switch语句来实现.
//
//  目标: 你的任务就是设计并实现下面的函数原型.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// stcp客户端初始化
//
// 这个函数初始化TCB表, 将所有条目标记为NULL.  
// 它还针对重叠网络TCP套接字描述符conn初始化一个STCP层的全局变量, 该变量作为sip_sendseg和sip_recvseg的输入参数.
// 最后, 这个函数启动seghandler线程来处理进入的STCP段. 客户端只有一个seghandler.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
client_tcb_t *tcb_table[MAX_TRANSPORT_CONNECTIONS];
int connection;
void stcp_client_init(int conn) {
	int i;
	for (i = 0; i < MAX_TRANSPORT_CONNECTIONS; i ++) {
		tcb_table[i] = NULL;
	}
	connection = conn;
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
	for (i = 0; i < MAX_TRANSPORT_CONNECTIONS; i ++) {
		if (tcb_table[i] == NULL)
			break;
	}
	if (i == MAX_TRANSPORT_CONNECTIONS) return -1;
	tcb_table[i] = (client_tcb_t *)malloc(sizeof(client_tcb_t));
	/* initialize the tcb item */
	tcb_table[i]->server_nodeID = 0;
	tcb_table[i]->server_portNum = 0;
	tcb_table[i]->client_nodeID = 0;
	tcb_table[i]->client_portNum = client_port;
	tcb_table[i]->state = CLOSED;
	tcb_table[i]->next_seqNum = 10;
	tcb_table[i]->bufMutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(tcb_table[i]->bufMutex, NULL);
	tcb_table[i]->sendBufHead = NULL;
	tcb_table[i]->sendBufunSent = NULL;
	tcb_table[i]->sendBufTail = NULL;
	tcb_table[i]->unAck_segNum = 0;
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

int stcp_client_connect(int sockfd, unsigned int server_port) {
	int resend_syn_times = 0;
	seg_t *segPtr1;
	tcb_table[sockfd]->server_portNum = server_port;
	segPtr1 = (seg_t *)malloc(sizeof(seg_t));
	segPtr1->header.src_port = tcb_table[sockfd]->client_portNum;
	segPtr1->header.dest_port = tcb_table[sockfd]->server_portNum;
	segPtr1->header.seq_num = tcb_table[sockfd]->next_seqNum;
	segPtr1->header.ack_num = 0;
	segPtr1->header.length = sizeof(seg_t);//???
	segPtr1->header.type = SYN;
	segPtr1->header.rcv_win = 0;
	segPtr1->header.checksum = 0;
	sip_sendseg(connection, segPtr1);
	printf("Client(SYNSENT): send a SYN segment data(src_port: %d, dest_port: %d)\n", segPtr1->header.src_port, segPtr1->header.dest_port);
	tcb_table[sockfd]->state = SYNSENT;
	// set timer and check state to confirm the ack
	while (resend_syn_times < SYN_MAX_RETRY ) {
		  usleep(SYN_TIMEOUT / 1000);
		if (tcb_table[sockfd]->state == CONNECTED) {
			return 1;
		}
		printf("Client(SYNSENT): Resend a SYN segment data(src_port: %d, dest_port: %d)\n", 
		segPtr1->header.src_port, segPtr1->header.dest_port);
		sip_sendseg(connection, segPtr1);
		resend_syn_times ++;
	}
	tcb_table[sockfd]->state = CLOSED;
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
	if (tcb_table[sockfd]->state == CONNECTED) {
		char *p = (char *)data;
		while (length / MAX_SEG_LEN > 0) {
			segBuf_t *segbuf = (segBuf_t *)malloc(sizeof(segBuf_t));
			segbuf->seg.header.src_port = tcb_table[sockfd]->client_portNum;
			segbuf->seg.header.dest_port = tcb_table[sockfd]->server_portNum;
			segbuf->seg.header.seq_num = tcb_table[sockfd]->next_seqNum;	//??
			tcb_table[sockfd]->next_seqNum ++;
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
			if (tcb_table[sockfd]->sendBufHead == NULL) {	// 链表为空
				pthread_mutex_lock(tcb_table[sockfd]->bufMutex);
				tcb_table[sockfd]->sendBufTail = segbuf;
				tcb_table[sockfd]->sendBufHead = segbuf;
				tcb_table[sockfd]->sendBufunSent = segbuf;
				pthread_mutex_unlock(tcb_table[sockfd]->bufMutex);
				pthread_t thread;	// 创建线程
				int *param = (int *)malloc(sizeof(int));
				*param = sockfd;
				int rc = pthread_create(&thread,NULL,sendBuf_timer,(void *)param);
				if (rc) {
					printf("create thread for sendBuf_timer error!\n");
				}
			}
			else {	// 添加到链表尾
				pthread_mutex_lock(tcb_table[sockfd]->bufMutex);
				if (tcb_table[sockfd]->sendBufunSent == NULL) {
					tcb_table[sockfd]->sendBufunSent = segbuf;
				}
				tcb_table[sockfd]->sendBufTail->next = segbuf;
				tcb_table[sockfd]->sendBufTail = segbuf;
				pthread_mutex_unlock(tcb_table[sockfd]->bufMutex);
			}
		}
		// 长度不到MAX_SEG_LEN的部分
		segBuf_t *segbuf = (segBuf_t *)malloc(sizeof(segBuf_t));
		segbuf->seg.header.src_port = tcb_table[sockfd]->client_portNum;
		segbuf->seg.header.dest_port = tcb_table[sockfd]->server_portNum;
		segbuf->seg.header.seq_num = tcb_table[sockfd]->next_seqNum;	//??
		tcb_table[sockfd]->next_seqNum ++;
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
		pthread_mutex_lock(tcb_table[sockfd]->bufMutex);
		if (tcb_table[sockfd]->sendBufHead == NULL) {	// 链表为空
			//pthread_mutex_lock(tcb_table[sockfd]->bufMutex);
			tcb_table[sockfd]->sendBufTail = segbuf;
			tcb_table[sockfd]->sendBufHead = segbuf;
			tcb_table[sockfd]->sendBufunSent = segbuf;
			//pthread_mutex_unlock(tcb_table[sockfd]->bufMutex);
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
			if (tcb_table[sockfd]->sendBufunSent == NULL) {
				tcb_table[sockfd]->sendBufunSent = segbuf;
			}
			tcb_table[sockfd]->sendBufTail->next = segbuf;
			tcb_table[sockfd]->sendBufTail = segbuf;
		}
		//  发送数据段直到已发送但未被确认的段数量到达GBN_WINDOW为止 
		//pthread_mutex_lock(tcb_table[sockfd]->bufMutex);

		int ack_sum = 0;
		segBuf_t *q = tcb_table[sockfd]->sendBufHead;
		for (;q != tcb_table[sockfd]->sendBufunSent; q = q->next)
			ack_sum ++;
		while (ack_sum < GBN_WINDOW) {
			if (tcb_table[sockfd]->sendBufunSent == NULL) break;
			sip_sendseg(connection, &tcb_table[sockfd]->sendBufunSent->seg);
			ack_sum ++;
		//	printf("Client(CONNECTED):Send a data to server(seq_num is %d, src_port is %d, dest_port is %d)\n",
			//tcb_table[sockfd]->sendBufunSent->seg.header.seq_num,tcb_table[sockfd]->sendBufunSent->seg.header.src_port,
			//tcb_table[sockfd]->sendBufunSent->seg.header.dest_port);
			tcb_table[sockfd]->sendBufunSent = tcb_table[sockfd]->sendBufunSent->next;
		}
		pthread_mutex_unlock(tcb_table[sockfd]->bufMutex);
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
	segPtr2->header.src_port = tcb_table[sockfd]->client_portNum;
	segPtr2->header.dest_port = tcb_table[sockfd]->server_portNum;
	segPtr2->header.seq_num = 0;
	segPtr2->header.ack_num = 0;
	segPtr2->header.length = sizeof(seg_t);//???
	segPtr2->header.type = FIN;
	segPtr2->header.rcv_win = 0;
	segPtr2->header.checksum = 0;
	sip_sendseg(connection, segPtr2);
	printf("Client(FINWAIT): send a FIN segment data(src_port: %d, dest_port: %d)\n",segPtr2->header.src_port,segPtr2->header.dest_port);
	tcb_table[sockfd]->state = FINWAIT;
	// set timer and check state
	while (resend_fin_times < FIN_MAX_RETRY) {
		usleep(FIN_TIMEOUT/1000);
		if (tcb_table[sockfd]->state == CLOSED) {
			return 1;
		}
		sip_sendseg(connection, segPtr2);
		printf("Client(FINWAIT): Resend a FIN segment data(src_port: %d, dest_port: %d)\n",
		segPtr2->header.src_port,segPtr2->header.dest_port);
		resend_fin_times ++;
	}
	tcb_table[sockfd]->state = CLOSED;
	printf("Client(CLOSED): cann't receive FINACK after send FIN(src_port: %d, dest_port: %d)\n",
	segPtr2->header.src_port,segPtr2->header.dest_port);
	// 清空发送缓冲区
	pthread_mutex_lock(tcb_table[sockfd]->bufMutex);
	segBuf_t *p = tcb_table[sockfd]->sendBufHead;
	while (p != NULL) {
		segBuf_t *q = p;
		p = p->next;
		free(q);
	}
	tcb_table[sockfd]->sendBufHead = NULL;
	tcb_table[sockfd]->sendBufunSent = NULL;
	tcb_table[sockfd]->sendBufTail = NULL;
	pthread_mutex_unlock(tcb_table[sockfd]->bufMutex);
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
	free(tcb_table[sockfd]->bufMutex);
	tcb_table[sockfd]->bufMutex = NULL;
	free(tcb_table[sockfd]);
	tcb_table[sockfd] = NULL;
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
	while (1) {
		int count = sip_recvseg(connection, segPtr);
		if ( count > 0) {
			unsigned short int type = segPtr->header.type;
			// 查找该数据段的TCB
			unsigned int client_port = segPtr->header.dest_port;
			int sockfd;
			for (sockfd = 0; sockfd < MAX_TRANSPORT_CONNECTIONS; sockfd ++) {
				if (tcb_table[sockfd] != NULL) {
					if (tcb_table[sockfd]->client_portNum == client_port) {
						break;
					}
				}
			}
			// client FSM
			switch(tcb_table[sockfd]->state) {
				case SYNSENT:
				if (type == SYNACK) {
					tcb_table[sockfd]->state = CONNECTED;
					printf("Client(SYNSENT): Get a SYNACK, state changes: SYNSENT---->CONNECTED\n");
				}
				break;
				case CONNECTED:
					if (type == DATAACK) {
						int ack_num = segPtr->header.ack_num;	// !!!!!!!!!!!!!!!
						printf("Client: Get a DATAACK from Server(expect_num is %d)\n", ack_num);
						pthread_mutex_lock(tcb_table[sockfd]->bufMutex);
						segBuf_t *q = tcb_table[sockfd]->sendBufHead;
						segBuf_t *p = NULL;
						while (q != tcb_table[sockfd]->sendBufunSent) { //释放发送缓冲区
							if (q->seg.header.seq_num < ack_num) {	
								p = q;
								q = q->next;
								free(p);
							}
							else 
								break;
						}
						tcb_table[sockfd]->sendBufHead = q;
						//  发送数据段直到已发送但未被确认的段数量到达GBN_WINDOW为止 
						int ack_sum = 0;
						segBuf_t *q1 = tcb_table[sockfd]->sendBufHead;
						for (;q1 != tcb_table[sockfd]->sendBufunSent && q1 != NULL; q1 = q1->next)
							ack_sum ++;
	
						while (ack_sum < GBN_WINDOW) {
							if (tcb_table[sockfd]->sendBufunSent == NULL) break;
							printf("Client:send data to server(seq_num is %d)\n", tcb_table[sockfd]->sendBufunSent->seg.header.seq_num);
							sip_sendseg(connection, &tcb_table[sockfd]->sendBufunSent->seg);
							ack_sum ++;
							tcb_table[sockfd]->sendBufunSent = tcb_table[sockfd]->sendBufunSent->next;
						}
						//printf("ack_sum is %d-----------------------------\n", ack_sum);
						pthread_mutex_unlock(tcb_table[sockfd]->bufMutex);
					}
				break;
				case FINWAIT:
				if (type == FINACK) {
					tcb_table[sockfd]->state = CLOSED;
					printf("Client(FINWAIT): GET a FINACK, state changes: FINWAIT----->CLOSED\n");
				}
				break;
				
			}
		}
		else if (count == 0);	// 段数据丢失
		else		// TCP 连接断开
			break;
	}
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
		if (tcb_table[sockfd]->state == CONNECTED && tcb_table[sockfd]->sendBufHead != NULL) {
			// set timer 
			time_t now;
			time(&now);
			//printf("-----------------------sendBuf_timer----------------\n");
	       // printf("now is %d, interval is %d,sentTime is %d\n", (int)now,(int)now - tcb_table[sockfd]->sendBufHead->sentTime,tcb_table[sockfd]->sendBufHead->sentTime);
	        pthread_mutex_lock(tcb_table[sockfd]->bufMutex);
   	        if (((int)now - tcb_table[sockfd]->sendBufHead->sentTime) >= 1) {
		       printf("Timeout and resend data\n");
		       segBuf_t *q = tcb_table[sockfd]->sendBufHead;
		       for (;q != tcb_table[sockfd]->sendBufunSent; q = q->next) {
				   //printf("Client send data to server(seq_num is %d)\n", q->seg.header.seq_num);
			       sip_sendseg(connection, &q->seg);
			     //  q->sentTime = clock();
				   }
	         }
            pthread_mutex_unlock(tcb_table[sockfd]->bufMutex);
			usleep(SENDBUF_POLLING_INTERVAL/1000000);
		}
		else {
			break;
		}
	}
	//printf("-------------------------timer exit(1)-------------------\n");
  	pthread_exit(NULL);
}
