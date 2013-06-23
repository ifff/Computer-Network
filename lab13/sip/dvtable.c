
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../common/constants.h"
#include "../topology/topology.h"
#include "dvtable.h"
#include "../son/neighbortable.h"
//这个函数动态创建距离矢量表.
//距离矢量表包含n+1个条目, 其中n是这个节点的邻居数,剩下1个是这个节点本身.
//距离矢量表中的每个条目是一个dv_t结构,它包含一个源节点ID和一个有N个dv_entry_t结构的数组, 其中N是重叠网络中节点总数.
//每个dv_entry_t包含一个目的节点地址和从该源节点到该目的节点的链路代价.
//距离矢量表也在这个函数中初始化.从这个节点到其邻居的链路代价使用提取自topology.dat文件中的直接链路代价初始化.
//其他链路代价被初始化为INFINITE_COST.
//该函数返回动态创建的距离矢量表.
dv_t* dvtable_create()
{
	int N = topology_getNodeNum();
	int n = topology_getNbrNum();
	int myNode = topology_getMyNodeID();
	dv_t* result = (dv_t* )malloc(sizeof(dv_t) * (n + 1));
	// 初始化dv_t
	int *nbrArray = topology_getNbrArray();
	int *nodeArray = topology_getNodeArray();
	int i, j;
	for (i = 0; i < n + 1; i ++)
	{
		result[i].dvEntry = (dv_entry_t *)malloc(sizeof(dv_entry_t) * N);
		if (i == n)
		{
			result[n].nodeID = myNode;	// 最后一个条目为节点本身
		}
		else 
			result[i].nodeID = nbrArray[i];
		for (j = 0; j < N; j ++)
		{
			result[i].dvEntry[j].nodeID = nodeArray[j];
			if (nodeArray[j] == result[i].nodeID)	// 初始化cost
			{
				result[i].dvEntry[j].cost = 0;	// 节点本身
			}
			else
				result[i].dvEntry[j].cost = INFINITE_COST; // 其他节点
		}
	}
	// 初始化每个条目
	FILE *fp;
	fp = fopen("/home/b101220023/lab13/topology/topology.dat", "r");
	if (fp == NULL)
	{
		printf("Cann't open file topology.dat\n");
		return 0;
	}
	char buffer[100];
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
		// 读取文件更新所有条目信息
		for (i = 0; i < n + 1; i ++)
		{
			if (node1ID == result[i].nodeID )	// 更新条目代价信息
			{
				for (j = 0; j < N; j ++)
				{
					if (node2ID == result[i].dvEntry[j].nodeID)
					{
						result[i].dvEntry[j].cost = buffer[22] - '0';
						break;
					}
				}
			}
			else if (node2ID == result[i].nodeID)	// 更新条目代价信息
			{
				for (j = 0; j < N; j ++)
				{
					if (node1ID == result[i].dvEntry[j].nodeID)
					{
						result[i].dvEntry[j].cost = buffer[22] - '0';
						break;
					}
				}
			}
		}	
	}
	return result;
}

//这个函数删除距离矢量表.
//它释放所有为距离矢量表动态分配的内存.
void dvtable_destroy(dv_t* dvtable)
{
	int n = topology_getNbrNum();
	int i;
	for (i = 0; i < n + 1; i ++)
	{
		free(dvtable[i].dvEntry);
	}
	free(dvtable);
}

//这个函数设置距离矢量表中2个节点之间的链路代价.
//如果这2个节点在表中发现了,并且链路代价也被成功设置了,就返回1,否则返回-1.
int dvtable_setcost(dv_t* dvtable,int fromNodeID,int toNodeID, unsigned int cost)
{
	int N = topology_getNodeNum();
	int n = topology_getNbrNum();
	int i,j;
	for (i = 0; i < n + 1; i ++)
	{
		if (dvtable[i].nodeID == fromNodeID)
		{
			for (j = 0; j < N; j ++)
			{
				if (dvtable[i].dvEntry[j].nodeID == toNodeID)
				{
					dvtable[i].dvEntry[j].cost = cost;
					return 1;
				}
			}
			return -1;
		}
	}
	return -1;
}

//这个函数返回距离矢量表中2个节点之间的链路代价.
//如果这2个节点在表中发现了,就返回链路代价,否则返回INFINITE_COST.
unsigned int dvtable_getcost(dv_t* dvtable, int fromNodeID, int toNodeID)
{
	int N = topology_getNodeNum();
	int n = topology_getNbrNum();
	int i,j;
	for (i = 0; i < n + 1; i ++)
	{
		if (dvtable[i].nodeID == fromNodeID)
		{
			for (j = 0; j < N; j ++)
			{
				if (dvtable[i].dvEntry[j].nodeID == toNodeID)
				{
					return dvtable[i].dvEntry[j].cost;
				}
			}
			return INFINITE_COST;
		}
	}
	return INFINITE_COST;
}

//这个函数打印距离矢量表的内容.
void dvtable_print(dv_t* dvtable)
{
	int N = topology_getNodeNum();
	int n = topology_getNbrNum();
	int i,j;
	int * array = topology_getNodeArray();
	printf("---------------dvtable---------------\n");
	printf("\t");
	for (i = 0; i < N; i ++)
	{
		printf("%d\t", array[i]);
	}
	printf("\n");
	for (i = 0; i < n + 1; i ++)
	{
		printf("%d:\t", dvtable[i].nodeID);
		for (j = 0; j < N; j ++)
		{
			printf("%d\t", dvtable[i].dvEntry[j].cost);
		}
		printf("\n");
	}
}
