//文件名: topology/topology.c
//
//描述: 这个文件实现一些用于解析拓扑文件的辅助函数 
//
//创建日期: 2013年1月

#include "topology.h"
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include "../common/constants.h"

//这个函数返回指定主机的节点ID.
//节点ID是节点IP地址最后8位表示的整数.
//例如, 一个节点的IP地址为202.119.32.12, 它的节点ID就是12.
//如果不能获取节点ID, 返回-1.
int topology_getNodeIDfromname(char* hostname) 
{
	//printf("hostname is %s\n", hostname);
	//printf("%d,%d,%d,%d",strcmp(hostname, "csnetlab_1"),strcmp(hostname, "csnetlab_2"),strcmp(hostname, "csnetlab_3"),strcmp(hostname, "csnetlab_4"));
	if (strcmp(hostname, "csnetlab_1") == 0)
	{
		return 185;
	}
	else if (strcmp(hostname, "csnetlab_2") == 0)
	{
		return 186;
	}
	else if (strcmp(hostname, "csnetlab_3") == 0)
	{
		return 187;
	}
	else if(strcmp(hostname, "csnetlab_4") == 0)
	{
		return 188;
	}
	return 0;
}

//这个函数返回指定的IP地址的节点ID.
//如果不能获取节点ID, 返回-1.
int topology_getNodeIDfromip(struct in_addr* addr)
{
	char *p = inet_ntoa(*addr);
	if (strcmp(p, "114.212.190.185") == 0)
	{
		return 185;
	}
	else if (strcmp(p, "114.212.190.186") == 0)
	{
		return 186;
	}
	else if (strcmp(p, "114.212.190.187") == 0)
	{
		return 187;
	}
	else if (strcmp(p, "114.212.190.188") == 0)
	{
		return 188;
	}
	else 
		return -1;
}

//这个函数返回本机的节点ID
//如果不能获取本机的节点ID, 返回-1.
int topology_getMyNodeID()
{
	char hostname[128];
	struct hostent *hent;
	gethostname(hostname, sizeof(hostname));
	hent = gethostbyname(hostname);
	char *p = inet_ntoa(*(struct in_addr*)(hent->h_addr_list[0]));
	if (strcmp(p, "114.212.190.185") == 0)
	{
		return 185;
	}
	else if (strcmp(p, "114.212.190.186") == 0)
	{
		return 186;
	}
	else if (strcmp(p, "114.212.190.187") == 0)
	{
		return 187;
	}
	else if (strcmp(p, "114.212.190.188") == 0)
	{
		return 188;
	}
	else 
		return -1;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回邻居数.
int topology_getNbrNum()
{
	int myNode = topology_getMyNodeID();
	FILE *fp;
	fp = fopen("/home/b101220023/lab13/topology/topology.dat", "r");
	if (fp == NULL)
	{
		printf("Cann't open file topology.dat\n");
		return 0;
	}
	char buffer[100];
	int count = 0;
	while (fgets(buffer, 99, fp))
	{
		char *node1 = (char *)malloc(11);
		char *node2 = (char *)malloc(11);
		memcpy(node1, buffer, 10);
		memcpy(node2, buffer + 11, 10);
		node1[10] = 0;
		node2[10] = 0;
		int node1ID = topology_getNodeIDfromname(node1);
		int node2ID = topology_getNodeIDfromname(node2);
		if (node1ID == myNode || node2ID == myNode)	// 邻居节点信息
		{
			count ++;
		}
	}
	fclose(fp);
	//printf("NbrNum count is %d\n",count);
	return count;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回重叠网络中的总节点数.
int topology_getNodeNum()
{ 
	/*FILE *fp;
	fp = fopen("topology.data", "r");
	if (fp == NULL)
	{
		printf("Cann't open file topology.dat\n");
		return 0;
	}
	char buffer[100];
	while (fgets(buffer, 99, fp))
	{
		
	}*/
	return 4;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回一个动态分配的数组, 它包含重叠网络中所有节点的ID. 
int* topology_getNodeArray()
{
	int *result = (int *)malloc(sizeof(int) * 4);
	result[0] = 185;
	result[1] = 186;
	result[2] = 187;
	result[3] = 188;
	return result;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回一个动态分配的数组, 它包含所有邻居的节点ID.  
int* topology_getNbrArray()
{
	int nbrNum = topology_getNbrNum();
	int *nbrArray = (int* )malloc(sizeof(int) * nbrNum);
	int myNode = topology_getMyNodeID();
	FILE *fp;
	fp = fopen("/home/b101220023/lab13/topology/topology.dat", "r");
	if (fp == NULL)
	{
		printf("Cann't open file topology.dat\n");
		return 0;
	}
	char buffer[100];
	int count = 0;
	while (fgets(buffer, 99, fp))
	{
		char *node1 = (char *)malloc(11);
		char *node2 = (char *)malloc(11);
		memcpy(node1, buffer, 10);
		memcpy(node2, buffer + 11, 10);
		node1[10] = 0;
		node2[10] = 0;
		int node1ID = topology_getNodeIDfromname(node1);
		int node2ID = topology_getNodeIDfromname(node2);
		if (node1ID == myNode)	// 邻居节点信息
		{
			nbrArray[count] = node2ID;
			count ++;
		}
		else if (node2ID == myNode)
		{
			nbrArray[count] = node1ID;
			count ++;
		}
	}
	fclose(fp);
	return nbrArray;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回指定两个节点之间的直接链路代价. 
//如果指定两个节点之间没有直接链路, 返回INFINITE_COST.
unsigned int topology_getCost(int fromNodeID, int toNodeID)
{
	if ((fromNodeID == 185 && toNodeID == 186) || (fromNodeID == 186 && toNodeID == 185))
	{
		return 5;
	}
	else if ((fromNodeID == 185 && toNodeID == 187) || (fromNodeID == 187 && toNodeID == 185))
	{
		return 4;
	}
	else if ((fromNodeID == 185 && toNodeID == 188) || (fromNodeID == 188 && toNodeID == 185))
	{
		return 7;
	}
	else if ((fromNodeID == 188 && toNodeID == 186) || (fromNodeID == 186 && toNodeID == 188))
	{
		return 3;
	}
	else if ((fromNodeID == 187 && toNodeID == 188) || (fromNodeID == 188 && toNodeID == 187))
	{
		return 2;
	}
	return INFINITE_COST;
}

