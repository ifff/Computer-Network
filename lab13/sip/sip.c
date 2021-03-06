//文件名: sip/sip.c
//
//描述: 这个文件实现SIP进程  
//
//创建日期: 2013年1月

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>
#include <assert.h>
#include <sys/utsname.h>
#include <pthread.h>
#include <unistd.h>

#include "../common/constants.h"
#include "../common/pkt.h"
#include "../common/seg.h"
#include "../topology/topology.h"
#include "sip.h"
#include "nbrcosttable.h"
#include "dvtable.h"
#include "routingtable.h"

//SIP层等待这段时间让SIP路由协议建立路由路径. 
#define SIP_WAITTIME 60

/**************************************************************/
//声明全局变量
/**************************************************************/
int son_conn; 			//到重叠网络的连接
int stcp_conn;			//到STCP的连接
nbr_cost_entry_t* nct;			//邻居代价表
dv_t* dv;				//距离矢量表
pthread_mutex_t* dv_mutex;		//距离矢量表互斥量
routingtable_t* routingtable;		//路由表
pthread_mutex_t* routingtable_mutex;	//路由表互斥量

/**************************************************************/
//实现SIP的函数
/**************************************************************/

//SIP进程使用这个函数连接到本地SON进程的端口SON_PORT.
//成功时返回连接描述符, 否则返回-1.
int connectToSON() { 
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

//这个线程每隔ROUTEUPDATE_INTERVAL时间发送路由更新报文.路由更新报文包含这个节点
//的距离矢量.广播是通过设置SIP报文头中的dest_nodeID为BROADCAST_NODEID,并通过son_sendpkt()发送报文来完成的.
void* routeupdate_daemon(void* arg) {
	//你需要编写这里的代码.
	int myNode = topology_getMyNodeID();
	int N = topology_getNodeNum();
	int nbrNum = topology_getNbrNum();

	sip_pkt_t* pkt = (sip_pkt_t* )malloc(sizeof(sip_pkt_t)); 
	pkt->header.src_nodeID = myNode;	
	pkt->header.dest_nodeID = BROADCAST_NODEID;
	pkt->header.length = sizeof(routeupdate_entry_t) * N + 4;	
	pkt->header.type = ROUTE_UPDATE;
	// 初始化data域
	pkt_routeupdate_t *content = (pkt_routeupdate_t *)malloc(sizeof(pkt_routeupdate_t));
	content->entryNum = N;
	while (1)
	{
		int i;
		pthread_mutex_lock(dv_mutex);
		for(i = 0; i < N; i ++){
			content->entry[i].nodeID = dv[nbrNum].dvEntry[i].nodeID;
			content->entry[i].cost = dv[nbrNum].dvEntry[i].cost;
		}
		pthread_mutex_unlock(dv_mutex);
		memcpy(pkt->data,content,sizeof(pkt_routeupdate_t));
		son_sendpkt(BROADCAST_NODEID, pkt, son_conn);
		sleep(ROUTEUPDATE_INTERVAL);
	}
}

//这个线程处理来自SON进程的进入报文. 它通过调用son_recvpkt()接收来自SON进程的报文.
//如果报文是SIP报文,并且目的节点就是本节点,就转发报文给STCP进程. 如果目的节点不是本节点,
//就根据路由表转发报文给下一跳.如果报文是路由更新报文,就更新距离矢量表和路由表.
void* pkthandler(void* arg) {	
	//你需要编写这里的代码.
	sip_pkt_t pkt;
	int myNode = topology_getMyNodeID();
	int N = topology_getNodeNum();
	int nbrNum = topology_getNbrNum();
	int count = 0;
	while(son_recvpkt(&pkt,son_conn)>0) {
		printf("Routing: received a packet from neighbor %d\n",pkt.header.src_nodeID);
		if(pkt.header.type == SIP){
			printf("-----------------Packet Type is SIP-------------\n");
			if(pkt.header.dest_nodeID == myNode){
				printf("-------------The Packet reach the destination node-------------\n");
				int src_nodeID = pkt.header.src_nodeID;
				seg_t *segPtr = (seg_t *)malloc(sizeof(seg_t));
				memcpy((char *)segPtr,(char *)&(pkt.data),sizeof(seg_t));	//!!!!!!!!!!!!!!!!!!!!	
				printf("reach!!!!!!!!! segPtr->header.type is %d\n", segPtr->header.type);
				forwardsegToSTCP(stcp_conn,src_nodeID,segPtr);		
			}
			else{
				int slot = makehash(pkt.header.dest_nodeID);
				routingtable_entry_t *entry = routingtable->hash[slot];
				while(entry != NULL){
					if(entry->destNodeID == pkt.header.dest_nodeID){
						son_sendpkt(entry->nextNodeID, &pkt, son_conn);
						//!!!!!!!!!!!!!!!!!
					//	seg_t *seg = (seg_t *)&(pkt.data);
						//printf("Transmit!!!!!!!!!!! segPtr->header.type is %d\n", seg->header.type);
						//!!!!!!!!!!!
						printf("-------------Transmit the packet to next node %d--------\n", entry->nextNodeID);
						break;
					}
					entry = entry->next;
				}
			}
		}
		else{
			printf("-----------------Packet Type is ROUTE_UPDATE-------------\n");
			count ++;
			if (count == 30)	// 打印距离向量表
			{
				dvtable_print(dv);
			}
			pkt_routeupdate_t *content = (pkt_routeupdate_t *)pkt.data;	// 更新路由信息
			int fromNodeID = pkt.header.src_nodeID;			
			int i, j;
			pthread_mutex_lock(dv_mutex);
			for(i = 0;i < nbrNum;i ++){
				if(dv[i].nodeID == fromNodeID){
					for(j = 0; j < N; j ++){
						dv[i].dvEntry[j].cost = content->entry[j].cost;
					}
					break;
				}
			}
			pthread_mutex_unlock(dv_mutex);
			// 更新节点自身到其他节点的最短路径
			unsigned int mid_node = 0;
			unsigned int min_cost = INFINITE_COST;
			int *nbrArray = topology_getNbrArray();
			pthread_mutex_lock(dv_mutex);
			for(i = 0; i < N; i ++){
				mid_node = routingtable_getnextnode(routingtable, dv[nbrNum].dvEntry[i].nodeID);
				pthread_mutex_lock(routingtable_mutex);
				min_cost = dvtable_getcost(dv, myNode, dv[nbrNum].dvEntry[i].nodeID);
				for(j = 0; j < nbrNum; j ++){
					unsigned int nbrCost = nbrcosttable_getcost(nct,nbrArray[j]);
					unsigned int dvCost = dvtable_getcost(dv, nbrArray[j], dv[nbrNum].dvEntry[i].nodeID);
					if(min_cost > (nbrCost + dvCost)){	// 更新最短路径
						min_cost = nbrCost + dvCost;
						//printf("nbrcost: %d, dvCost: %d\n", nbrCost, dvCost);
						printf("------------Update: src(Node %d) dest(Node %d) currentcost: %d---------\n",myNode,dv[nbrNum].dvEntry[i].nodeID,min_cost);
						mid_node = nbrArray[j];
					}
				}			
				dv[nbrNum].dvEntry[i].cost = min_cost;
				if(mid_node != -1){	//更新nextNode和min_cost
					dvtable_setcost(dv, myNode, dv[nbrNum].dvEntry[i].nodeID, min_cost);
					routingtable_setnextnode(routingtable, dv[0].dvEntry[i].nodeID, mid_node);
				}
				pthread_mutex_unlock(routingtable_mutex);
			}
			pthread_mutex_unlock(dv_mutex);
		}
	}
	pthread_exit(NULL);
}

//这个函数终止SIP进程, 当SIP进程收到信号SIGINT时会调用这个函数. 
//它关闭所有连接, 释放所有动态分配的内存.
void sip_stop() {
	//你需要编写这里的代码.
	close(son_conn);
	close(stcp_conn);
	son_conn = -1;
	stcp_conn = -1;
	nbrcosttable_destroy(nct);
	dvtable_destroy(dv);
	free(dv_mutex);
	free(routingtable_mutex);
	dv_mutex = NULL;
	routingtable_mutex = NULL;
}

//这个函数打开端口SIP_PORT并等待来自本地STCP进程的TCP连接.
//在连接建立后, 这个函数从STCP进程处持续接收包含段及其目的节点ID的sendseg_arg_t. 
//接收的段被封装进数据报(一个段在一个数据报中), 然后使用son_sendpkt发送该报文到下一跳. 下一跳节点ID提取自路由表.
//当本地STCP进程断开连接时, 这个函数等待下一个STCP进程的连接.
void waitSTCP() {
	//你需要编写这里的代码.
	int listenfd,connfd;
	struct sockaddr_in cliaddr,servaddr;
	socklen_t clilen;
	listenfd = socket(AF_INET,SOCK_STREAM,0);
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(SIP_PORT);
	
	bind(listenfd,(struct sockaddr *)&servaddr,sizeof(servaddr));
	listen(listenfd,8);
	clilen = sizeof(cliaddr);
	
	int myID = topology_getMyNodeID();
	
	seg_t recv_seg;
	int destNodeID;
	int nextNodeID;
	while(1){
		connfd = accept(listenfd,(struct sockaddr *)&cliaddr,&clilen);
		stcp_conn=connfd;
		printf("---------------wait for thread STCP ------------\n");
		while(1){
			sip_pkt_t send_pkt;
			int rc;
			if((rc=getsegToSend(stcp_conn, &destNodeID,&recv_seg))==-1){
				printf("--------------------STCP disconnect---------------\n");
				break;
			}
			printf("waitSTCP: destNodeID=%d, seg->header.type is %d\n",destNodeID, recv_seg.header.type);
			send_pkt.header.src_nodeID = myID;
			send_pkt.header.dest_nodeID=destNodeID;
			send_pkt.header.length = sizeof(seg_t);
			send_pkt.header.type = SIP;
			memcpy(send_pkt.data,&recv_seg,sizeof(seg_t));			
			nextNodeID = routingtable_getnextnode(routingtable,destNodeID);
			son_sendpkt(nextNodeID,&send_pkt,son_conn);
			
		}

	}
	pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
	printf("SIP layer is starting, pls wait...\n");

	//初始化全局变量
	nct = nbrcosttable_create();
	dv = dvtable_create();
	dv_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(dv_mutex,NULL);
	routingtable = routingtable_create();
	routingtable_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(routingtable_mutex,NULL);
	son_conn = -1;
	stcp_conn = -1;
	
	nbrcosttable_print(nct);
	dvtable_print(dv);
	routingtable_print(routingtable);

	//注册用于终止进程的信号句柄
	signal(SIGINT, sip_stop);

	//连接到本地SON进程 
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
	printf("waiting for routes to be established\n");
	sleep(SIP_WAITTIME);
	routingtable_print(routingtable);

	//等待来自STCP进程的连接
	printf("waiting for connection from STCP process\n");
	waitSTCP(); 

}


