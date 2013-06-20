//文件名: son/neighbortable.c
//
//描述: 这个文件实现用于邻居表的API
//
//创建日期: 2013年1月

#include "neighbortable.h"
#include "../topology/topology.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>

//这个函数首先动态创建一个邻居表. 然后解析文件topology/topology.dat, 填充所有条目中的nodeID和nodeIP字段, 将conn字段初始化为-1.
//返回创建的邻居表.
nbr_entry_t* nt_create()
{
	nbr_entry_t *result = (nbr_entry_t *)malloc(sizeof(nbr_entry_t) * 3);
	int myNode = topology_getMyNodeID();
	FILE *fp;
	fp = fopen("topology.data", "r");
	if (fp == NULL)
	{
		printf("Cann't open file topology.dat\n");
		return 0;
	}
	char buffer[100];
	int count = 0;
	while (fgets(buffer, 99, fp))
	{
		char *node1 = (char *)malloc(10);
		char *node2 = (char *)malloc(10);
		memcpy(node1, buffer, 10);
		memcpy(node2, buffer + 11, 10);
		int node1ID = topology_getNodeIDfromname(node1);
		int node2ID = topology_getNodeIDfromname(node2);
		if (node1ID == myNode )	// 添加邻居节点信息
		{
			result[count].nodeID = node2ID;
			if (node2ID == 185)
			{
				inet_aton("114.212.190.185", &result[count].nodeIP);
			}
			else if (node2ID == 186)
			{
				inet_aton("114.212.190.186", &result[count].nodeIP);
			}
			else if (node2ID == 187)
			{
				inet_aton("114.212.190.187", &result[count].nodeIP);
			}
			else if (node2ID == 188)
			{
				inet_aton("114.212.190.188", &result[count].nodeIP);
			}
			result[count].conn = -1;
			count ++;
		}
		else if (node2ID == myNode)
		{
			result[count].nodeID = node1ID;
			if (node1ID == 185)
			{
				inet_aton("114.212.190.185", &result[count].nodeIP);
			}
			else if (node1ID == 186)
			{
				inet_aton("114.212.190.186", &result[count].nodeIP);
			}
			else if (node1ID == 187)
			{
				inet_aton("114.212.190.187", &result[count].nodeIP);
			}
			else if (node1ID == 188)
			{
				inet_aton("114.212.190.188", &result[count].nodeIP);
			}
			result[count].conn = -1;
			count ++;
		}
	}
	return 0;
}

//这个函数删除一个邻居表. 它关闭所有连接, 释放所有动态分配的内存.
void nt_destroy(nbr_entry_t* nt)
{
	int nbrNum = topology_getNbrNum();	
	int i;
	for (i = 0; i < nbrNum; i ++)
	{
		close(nt[i].conn);
	}
	free(nt);
}

//这个函数为邻居表中指定的邻居节点条目分配一个TCP连接. 如果分配成功, 返回1, 否则返回-1.
int nt_addconn(nbr_entry_t* nt, int nodeID, int conn)
{
	int nbrNum = topology_getNbrNum();
	int i;
	for (i = 0; i < nbrNum; i ++)
	{
		if (nt[i].nodeID == nodeID)
		{
			nt[i].conn = conn;
			return 1;
		}
	}
	return -1;
}
