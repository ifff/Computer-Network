#include <stdlib.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <time.h>
#include <pthread.h>
#include "stcp_server.h"
#include "../common/constants.h"

/*面向应用层的接口*/

//
//
//  我们在下面提供了每个函数调用的原型定义和细节说明, 但这些只是指导性的, 你完全可以根据自己的想法来设计代码.
//
//  注意: 当实现这些函数时, 你需要考虑FSM中所有可能的状态, 这可以使用switch语句来实现.
//
//  目标: 你的任务就是设计并实现下面的函数原型.
//

// stcp服务器初始化
//
// 这个函数初始化TCB表, 将所有条目标记为NULL. 它还针对重叠网络TCP套接字描述符conn初始化一个STCP层的全局变量,
// 该变量作为sip_sendseg和sip_recvseg的输入参数. 最后, 这个函数启动seghandler线程来处理进入的STCP段.
// 服务器只有一个seghandler.
//
server_tcb_t *tcb_table[MAX_TRANSPORT_CONNECTIONS];
int connection;
void stcp_server_init(int conn) {
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

// 创建服务器套接字
//
// 这个函数查找服务器TCB表以找到第一个NULL条目, 然后使用malloc()为该条目创建一个新的TCB条目.
// 该TCB中的所有字段都被初始化, 例如, TCB state被设置为CLOSED, 服务器端口被设置为函数调用参数server_port. 
// TCB表中条目的索引应作为服务器的新套接字ID被这个函数返回, 它用于标识服务器的连接. 
// 如果TCB表中没有条目可用, 这个函数返回-1.

int stcp_server_sock(unsigned int server_port) {
	int i;
	for (i = 0; i < MAX_TRANSPORT_CONNECTIONS; i ++) {
		if (tcb_table[i] == NULL)
			break;
	}
	if (i == MAX_TRANSPORT_CONNECTIONS) return -1;
	tcb_table[i] = (server_tcb_t *)malloc(sizeof(server_tcb_t));
	/* initialize the tcb item */
	tcb_table[i]->server_nodeID = 0;
	tcb_table[i]->server_portNum = server_port;
	tcb_table[i]->client_nodeID = 0;
	tcb_table[i]->client_portNum = 0;
	tcb_table[i]->state = CLOSED;
	tcb_table[i]->expect_seqNum = 0;
	tcb_table[i]->recvBuf = NULL;
	tcb_table[i]->usedBufLen = 0;
	tcb_table[i]->bufMutex = NULL;
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
	tcb_table[sockfd]->state = LISTENING;
	while (tcb_table[sockfd]->state != CONNECTED);
	return 1;
}

// 接收来自STCP客户端的数据
//
int stcp_server_recv(int sockfd, void* buf, unsigned int length) {
	return 1;
}

// 关闭STCP服务器
//
// 这个函数调用free()释放TCB条目. 它将该条目标记为NULL, 成功时(即位于正确的状态)返回1,
// 失败时(即位于错误的状态)返回-1.
//

int stcp_server_close(int sockfd) {
	free(tcb_table[sockfd]);
	tcb_table[sockfd] = NULL;
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
		int count = sip_recvseg(connection, segPtr);
		if (count > 0) {
			unsigned short int type = segPtr->header.type;
			// 查找该数据段的TCB
			unsigned int server_port = segPtr->header.dest_port;
			int sockfd;
			for (sockfd = 0; sockfd < MAX_TRANSPORT_CONNECTIONS; sockfd ++) {
				if (tcb_table[sockfd] != NULL) {
					if (tcb_table[sockfd]->server_portNum == server_port) {
						break;
					}
				}
			}
			// server FSM
			switch(tcb_table[sockfd]->state) {
				case LISTENING:  	// receive syn and send synack
				if (type == SYN) {
					tcb_table[sockfd]->client_portNum = segPtr->header.src_port;	// update client_portNum
					seg_t *synack = (seg_t *)malloc(sizeof(seg_t));
					synack->header.src_port = tcb_table[sockfd]->server_portNum;
					synack->header.dest_port = tcb_table[sockfd]->client_portNum;
					synack->header.seq_num = 0;
					synack->header.ack_num = 0;
					synack->header.length = 0;
					synack->header.type = SYNACK;
					synack->header.rcv_win = 0;
					synack->header.checksum = 0;
					sip_sendseg(connection, synack);   // send synack
					printf("Server(LISTENING): receive a SYN from client and send a SYNACK(src_port: %d, dest_port: %d)\n",
					synack->header.src_port, synack->header.dest_port);
					tcb_table[sockfd]->state = CONNECTED;	// change the state
					printf("state changes: LISTENING--->CONNECTED\n");
				}
				break;
				case CONNECTED:
				if (type == SYN) {
					seg_t *synack = (seg_t *)malloc(sizeof(seg_t));
					synack->header.src_port = tcb_table[sockfd]->server_portNum;
					synack->header.dest_port = tcb_table[sockfd]->client_portNum;
					synack->header.seq_num = 0;
					synack->header.ack_num = 0;
					synack->header.length = 0;
					synack->header.type = SYNACK;
					synack->header.rcv_win = 0;
					synack->header.checksum = 0;
					sip_sendseg(connection, synack);   // send synack
					printf("Server(CONNECTED): receive a SYN from client and resend a SYNACK(src_port: %d, dest_port: %d)\n",
					synack->header.src_port, synack->header.dest_port); 
				}
				else if (type == FIN) {		// receive fin and send finack
					seg_t *finack = (seg_t *)malloc(sizeof(seg_t));
					finack->header.src_port = tcb_table[sockfd]->server_portNum;
					finack->header.dest_port = tcb_table[sockfd]->client_portNum;
					finack->header.seq_num = 0;
					finack->header.ack_num = 0;
					finack->header.length = 0;
					finack->header.type = FINACK;
					finack->header.rcv_win = 0;
					finack->header.checksum = 0;
					sip_sendseg(connection, finack);   // send FINACK
					printf("Server(CONNECTED): receive a FIN from client and send a FINACK(src_port: %d, dest_port: %d)\n",
					finack->header.src_port, finack->header.dest_port);
					tcb_table[sockfd]->state = CLOSEWAIT;	// change the state
					printf("state changes: CONNECTED--->CLOSEWAIT\n");
				}
				break;
				case CLOSEWAIT:
				if (type == FIN) {		// receive fin and send finack
					seg_t *finack = (seg_t *)malloc(sizeof(seg_t));
					finack->header.src_port = tcb_table[sockfd]->server_portNum;
					finack->header.dest_port = tcb_table[sockfd]->client_portNum;
					finack->header.seq_num = 0;
					finack->header.ack_num = 0;
					finack->header.length = 0;
					finack->header.type = FINACK;
					finack->header.rcv_win = 0;
					finack->header.checksum = 0;
					sip_sendseg(connection, finack);   // send FINACK
				printf("Server(CLOSEWAIT):receive a FIN from client and resend a FINACK(src_port: %d, dest_port: %d)\n",
					finack->header.src_port, finack->header.dest_port);
					//sleep(CLOSEWAIT_TIMEOUT);
					//tcb_table[sockfd]->state = CLOSED;
					//printf("state changes: CLOSEWAIT--->CLOSED\n");
				}
				break;
			}
		}
		else if (count == 0);	// 段数据丢失
		else break;	// TCP连接断开
	}
	pthread_exit(0);
}
