
#include "seg.h"
#include "stdio.h"
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>
#define SEND 987
#define RECV 789


void print_seg(seg_t* segPtr, int sendorrecv){
	if(sendorrecv == SEND)
		printf("-------SEND:-----\n");
	else
		printf("-------RECV:-----\n");
	printf("|SOURCE PORT: %d|\n", segPtr->header.src_port);
	printf("|DEST   PORT: %d|\n", segPtr->header.dest_port);
	printf("|SEQ     NUM: %d|\n", segPtr->header.seq_num);
	printf("|ACK     NUM: %d|\n", segPtr->header.ack_num);
	printf("|TYPE   : ");
	switch(segPtr->header.type){
		case SYNACK:
			printf("ACK   |\n");
			break;
		case SYN:
			printf("SYN   |\n");
			break;
		case FIN:
			printf("FIN   |\n");
			break;
		case FINACK:
			printf("FINACK|\n");
			break;
		case DATA:
			printf("DATA  |\n");
			break;
		case DATAACK:
			printf("DATACK|\n");
			break;
		default:
			printf("UNKOWN |\n");
	}
	printf("-----------------\n");
}

//STCP进程使用这个函数发送sendseg_arg_t结构(包含段及其目的节点ID)给SIP进程.
//参数sip_conn是在STCP进程和SIP进程之间连接的TCP描述符.
//如果sendseg_arg_t发送成功,就返回1,否则返回-1.
int sip_sendseg(int sip_conn, int dest_nodeID, seg_t* segPtr)
{
	//print_seg(segPtr, SEND);
	send(sip_conn, "!&", 2, 0);
	sendseg_arg_t *sendseg = (sendseg_arg_t *)malloc(sizeof(sendseg_arg_t));
	memcpy(&sendseg->seg, segPtr, sizeof(seg_t));
	sendseg->nodeID = dest_nodeID;
	send(sip_conn, sendseg, sizeof(sendseg_arg_t), 0);
	send(sip_conn, "!#", 2, 0);
	return 1;
}

//STCP进程使用这个函数来接收来自SIP进程的包含段及其源节点ID的sendseg_arg_t结构.
//参数sip_conn是STCP进程和SIP进程之间连接的TCP描述符.
//当接收到段时, 使用seglost()来判断该段是否应被丢弃并检查校验和.
//如果成功接收到sendseg_arg_t就返回1, 否则返回-1.
enum recvstate
{
	SEGSTART1,
	SEGSTART2,
	SEGRECV,
	SEGSTOP1
};
int sip_recvseg(int sip_conn, int* src_nodeID, seg_t* segPtr)
{
	//print_seg(segPtr, RECV);
	char tmp;
	enum recvstate state = SEGSTART1;
	int cnt = 0;
	char buf[sizeof(sendseg_arg_t)+1];
	int n = 0;
	while ((n= recv(sip_conn, &tmp, 1, 0)) > 0)
	{
		switch(state){
			case SEGSTART1:
				if(tmp == '!')
					state = SEGSTART2;
				break;
			case SEGSTART2:
				if(tmp == '&')
					state = SEGRECV;
				break;
			case SEGRECV:
				buf[cnt++] = tmp;
				if(tmp == '!')
					state = SEGSTOP1;
				break;
			case SEGSTOP1:
				if(tmp == '#')
					cnt--;
				else if(tmp == '&')
					fprintf(stderr, "ERROR: when receiving segment!\n");
				else{
					buf[cnt++] = tmp;
					state = SEGRECV;
				}
				break;
		}
		if(tmp == '#' && state == SEGSTOP1){
			break;
		}
	}
	if(n < 0){
		return n;
	}
	sendseg_arg_t *sendseg = (sendseg_arg_t *)buf;
	memcpy(segPtr, &sendseg->seg, sizeof(seg_t));
	memcpy(src_nodeID, &sendseg->nodeID, sizeof(int));
	return 1;
}

//SIP进程使用这个函数接收来自STCP进程的包含段及其目的节点ID的sendseg_arg_t结构.
//参数stcp_conn是在STCP进程和SIP进程之间连接的TCP描述符.
//如果成功接收到sendseg_arg_t就返回1, 否则返回-1.
int getsegToSend(int stcp_conn, int* dest_nodeID, seg_t* segPtr)
{
	char tmp;
	enum recvstate state = SEGSTART1;
	int cnt = 0;
	char buf[sizeof(sendseg_arg_t)];
	int n = 0;
	while ((n= recv(stcp_conn, &tmp, 1, 0)) > 0)
	{
		switch(state){
			case SEGSTART1:
				if(tmp == '!')
					state = SEGSTART2;
				break;
			case SEGSTART2:
				if(tmp == '&')
					state = SEGRECV;
				break;
			case SEGRECV:
				buf[cnt++] = tmp;
				if(tmp == '!')
					state = SEGSTOP1;
				break;
			case SEGSTOP1:
				if(tmp == '#')
					cnt--;
				else if(tmp == '&')
					fprintf(stderr, "ERROR: when receiving segment!\n");
				else{
					buf[cnt++] = tmp;
					state = SEGRECV;
				}
				break;
		}
		if(tmp == '#' && state == SEGSTOP1){
			break;
		}
	}
	if(n < 0){
		return n;
	}
	sendseg_arg_t *recvseg = (sendseg_arg_t *)buf;
	memcpy(segPtr, &recvseg->seg, sizeof(seg_t));
	memcpy(dest_nodeID, &recvseg->nodeID, sizeof(int));
	return cnt;
}

//SIP进程使用这个函数发送包含段及其源节点ID的sendseg_arg_t结构给STCP进程.
//参数stcp_conn是STCP进程和SIP进程之间连接的TCP描述符.
//如果sendseg_arg_t被成功发送就返回1, 否则返回-1.
int forwardsegToSTCP(int stcp_conn, int src_nodeID, seg_t* segPtr)
{
	send(stcp_conn, "!&", 2, 0);
	sendseg_arg_t *sendseg = (sendseg_arg_t *)malloc(sizeof(sendseg_arg_t));
	memcpy(&sendseg->seg, segPtr, sizeof(seg_t));
	sendseg->nodeID = src_nodeID;
	send(stcp_conn, sendseg, sizeof(sendseg_arg_t), 0);
	send(stcp_conn, "!#", 2, 0);
	return 1;
}

// 一个段有PKT_LOST_RATE/2的可能性丢失, 或PKT_LOST_RATE/2的可能性有着错误的校验和.
// 如果数据包丢失了, 就返回1, 否则返回0. 
// 即使段没有丢失, 它也有PKT_LOST_RATE/2的可能性有着错误的校验和.
// 我们在段中反转一个随机比特来创建错误的校验和.
int seglost(seg_t* segPtr)
{
  return 0;
}

//这个函数计算指定段的校验和.
//校验和计算覆盖段首部和段数据. 你应该首先将段首部中的校验和字段清零, 
//如果数据长度为奇数, 添加一个全零的字节来计算校验和.
//校验和计算使用1的补码.
unsigned short checksum(seg_t* segment)
{
  return 0;
}

//这个函数检查段中的校验和, 正确时返回1, 错误时返回-1.
int checkchecksum(seg_t* segment)
{
  return 0;
}
