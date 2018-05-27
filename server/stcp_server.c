//文件名: server/stcp_server.c
//
//描述: 这个文件包含STCP服务器接口实现. 
//
//创建日期: 2018年

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/select.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "stcp_server.h"
#include "../topology/topology.h"
#include "../common/constants.h"

//声明tcbtable为全局变量
server_tcb_t* tcbtable[MAX_TRANSPORT_CONNECTIONS];
//声明到SIP进程的连接为全局变量
int sip_conn;

/*********************************************************************/
//
//STCP API实现
//
/*********************************************************************/

// 这个函数初始化TCB表, 将所有条目标记为NULL. 它还针对TCP套接字描述符conn初始化一个STCP层的全局变量, 
// 该变量作为sip_sendseg和sip_recvseg的输入参数. 最后, 这个函数启动seghandler线程来处理进入的STCP段.
// 服务器只有一个seghandler.
void stcp_server_init(int conn) {
	// 这个函数初始化TCB表, 将所有条目标记为NULL.  
  for (int i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++)
    gtcb_table[i] = NULL;
	// 它还针对重叠网络TCP套接字描述符conn初始化一个STCP层的全局变量, 该变量作为sip_sendseg和sip_recvseg的输入参数.
	gsip_conn = conn;
	//  最后, 这个函数启动seghandler线程来处理进入的STCP段.服务器只有一个seghandler.
	pthread_t thread;
  int rc = pthread_create(&thread, NULL, &seghandler, NULL);
  if(rc){
    fprintf(stderr, "Error! return code %d when creating a code\n", rc);
    exit(-1);
  }
}

// 这个函数查找服务器TCB表以找到第一个NULL条目, 然后使用malloc()为该条目创建一个新的TCB条目.
// 该TCB中的所有字段都被初始化, 例如, TCB state被设置为CLOSED, 服务器端口被设置为函数调用参数server_port. 
// TCB表中条目的索引应作为服务器的新套接字ID被这个函数返回, 它用于标识服务器端的连接. 
// 如果TCB表中没有条目可用, 这个函数返回-1.
int stcp_server_sock(unsigned int server_port) 
{
	int k = -1;
  for (int i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++){
    if(gtcb_table[i] == NULL)
    {
      k = i;
      break;
    }
  }
  if(k == -1)
    return -1;
	//使用malloc()为该条目创建一个新的TCB条目.
  gtcb_table[k] = (server_tcb_t *)malloc(sizeof(server_tcb_t));
  //初始化TCB中的字段
  memset(gtcb_table[k], 0, sizeof(server_tcb_t));
	gtcb_table[k]->server_nodeID = topology_getMyNodeID();
	gtcb_table[k]->server_portNum = server_port;
	gtcb_table[k]->stt = CLOSED;
	gtcb_table[k]->recvBuf = (char *)malloc(sizeof(char) * RECEIVE_BUF_SIZE);
  gtcb_table[k]->bufMutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init(gtcb_table[k]->bufMutex, NULL);
	//TCB表中条目的索引号应作为客户端的新套接字ID被这个函数返回
	return k;
}

// 这个函数使用sockfd获得TCB指针, 并将连接的state转换为LISTENING. 它然后启动定时器进入忙等待直到TCB状态转换为CONNECTED 
// (当收到SYN时, seghandler会进行状态的转换). 该函数在一个无穷循环中等待TCB的state转换为CONNECTED,  
// 当发生了转换时, 该函数返回1. 你可以使用不同的方法来实现这种阻塞等待.
int stcp_server_accept(int sockfd) 
{
	server_tcb_t *tp = gtcb_table[sockfd];
	if(tp == NULL){
		fprintf(stderr, "Invalid sockfd\n");
		return -1;
	}
	switch(tp->stt){
		case CLOSED:
			tp->stt = LISTENING;
			printf("===LISTENING FROM CLIENT===\n");
			while(tp->stt != CONNECTED){
				sleep(1);
				printf("=");
			}
			printf("=>>\n===CONNECTION ESTABLISHED==\n");
		case LISTENING:
			return 1;
		case CONNECTED:
			fprintf(stderr, "Invalid state CONNECTED\n");
			return -1;
		case CLOSEWAIT:
			fprintf(stderr, "Invalid state CLOSEWAIT\n");
			return -1;
		}
		return 1;
}

// 接收来自STCP客户端的数据. 这个函数每隔RECVBUF_POLLING_INTERVAL时间
// 就查询接收缓冲区, 直到等待的数据到达, 它然后存储数据并返回1. 如果这个函数失败, 则返回-1.
int stcp_server_recv(int sockfd, void* buf, unsigned int length) {
  server_tcb_t* tp = gtcb_table[sockfd];
  if (tp == NULL) {
	fprintf(stderr, "socket does not exist\n");
	return -1;
  }
  switch (tp->stt) {
	case CLOSED: 
	  fprintf(stderr, "socket is in CLOSED\n");
	  return -1;
	case LISTENING:
	  fprintf(stderr, "socket is in LISTENING\n");
	  return -1;
	case CONNECTED:
	  while (1) {
		pthread_mutex_lock(tp->bufMutex);
		if(tp->usedBufLen >= length) {
		  memcpy(buf, tp->recvBuf, length);
		  tp->usedBufLen -= length;
		  memcpy(tp->recvBuf, tp->recvBuf + length, tp->usedBufLen);
  		  pthread_mutex_unlock(tp->bufMutex);
		  break;
		} else {
  		  pthread_mutex_unlock(tp->bufMutex);
  		  sleep(RECVBUF_POLLING_INTERVAL);
		}
	  }
	  break;
	case CLOSEWAIT:
	  fprintf(stderr, "socket is in CLOSEWAIT\n");
	  return -1;
	default:
	  break;
  }
  return 1;
}

// 这个函数调用free()释放TCB条目. 它将该条目标记为NULL, 成功时(即位于正确的状态)返回1,
// 失败时(即位于错误的状态)返回-1.
int stcp_server_close(int sockfd) {
	server_tcb_t *tp = gtcb_table[sockfd];
	if(tp == NULL){
		fprintf(stderr, "Invalid sockfd\n");
		return -1;
	}
	switch(tp->stt){
		case CLOSED:
			free(tp);
			tp = NULL;
			break;
		case LISTENING:
			fprintf(stderr, "Invalid state LISTENING\n");
			return -1;
		case CONNECTED:
			fprintf(stderr, "Invalid state CONNECTED \n");
			return -1;
		case CLOSEWAIT:
			fprintf(stderr, "Invalid state CLOSEWAIT \n");
			return -1;
		}
		return 1;
}

// 这是由stcp_server_init()启动的线程. 它处理所有来自客户端的进入数据. seghandler被设计为一个调用sip_recvseg()的无穷循环, 
// 如果sip_recvseg()失败, 则说明到SIP进程的连接已关闭, 线程将终止. 根据STCP段到达时连接所处的状态, 可以采取不同的动作.
// 请查看服务端FSM以了解更多细节.
void *finclock(void* arg) {
  server_tcb_t* tp = (server_tcb_t*)arg;
  sleep(CLOSEWAIT_TIMEOUT);
  tp->stt = CLOSED;
  pthread_exit(NULL);
}

void *seghandler(void* arg) {
	seg_t seg;
	int srcID;
	while (sip_recvseg(sip_conn, &srcID, &seg) > 0)
	{
		//根据dest_port和src_port找到对应的TCB
		int k = -1;
		for (int i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++){
			if(gtcb_table[i] != NULL
			&& gtcb_table[i]->server_portNum == seg.header.dest_port){
				k = i;
				break;
			}
		}
		if(k==-1){
			fprintf(stderr, "ERROR: received an invalid segment\n");
			exit(-1);
		}
		server_tcb_t *tp = gtcb_table[k];
		int segtype = seg.header.type;
		if(segtype == SYN){
			printf("===RECEIVED A SYN ===\n");
			//FSM state transition
			switch(tp->stt){
				case LISTENING:
					tp->stt = CONNECTED;
					tp->client_nodeID = srcID;
				case CONNECTED:
					//fulfill something missing in server TCB state
					tp->client_portNum = seg.header.src_port;
					tp->expect_seqNum = seg.header.seq_num + 1;
					//construct a SYNACK segment
					seg_t syn_ack;
					syn_ack.header.dest_port = tp->client_portNum;
					syn_ack.header.src_port = tp->server_portNum;
					syn_ack.header.seq_num = 0;
					syn_ack.header.ack_num = tp->expect_seqNum;
					syn_ack.header.length = 0;
					syn_ack.header.type = SYNACK;
					//send a SYNACK segment to client
					sip_sendseg(gsip_conn, srcID, &syn_ack);
					printf("===send a SYNACK to client===\n");
					break;
				case CLOSED:
					break;
				case CLOSEWAIT:
					break;
				}
		}
		else if(segtype == FIN){
			seg_t fin_ack;
			//FSM state transition
			switch(tp->stt){
				case LISTENING:
					break;
				case CLOSED:
					break;
				case CONNECTED:
					tp->stt = CLOSEWAIT;
					pthread_t fin_clk_thd;
					int rc = pthread_create(&fin_clk_thd, NULL, finclock, tp);
					if(rc){
						fprintf(stderr, "ERROR: failed when creating a FIN_CLOCK");
						exit(-1);
					}
				case CLOSEWAIT:
					//construct a FINACK segment
					fin_ack.header.dest_port = seg.header.src_port;
					fin_ack.header.src_port = seg.header.dest_port;
					fin_ack.header.seq_num = 0;
					fin_ack.header.ack_num = seg.header.seq_num + seg.header.length;
					fin_ack.header.length = 0;
					fin_ack.header.type = FINACK;
					//send a FIN_ACK segment to client
					sip_sendseg(gsip_conn, srcID, &fin_ack);
					printf("===send a FINACK to client===\n");
					break;
			}
		}else if (segtype == DATA && tp->stt == CONNECTED) {
			
			
		  if (tp->expect_seqNum == seg.header.seq_num) {
				tp->expect_seqNum += seg.header.length;
				pthread_mutex_lock(tp->bufMutex);
				memcpy(tp->recvBuf + tp->usedBufLen, seg.data, seg.header.length);
				tp->usedBufLen += seg.header.length;
				pthread_mutex_unlock(tp->bufMutex);
		  }
		  seg_t datack_seg;
		  datack_seg.header.src_port = tp->server_portNum;
		  datack_seg.header.dest_port = tp->client_portNum;
		  datack_seg.header.seq_num = 0;
		  datack_seg.header.ack_num = tp->expect_seqNum;
		  datack_seg.header.type = DATAACK;
		  sip_sendseg(gsip_conn, srcID, &datack_seg);
		  printf("send DATA ACK to client\n");
	  
			}
	}
	pthread_exit(NULL);
	return 0;
}


