
#include "seg.h"
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
#define SEGSTART1 0
#define SEGSTART2 1
#define SEGRECV 2
#define SEGSTOP1 3
#define SEGSTOP2 4
//STCP进程使用这个函数发送sendseg_arg_t结构(包含段及其目的节点ID)给SIP进程.
//参数sip_conn是在STCP进程和SIP进程之间连接的TCP描述符.
//如果sendseg_arg_t发送成功,就返回1,否则返回-1.
int sip_sendseg(int sip_conn, int dest_nodeID, seg_t* segPtr)
{
	//unsigned int check = checksum(segPtr);
	//segPtr->header.checksum = check;
	sendseg_arg_t *arg = (sendseg_arg_t *)malloc(sizeof(sendseg_arg_t));
	arg->nodeID = dest_nodeID;
	memcpy(&arg->seg, segPtr, sizeof(seg_t));
	char *buffer = "!&";
	if (send(sip_conn, buffer, 2, 0) < 0) {
		perror("Problem in sending data\n");
		return -1;
	}	
	buffer = (char *)arg;
	if (send(sip_conn, buffer, sizeof(sendseg_arg_t), 0) < 0) {
		perror("Problem in sending data\n");
		return -1;
	}
	buffer = "!#";
	if (send(sip_conn, buffer, 2, 0) < 0) {
		perror("Problem in sending data\n");
		return -1;
	}
	return 1;
}

//STCP进程使用这个函数来接收来自SIP进程的包含段及其源节点ID的sendseg_arg_t结构.
//参数sip_conn是STCP进程和SIP进程之间连接的TCP描述符.
//当接收到段时, 使用seglost()来判断该段是否应被丢弃并检查校验和.
//如果成功接收到sendseg_arg_t就返回1, 否则返回-1.
int sip_recvseg(int sip_conn, int* src_nodeID, seg_t* segPtr)
{
	//printf("--------------In function sip_recvseg----------------\n");
	sendseg_arg_t *arg = (sendseg_arg_t *)malloc(sizeof(sendseg_arg_t));
	char buffer;
	int state = SEGSTART1;
	int result;	// 判断TCP连接是否断开
	while (state != SEGSTOP2 && ((result = recv(sip_conn, &buffer, 1, 0)) > 0)) {
		switch (state) {	// FSM
			case SEGSTART1:
				if (buffer == '!') 
					state = SEGSTART2;
				break;
			case SEGSTART2:
				if (buffer == '&') {
					state = SEGRECV;
					recv(sip_conn, (char *)arg, sizeof(sendseg_arg_t), 0);	// recv seg data
				}
				break;
			case SEGRECV:
				if (buffer == '!')
					state = SEGSTOP1;
				break;
			case SEGSTOP1:
				if (buffer == '#')
					state = SEGSTOP2;
				break;
		}
	}   	
	//printf("seg->header.type is %d\n", arg->seg.header.type);
	if (seglost(segPtr) == 1) 	// 段丢失，可继续调用此函数
	{
		*src_nodeID = arg->nodeID;
		memcpy((char *)segPtr, (char *)&(arg->seg), sizeof(seg_t));
		return 1;	//??
	}
	//else if (checkchecksum(segPtr) < 0) {
	//	 printf("checksum error!!\n");
	//	 *src_nodeID = arg->nodeID;
	//		memcpy(segPtr, &arg->seg, sizeof(seg_t));
	//	 return 1;
 //	}
	else if (result <= 0) return -1;   // 连接断开， 不再调用此函数
	else {	// 接受成功
		*src_nodeID = arg->nodeID;
		memcpy((char *)segPtr, (char *)&(arg->seg), sizeof(seg_t));
		return 1;	
	}
}

//SIP进程使用这个函数接收来自STCP进程的包含段及其目的节点ID的sendseg_arg_t结构.
//参数stcp_conn是在STCP进程和SIP进程之间连接的TCP描述符.
//如果成功接收到sendseg_arg_t就返回1, 否则返回-1.
int getsegToSend(int stcp_conn, int* dest_nodeID, seg_t* segPtr)
{
	//printf("In function getsegTosend-------------\n");
	sendseg_arg_t *arg = (sendseg_arg_t *)malloc(sizeof(sendseg_arg_t));
	char buffer;
	int state = SEGSTART1;
	int result;	// 判断TCP连接是否断开
	while (state != SEGSTOP2 && ((result = recv(stcp_conn, &buffer, 1, 0)) > 0)) {
		switch (state) {	// FSM
			case SEGSTART1:
				if (buffer == '!') 
					state = SEGSTART2;
				break;
			case SEGSTART2:
				if (buffer == '&') {
					state = SEGRECV;
					recv(stcp_conn, (char *)arg, sizeof(sendseg_arg_t), 0);	// recv seg data
				}
				break;
			case SEGRECV:
				if (buffer == '!')
					state = SEGSTOP1;
				break;
			case SEGSTOP1:
				if (buffer == '#')
					state = SEGSTOP2;
				break;
		}
	}   	

	if (result <= 0) return -1;   // 连接断开， 不再调用此函数
	else {	// 接受成功
		*dest_nodeID = arg->nodeID;
		memcpy(segPtr, &arg->seg, sizeof(seg_t));
		return 1;	
	}
}

//SIP进程使用这个函数发送包含段及其源节点ID的sendseg_arg_t结构给STCP进程.
//参数stcp_conn是STCP进程和SIP进程之间连接的TCP描述符.
//如果sendseg_arg_t被成功发送就返回1, 否则返回-1.
int forwardsegToSTCP(int stcp_conn, int src_nodeID, seg_t* segPtr)
{
	sendseg_arg_t *arg = (sendseg_arg_t *)malloc(sizeof(sendseg_arg_t));
	arg->nodeID = src_nodeID;
	memcpy(&arg->seg, segPtr, sizeof(seg_t));
	//printf("---------------forwardsegToSTCP seg->header.type is %d---------\n", segPtr->header.type);
	char *buffer = "!&";
	if (send(stcp_conn, buffer, 2, 0) < 0) {
		perror("Problem in sending data\n");
		return -1;
	}	
	buffer = (char *)arg;
	if (send(stcp_conn, buffer, sizeof(sendseg_arg_t), 0) < 0) {
		perror("Problem in sending data\n");
		return -1;
	}
	buffer = "!#";
	if (send(stcp_conn, buffer, 2, 0) < 0) {
		perror("Problem in sending data\n");
		return -1;
	}
	return 1;
}

// 一个段有PKT_LOST_RATE/2的可能性丢失, 或PKT_LOST_RATE/2的可能性有着错误的校验和.
// 如果数据包丢失了, 就返回1, 否则返回0. 
// 即使段没有丢失, 它也有PKT_LOST_RATE/2的可能性有着错误的校验和.
// 我们在段中反转一个随机比特来创建错误的校验和.
int seglost(seg_t* segPtr)
{
	int random = rand()%100;
	if(random<PKT_LOSS_RATE*100) {
		//50%可能性丢失段
		if(rand()%2==0) {
			printf("seg lost!!!\n");
      		return 1;
		}
		//50%可能性是错误的校验和
		else {
			//获取数据长度
			int len = sizeof(stcp_hdr_t)+segPtr->header.length;
			//获取要反转的随机位
			int errorbit = rand()%(len*8);
			//反转该比特
			char* temp = (char*)segPtr;
			temp = temp + errorbit/8;
			*temp = *temp^(1<<(errorbit%8));
			return 0;
		}
	}
	return 0;
}

//这个函数计算指定段的校验和.
//校验和计算覆盖段首部和段数据. 你应该首先将段首部中的校验和字段清零, 
//如果数据长度为奇数, 添加一个全零的字节来计算校验和.
//校验和计算使用1的补码.
unsigned short checksum(seg_t* segment)
{
	segment->header.checksum = 0;
	unsigned short *pBuffer = (unsigned short*)segment;
	unsigned int sum = 0; 
	int length = 24 + segment->header.length;
	if(length == 24){
		int decrease = sizeof(unsigned short);
		for(; length > 1; length -= decrease)
			sum += *pBuffer++;
		if(length == 1)
			sum += *(unsigned char*)pBuffer;
		sum = (sum >> 16) + (sum & 0xffff); 
		sum += (sum >> 16);             
		return (unsigned short)(~sum);
	}
	else{	// 
		if(segment->header.length % 2 != 0){
			length += 1;
			segment->data[length] = 0;
		}
		int decrease = sizeof(unsigned short);
		for(; length > 1; length -= decrease)
			sum += *pBuffer++;
		if(length == 1)
			sum += *(unsigned char*)pBuffer;
		sum = (sum >> 16) + (sum & 0xffff);  
		sum += (sum >> 16);           
		return (unsigned short)(~sum);
	}
}

//这个函数检查段中的校验和, 正确时返回1, 错误时返回-1.
int checkchecksum(seg_t* segment)
{
	unsigned short *pBuffer = (unsigned short*)segment;
	unsigned int sum = 0; 
	int length = 24 + segment->header.length;
	if(length == 24){	// SYN FIN
		int decrease = sizeof(unsigned short);
		for(; length > 1; length -= decrease)
			sum += *pBuffer++;
		if(length == 1)
			sum += *(unsigned char*)pBuffer;
		sum = (sum >> 16) + (sum & 0xffff);  
		sum += (sum >> 16);             
		if((unsigned short)~sum == 0)
			return 1;
		else
			return -1;
	}
	else{	// DATA
		if(segment->header.length % 2 != 0){
			length += 1;
			segment->data[length] = 0;
		}
		int decrease = sizeof(unsigned short);
		for(; length > 1; length -= decrease)
			sum += *pBuffer++;
		if(length == 1)
			sum += *(unsigned char*)pBuffer;
		sum = (sum >> 16) + (sum & 0xffff);  
		sum += (sum >> 16);             
		if((unsigned short)~sum == 0)
			return 1;
		else
			return -1;
	}
}
