
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "nbrcosttable.h"
#include "../common/constants.h"
#include "../topology/topology.h"

//这个函数动态创建邻居代价表并使用邻居节点ID和直接链路代价初始化该表.
//邻居的节点ID和直接链路代价提取自文件topology.dat. 
nbr_cost_entry_t* nbrcosttable_create()
{
	int nbrNum = topology_getNbrNum();
	nbr_cost_entry_t *result = (nbr_cost_entry_t *)malloc(sizeof(nbr_cost_entry_t) * nbrNum);
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
	while (fgets(buffer, 99, fp) != NULL)
	{
		//printf("count is %d\n",count);
		char *node1 = (char *)malloc(11);
		char *node2 = (char *)malloc(11);
		memcpy(node1, buffer, 10);
		memcpy(node2, buffer + 11, 10);
		node1[10] = 0;
		node2[10] = 0;
		int node1ID = topology_getNodeIDfromname(node1);
		int node2ID = topology_getNodeIDfromname(node2);
		if (node1ID == myNode )	// 添加邻居节点信息
		{
			result[count].nodeID = node2ID;
			result[count].cost = buffer[22] - '0';
			count ++;
		}
		else if (node2ID == myNode)
		{
			result[count].nodeID = node1ID;
			result[count].cost = buffer[22] - '0';
			count ++;
		}
	}
	return result;
}

//这个函数删除邻居代价表.
//它释放所有用于邻居代价表的动态分配内存.
void nbrcosttable_destroy(nbr_cost_entry_t* nct)
{
	free(nct);
	nct = NULL;
}

//这个函数用于获取邻居的直接链路代价.
//如果邻居节点在表中发现,就返回直接链路代价.否则返回INFINITE_COST.
unsigned int nbrcosttable_getcost(nbr_cost_entry_t* nct, int nodeID)
{
	int nbrNum = topology_getNbrNum();
	int i;
	for (i = 0; i < nbrNum; i ++)
	{
		if (nodeID == nct[i].nodeID)
		{
			return nct[i].cost;
		}
	}
	return INFINITE_COST;
}

//这个函数打印邻居代价表的内容.
void nbrcosttable_print(nbr_cost_entry_t* nct)
{
	int nbrNum = topology_getNbrNum();
	int myNode = topology_getMyNodeID();
	int i;
	for (i = 0; i < nbrNum; i ++)
	{
		printf("Node %d's neighbor(Node %d)%d cost: %d\n", myNode, nct[i].nodeID, i + 1, nct[i].cost);
	}
}
