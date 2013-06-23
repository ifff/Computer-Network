// 文件名 pkt.c
// 创建日期: 2013年1月
#include "pkt.h"
#include "seg.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PKTSTART1 0
#define PKTSTART2 1
#define PKTRECV 2
#define PKTSTOP1 3
// son_sendpkt()由SIP进程调用, 其作用是要求SON进程将报文发送到重叠网络中. SON进程和SIP进程通过一个本地TCP连接互连.
// 在son_sendpkt()中, 报文及其下一跳的节点ID被封装进数据结构sendpkt_arg_t, 并通过TCP连接发送给SON进程. 
// 参数son_conn是SIP进程和SON进程之间的TCP连接套接字描述符.
// 当通过SIP进程和SON进程之间的TCP连接发送数据结构sendpkt_arg_t时, 使用'!&'和'!#'作为分隔符, 按照'!& sendpkt_arg_t结构 !#'的顺序发送.
// 如果发送成功, 返回1, 否则返回-1.
int son_sendpkt(int nextNodeID, sip_pkt_t* pkt, int son_conn)
{
	sendpkt_arg_t *arg = (sendpkt_arg_t *)malloc(sizeof(sendpkt_arg_t));
	arg->nextNodeID = nextNodeID;
	memcpy((char *)&(arg->pkt), (char *)pkt, sizeof(sip_pkt_t));
	seg_t *seg = (seg_t *)&(arg->pkt.data);
	char *buffer = "!&";
	if (send(son_conn, buffer, 2, 0) < 0) {
		perror("Problem in sending data\n");
		return -1;
	}	
	buffer = (char *)arg;
	if (send(son_conn, buffer, sizeof(sendpkt_arg_t), 0) < 0) {
		perror("Problem in sending data\n");
		return -1;
	}
	buffer = "!#";
	if (send(son_conn, buffer, 2, 0) < 0) {
		perror("Problem in sending data\n");
		return -1;
	}
	return 1;
}

// son_recvpkt()函数由SIP进程调用, 其作用是接收来自SON进程的报文. 
// 参数son_conn是SIP进程和SON进程之间TCP连接的套接字描述符. 报文通过SIP进程和SON进程之间的TCP连接发送, 使用分隔符!&和!#. 
// 为了接收报文, 这个函数使用一个简单的有限状态机FSM
// PKTSTART1 -- 起点 
// PKTSTART2 -- 接收到'!', 期待'&' 
// PKTRECV -- 接收到'&', 开始接收数据
// PKTSTOP1 -- 接收到'!', 期待'#'以结束数据的接收 
// 如果成功接收报文, 返回1, 否则返回-1.
int son_recvpkt(sip_pkt_t* pkt, int son_conn)
{
	char buffer;
	int state = PKTSTART1;
	int result;	// 判断TCP连接是否断开
	while ((result = recv(son_conn, &buffer, 1, 0)) > 0) {
		switch (state) {	// FSM
			case PKTSTART1:
				if (buffer == '!') 
					state = PKTSTART2;
				break;
			case PKTSTART2:
				if (buffer == '&') {
					state = PKTRECV;
					recv(son_conn, (char *)pkt, sizeof(sip_pkt_t), 0);	// recv pkt data
				}
				break;
			case PKTRECV:
				if (buffer == '!')
					state = PKTSTOP1;
				break;
		}
		if (state == PKTSTOP1 && buffer == '#')
		{
			break;
		}
	} 
	if (result <= 0)	// 连接断开
	{
		return -1;
	}
	else 
		return 1;
}

// 这个函数由SON进程调用, 其作用是接收数据结构sendpkt_arg_t.
// 报文和下一跳的节点ID被封装进sendpkt_arg_t结构.
// 参数sip_conn是在SIP进程和SON进程之间的TCP连接的套接字描述符. 
// sendpkt_arg_t结构通过SIP进程和SON进程之间的TCP连接发送, 使用分隔符!&和!#. 
// 为了接收报文, 这个函数使用一个简单的有限状态机FSM
// PKTSTART1 -- 起点 
// PKTSTART2 -- 接收到'!', 期待'&' 
// PKTRECV -- 接收到'&', 开始接收数据
// PKTSTOP1 -- 接收到'!', 期待'#'以结束数据的接收
// 如果成功接收sendpkt_arg_t结构, 返回1, 否则返回-1.
int getpktToSend(sip_pkt_t* pkt, int* nextNode,int sip_conn)
{
	sendpkt_arg_t *arg = (sendpkt_arg_t *)malloc(sizeof(sendpkt_arg_t));
	char buffer;
	int state = PKTSTART1;
	int result;	// 判断TCP连接是否断开
	while ((result = recv(sip_conn, &buffer, 1, 0)) > 0) {
		switch (state) {	// FSM
			case PKTSTART1:
				if (buffer == '!') 
					state = PKTSTART2;
				break;
			case PKTSTART2:
				if (buffer == '&') {
					state = PKTRECV;
					recv(sip_conn, (char *)arg, sizeof(sendpkt_arg_t), 0);	// recv pkt data
					*nextNode = arg->nextNodeID;
					memcpy((char *)pkt, (char *)&(arg->pkt), sizeof(sip_pkt_t));
				}
				break;
			case PKTRECV:
				if (buffer == '!')
					state = PKTSTOP1;
				break;
		}
		if (state == PKTSTOP1 && buffer == '#')
		{
			break;
		}
	} 
	if (result <= 0)	// 连接断开
	{
		return -1;
	}
	else 
		return 1;
}

// forwardpktToSIP()函数是在SON进程接收到来自重叠网络中其邻居的报文后被调用的. 
// SON进程调用这个函数将报文转发给SIP进程. 
// 参数sip_conn是SIP进程和SON进程之间的TCP连接的套接字描述符. 
// 报文通过SIP进程和SON进程之间的TCP连接发送, 使用分隔符!&和!#, 按照'!& 报文 !#'的顺序发送. 
// 如果报文发送成功, 返回1, 否则返回-1.
int forwardpktToSIP(sip_pkt_t* pkt, int sip_conn)
{
	
	sip_pkt_t *sendpkt = (sip_pkt_t *)malloc(sizeof(sip_pkt_t));
	memcpy((char *)sendpkt, (char *)pkt, sizeof(sip_pkt_t));
	char *buffer = "!&";
	if (send(sip_conn, buffer, 2, 0) < 0) {
		perror("Problem in sending data\n");
		return -1;
	}	
	buffer = (char *)sendpkt;
	if (send(sip_conn, buffer, sizeof(sip_pkt_t), 0) < 0) {
		perror("Problem in sending data\n");
		return -1;
	}
	buffer = "!#";
	if (send(sip_conn, buffer, 2, 0) < 0) {
		perror("Problem in sending data\n");
		return -1;
	}
	//seg_t *seg = (seg_t *)(sendpkt->data);
	//printf("In forwardpktToSIP header.type is %d\n", seg->header.type);
	return 1;
}

// sendpkt()函数由SON进程调用, 其作用是将接收自SIP进程的报文发送给下一跳.
// 参数conn是到下一跳节点的TCP连接的套接字描述符.
// 报文通过SON进程和其邻居节点之间的TCP连接发送, 使用分隔符!&和!#, 按照'!& 报文 !#'的顺序发送. 
// 如果报文发送成功, 返回1, 否则返回-1.
int sendpkt(sip_pkt_t* pkt, int conn)
{
	char *buffer = "!&";
	if (send(conn, buffer, 2, 0) < 0) {
		perror("Problem in sending data\n");
		return -1;
	}	
	buffer = (char *)pkt;
	if (send(conn, buffer, sizeof(sip_pkt_t), 0) < 0) {
		perror("Problem in sending data\n");
		return -1;
	}
	buffer = "!#";
	if (send(conn, buffer, 2, 0) < 0) {
		perror("Problem in sending data\n");
		return -1;
	}
	return 1;
}

// recvpkt()函数由SON进程调用, 其作用是接收来自重叠网络中其邻居的报文.
// 参数conn是到其邻居的TCP连接的套接字描述符.
// 报文通过SON进程和其邻居之间的TCP连接发送, 使用分隔符!&和!#. 
// 为了接收报文, 这个函数使用一个简单的有限状态机FSM
// PKTSTART1 -- 起点 
// PKTSTART2 -- 接收到'!', 期待'&' 
// PKTRECV -- 接收到'&', 开始接收数据
// PKTSTOP1 -- 接收到'!', 期待'#'以结束数据的接收 
// 如果成功接收报文, 返回1, 否则返回-1.
int recvpkt(sip_pkt_t* pkt, int conn)
{
	char buffer;
	int state = PKTSTART1;
	int result;	// 判断TCP连接是否断开
	while ((result = recv(conn, &buffer, 1, 0)) > 0) {
		switch (state) {	// FSM
			case PKTSTART1:
				if (buffer == '!') 
					state = PKTSTART2;
				break;
			case PKTSTART2:
				if (buffer == '&') {
					state = PKTRECV;
					recv(conn, (char *)pkt, sizeof(sip_pkt_t), 0);	// recv pkt data
				}
				break;
			case PKTRECV:
				if (buffer == '!')
					state = PKTSTOP1;
				break;
		}
		if (state == PKTSTOP1 && buffer == '#')
		{
			break;
		}
	} 
	if (result <= 0)	// 连接断开
	{
		return -1;
	}
	else 
		return 1;
}
