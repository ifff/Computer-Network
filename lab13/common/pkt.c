// �ļ��� pkt.c
// ��������: 2013��1��
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
// son_sendpkt()��SIP���̵���, ��������Ҫ��SON���̽����ķ��͵��ص�������. SON���̺�SIP����ͨ��һ������TCP���ӻ���.
// ��son_sendpkt()��, ���ļ�����һ���Ľڵ�ID����װ�����ݽṹsendpkt_arg_t, ��ͨ��TCP���ӷ��͸�SON����. 
// ����son_conn��SIP���̺�SON����֮���TCP�����׽���������.
// ��ͨ��SIP���̺�SON����֮���TCP���ӷ������ݽṹsendpkt_arg_tʱ, ʹ��'!&'��'!#'��Ϊ�ָ���, ����'!& sendpkt_arg_t�ṹ !#'��˳����.
// ������ͳɹ�, ����1, ���򷵻�-1.
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

// son_recvpkt()������SIP���̵���, �������ǽ�������SON���̵ı���. 
// ����son_conn��SIP���̺�SON����֮��TCP���ӵ��׽���������. ����ͨ��SIP���̺�SON����֮���TCP���ӷ���, ʹ�÷ָ���!&��!#. 
// Ϊ�˽��ձ���, �������ʹ��һ���򵥵�����״̬��FSM
// PKTSTART1 -- ��� 
// PKTSTART2 -- ���յ�'!', �ڴ�'&' 
// PKTRECV -- ���յ�'&', ��ʼ��������
// PKTSTOP1 -- ���յ�'!', �ڴ�'#'�Խ������ݵĽ��� 
// ����ɹ����ձ���, ����1, ���򷵻�-1.
int son_recvpkt(sip_pkt_t* pkt, int son_conn)
{
	char buffer;
	int state = PKTSTART1;
	int result;	// �ж�TCP�����Ƿ�Ͽ�
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
	if (result <= 0)	// ���ӶϿ�
	{
		return -1;
	}
	else 
		return 1;
}

// ���������SON���̵���, �������ǽ������ݽṹsendpkt_arg_t.
// ���ĺ���һ���Ľڵ�ID����װ��sendpkt_arg_t�ṹ.
// ����sip_conn����SIP���̺�SON����֮���TCP���ӵ��׽���������. 
// sendpkt_arg_t�ṹͨ��SIP���̺�SON����֮���TCP���ӷ���, ʹ�÷ָ���!&��!#. 
// Ϊ�˽��ձ���, �������ʹ��һ���򵥵�����״̬��FSM
// PKTSTART1 -- ��� 
// PKTSTART2 -- ���յ�'!', �ڴ�'&' 
// PKTRECV -- ���յ�'&', ��ʼ��������
// PKTSTOP1 -- ���յ�'!', �ڴ�'#'�Խ������ݵĽ���
// ����ɹ�����sendpkt_arg_t�ṹ, ����1, ���򷵻�-1.
int getpktToSend(sip_pkt_t* pkt, int* nextNode,int sip_conn)
{
	sendpkt_arg_t *arg = (sendpkt_arg_t *)malloc(sizeof(sendpkt_arg_t));
	char buffer;
	int state = PKTSTART1;
	int result;	// �ж�TCP�����Ƿ�Ͽ�
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
	if (result <= 0)	// ���ӶϿ�
	{
		return -1;
	}
	else 
		return 1;
}

// forwardpktToSIP()��������SON���̽��յ������ص����������ھӵı��ĺ󱻵��õ�. 
// SON���̵����������������ת����SIP����. 
// ����sip_conn��SIP���̺�SON����֮���TCP���ӵ��׽���������. 
// ����ͨ��SIP���̺�SON����֮���TCP���ӷ���, ʹ�÷ָ���!&��!#, ����'!& ���� !#'��˳����. 
// ������ķ��ͳɹ�, ����1, ���򷵻�-1.
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

// sendpkt()������SON���̵���, �������ǽ�������SIP���̵ı��ķ��͸���һ��.
// ����conn�ǵ���һ���ڵ��TCP���ӵ��׽���������.
// ����ͨ��SON���̺����ھӽڵ�֮���TCP���ӷ���, ʹ�÷ָ���!&��!#, ����'!& ���� !#'��˳����. 
// ������ķ��ͳɹ�, ����1, ���򷵻�-1.
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

// recvpkt()������SON���̵���, �������ǽ��������ص����������ھӵı���.
// ����conn�ǵ����ھӵ�TCP���ӵ��׽���������.
// ����ͨ��SON���̺����ھ�֮���TCP���ӷ���, ʹ�÷ָ���!&��!#. 
// Ϊ�˽��ձ���, �������ʹ��һ���򵥵�����״̬��FSM
// PKTSTART1 -- ��� 
// PKTSTART2 -- ���յ�'!', �ڴ�'&' 
// PKTRECV -- ���յ�'&', ��ʼ��������
// PKTSTOP1 -- ���յ�'!', �ڴ�'#'�Խ������ݵĽ��� 
// ����ɹ����ձ���, ����1, ���򷵻�-1.
int recvpkt(sip_pkt_t* pkt, int conn)
{
	char buffer;
	int state = PKTSTART1;
	int result;	// �ж�TCP�����Ƿ�Ͽ�
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
	if (result <= 0)	// ���ӶϿ�
	{
		return -1;
	}
	else 
		return 1;
}
