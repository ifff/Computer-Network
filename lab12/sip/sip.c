//文件名: sip/sip.c
//
//描述: 这个文件实现SIP进程  
//
//创建日期: 2013年1月

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/utsname.h>
#include <assert.h>

#include "../common/constants.h"
#include "../common/pkt.h"
#include "../topology/topology.h"
#include "sip.h"

/**************************************************************/
//声明全局变量
/**************************************************************/
int son_conn; 		//到重叠网络的连接

/**************************************************************/
//实现SIP的函数
/**************************************************************/

//SIP进程使用这个函数连接到本地SON进程的端口SON_PORT
//成功时返回连接描述符, 否则返回-1
int connectToSON() { 
	//你需要编写这里的代码.
	int sockfd;
	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;

	char hostname[128];
	struct hostent *hent;
	gethostname(hostname, sizeof(hostname));
	hent = gethostbyname(hostname);
	char *p = inet_ntoa(*(struct in_addr*)(hent->h_addr_list[0]));
	if (strcmp(p, "114.212.190.185") == 0)
	{
		servaddr.sin_addr.s_addr = inet_addr("114.212.190.185");
	}
	else if (strcmp(p, "114.212.190.186") == 0)
	{
		servaddr.sin_addr.s_addr = inet_addr("114.212.190.186");
	}
	else if (strcmp(p, "114.212.190.187") == 0)
	{
		servaddr.sin_addr.s_addr = inet_addr("114.212.190.187");
	}
	else if (strcmp(p, "114.212.190.188") == 0)
	{
		servaddr.sin_addr.s_addr = inet_addr("114.212.190.188");
	}
	
	servaddr.sin_port = htons(SON_PORT);
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Problem in creating the socket\n");
		return -1;
	}
	if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
		perror("Problem in connecting to the server\n");
		return -1;
	}
	int myNode = topology_getMyNodeID();
	printf("(Node %d:)SIP connects to SON\n", myNode);
	return sockfd;
}

//这个线程每隔ROUTEUPDATE_INTERVAL时间就发送一条路由更新报文
//在本实验中, 这个线程只广播空的路由更新报文给所有邻居, 
//我们通过设置SIP报文首部中的dest_nodeID为BROADCAST_NODEID来发送广播
void* routeupdate_daemon(void* arg) {
	//你需要编写这里的代码.
	int myNode = topology_getMyNodeID();
	sip_pkt_t* pkt = (sip_pkt_t* )malloc(sizeof(sip_pkt_t)); 
	pkt->header.src_nodeID = myNode;	//??
	pkt->header.dest_nodeID = BROADCAST_NODEID;
	pkt->header.length = sizeof(pkt->data);	// ??
	pkt->header.type = ROUTE_UPDATE;
	while (1)
	{
		son_sendpkt(BROADCAST_NODEID, pkt, son_conn);
		sleep(ROUTEUPDATE_INTERVAL);
	}
}

//这个线程处理来自SON进程的进入报文
//它通过调用son_recvpkt()接收来自SON进程的报文
//在本实验中, 这个线程在接收到报文后, 只是显示报文已接收到的信息, 并不处理报文 
void* pkthandler(void* arg) {
	sip_pkt_t pkt;

	while(son_recvpkt(&pkt,son_conn)>0) {
		printf("Routing: received a packet from neighbor %d\n",pkt.header.src_nodeID);
	}
	close(son_conn);
	son_conn = -1;
	pthread_exit(NULL);
}

//这个函数终止SIP进程, 当SIP进程收到信号SIGINT时会调用这个函数. 
//它关闭所有连接, 释放所有动态分配的内存.
void sip_stop() {
	//你需要编写这里的代码.
	close(son_conn);
}

int main(int argc, char *argv[]) {
	printf("SIP layer is starting, please wait...\n");

	//初始化全局变量
	son_conn = -1;

	//注册用于终止进程的信号句柄
	signal(SIGINT, sip_stop);

	//连接到重叠网络层SON
	son_conn = connectToSON();
	if(son_conn<0) {
		printf("can't connect to SON process\n");
		exit(1);		
	}
	
	//启动线程处理来自SON进程的进入报文
	pthread_t pkt_handler_thread; 
	pthread_create(&pkt_handler_thread,NULL,pkthandler,(void*)0);

	//启动路由更新线程 
	pthread_t routeupdate_thread;
	pthread_create(&routeupdate_thread,NULL,routeupdate_daemon,(void*)0);	

	printf("SIP layer is started...\n");

	//永久睡眠
	while(1) {
		sleep(60);
	}
}


