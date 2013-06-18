//
// 文件名: seg.c

// 描述: 这个文件包含用于发送和接收STCP段的接口sip_sendseg() and sip_rcvseg(), 及其支持函数的实现. 
//
// 创建日期: 2013年1月
//

#include "seg.h"
#include "stdio.h"

//
//
//  用于客户端和服务器的SIP API 
//  =======================================
//
//  我们在下面提供了每个函数调用的原型定义和细节说明, 但这些只是指导性的, 你完全可以根据自己的想法来设计代码.
//
//  注意: sip_sendseg()和sip_recvseg()是由网络层提供的服务, 即SIP提供给STCP.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

// 通过重叠网络(在本实验中，是一个TCP连接)发送STCP段. 因为TCP以字节流形式发送数据, 
// 为了通过重叠网络TCP连接发送STCP段, 你需要在传输STCP段时，在它的开头和结尾加上分隔符. 
// 即首先发送表明一个段开始的特殊字符"!&"; 然后发送seg_t; 最后发送表明一个段结束的特殊字符"!#".  
// 成功时返回1, 失败时返回-1. sip_sendseg()首先使用send()发送两个字符, 然后使用send()发送seg_t,
// 最后使用send()发送表明段结束的两个字符.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int sip_sendseg(int connection, seg_t* segPtr)
{
 	segPtr->header.checksum = checksum(segPtr);
	char *buffer = "!&";
	if (send(connection, buffer, 2, 0) < 0) {
		perror("Problem in sending data\n");
		return -1;
	}	
	buffer = (char *)segPtr;
	if (send(connection, buffer, sizeof(seg_t), 0) < 0) {
		perror("Problem in sending data\n");
		return -1;
	}
	buffer = "!#";
	if (send(connection, buffer, 2, 0) < 0) {
		perror("Problem in sending data\n");
		return -1;
	}
	return 1;
}

// 通过重叠网络(在本实验中，是一个TCP连接)接收STCP段. 我们建议你使用recv()一次接收一个字节.
// 你需要查找"!&", 然后是seg_t, 最后是"!#". 这实际上需要你实现一个搜索的FSM, 可以考虑使用如下所示的FSM.
// SEGSTART1 -- 起点 
// SEGSTART2 -- 接收到'!', 期待'&' 
// SEGRECV -- 接收到'&', 开始接收数据
// SEGSTOP1 -- 接收到'!', 期待'#'以结束数据的接收
// 这里的假设是"!&"和"!#"不会出现在段的数据部分(虽然相当受限, 但实现会简单很多).
// 你应该以字符的方式一次读取一个字节, 将数据部分拷贝到缓冲区中返回给调用者.
//
// 注意: 在你剖析了一个STCP段之后,  你需要调用seglost()来模拟网络中数据包的丢失. 
// 在sip_recvseg()的下面是seglost(seg_t* segment)的代码.
//
// 一个段有PKT_LOST_RATE/2的可能性丢失, 或PKT_LOST_RATE/2的可能性有着错误的校验和.
// 如果数据包丢失了, 就返回1, 否则返回0. 
// 即使段没有丢失, 它也有PKT_LOST_RATE/2的可能性有着错误的校验和.
// 我们在段中反转一个随机比特来创建错误的校验和.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// 
#define SEGSTART1 0
#define SEGSTART2 1
#define SEGRECV 2
#define SEGSTOP1 3
#define SEGSTOP2 4
int sip_recvseg(int connection, seg_t* segPtr)
{
	//char *buffer = (char *)malloc(1);   ???
	char buffer;
	int state = SEGSTART1;
	int result;	// 判断TCP连接是否断开
	while (state != SEGSTOP2 && ((result = recv(connection, &buffer, 1, 0)) > 0)) {
		switch (state) {	// FSM
			case SEGSTART1:
				if (buffer == '!') 
					state = SEGSTART2;
				break;
			case SEGSTART2:
				if (buffer == '&') {
					state = SEGRECV;
					recv(connection, (char *)segPtr, sizeof(seg_t), 0);	// recv seg data
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
	if (seglost(segPtr) == 1) 	// 段丢失，可继续调用此函数
	{
		return 0;
	}
//	else if (checkchecksum(segPtr) < 0) {
	//	 printf("checksum error!!\n");
	//	 return 0;
 //		 }
	else if (result <= 0) return -1;   // 连接断开， 不再调用此函数
	else return 1;	// 接受成功
}

int seglost(seg_t* segPtr) {
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
//校验和覆盖段首部和段数据. 你应该首先将段首部中的校验和字段清零, 
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
		sum = (sum >> 16) + (sum & 0xffff);  //����16bit���16bit���
		sum += (sum >> 16);             //����λ����λ��16bit���16bit �����
		return (unsigned short)(~sum);
	}
	else{	// �����ֶ� 
		if(segment->header.length % 2 != 0){
			length += 1;
			segment->data[length] = 0;
		}
		int decrease = sizeof(unsigned short);
		for(; length > 1; length -= decrease)
			sum += *pBuffer++;
		if(length == 1)
			sum += *(unsigned char*)pBuffer;
		sum = (sum >> 16) + (sum & 0xffff);  //����16bit���16bit���
		sum += (sum >> 16);             //����λ����λ��16bit���16bit �����
		return (unsigned short)(~sum);
	}
}

//这个函数检查段中的校验和, 正确时返回1, 错误时返回-1
int checkchecksum(seg_t* segment)
{
  	unsigned short *pBuffer = (unsigned short*)segment;
	unsigned int sum = 0; 
	//���ݳ���Ϊ����������ȫ���ֶ�
	int length = 24 + segment->header.length;
	//SYN FINֻ���ײ�
	if(length == 24){	// SYN FIN
		int decrease = sizeof(unsigned short);
		for(; length > 1; length -= decrease)
			sum += *pBuffer++;
		if(length == 1)
			sum += *(unsigned char*)pBuffer;
		sum = (sum >> 16) + (sum & 0xffff);  //����16bit���16bit���
		sum += (sum >> 16);             //����λ����λ��16bit���16bit �����
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
		sum = (sum >> 16) + (sum & 0xffff);  //����16bit���16bit���
		sum += (sum >> 16);             //����λ����λ��16bit���16bit �����
		if((unsigned short)~sum == 0)
			return 1;
		else
			return -1;
	}
}
