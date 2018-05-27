//文件名: client/stcp_client.c
//
//描述: 这个文件包含STCP客户端接口实现 
//
//创建日期: 2018年

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <assert.h>
#include <strings.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include "../topology/topology.h"
#include "stcp_client.h"
#include "../common/seg.h"

//声明tcbtable为全局变量
client_tcb_t* tcbtable[MAX_TRANSPORT_CONNECTIONS];
//声明到SIP进程的TCP连接为全局变量
int sip_conn;

/*********************************************************************/
//
//STCP API实现
//
/*********************************************************************/
client_tcb_t* gtcb_table[MAX_TRANSPORT_CONNECTIONS];
int gsip_sonn;
// 这个函数初始化TCB表, 将所有条目标记为NULL.  
// 它还针对TCP套接字描述符conn初始化一个STCP层的全局变量, 该变量作为sip_sendseg和sip_recvseg的输入参数.
// 最后, 这个函数启动seghandler线程来处理进入的STCP段. 客户端只有一个seghandler.
#define min(x,y) (x>y)?y:x
void stcp_client_init(int conn) {
  for (int i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++)
    gtcb_table[i] = NULL;
  gsip_sonn = conn;

  pthread_t thread;
  int rc = pthread_create(&thread, NULL, &seghandler, NULL);
  if(rc){
    fprintf(stderr, "Error! return code %d when creating a code\n", rc);
    exit(-1);
  }
}

// 这个函数查找客户端TCB表以找到第一个NULL条目, 然后使用malloc()为该条目创建一个新的TCB条目.
// 该TCB中的所有字段都被初始化. 例如, TCB state被设置为CLOSED，客户端端口被设置为函数调用参数client_port. 
// TCB表中条目的索引号应作为客户端的新套接字ID被这个函数返回, 它用于标识客户端的连接. 
// 如果TCB表中没有条目可用, 这个函数返回-1.
int stcp_client_sock(unsigned int client_port) 
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
  gtcb_table[k] = (client_tcb_t *)malloc(sizeof(client_tcb_t));
  //初始化TCB中的字段
  memset(gtcb_table[k], 0, sizeof(client_tcb_t));
  gtcb_table[k]->client_nodeID = topology_getMyNodeID();
  gtcb_table[k]->client_portNum = client_port;
  gtcb_table[k]->stt = CLOSED;
  gtcb_table[k]->bufMutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init(gtcb_table[k]->bufMutex, NULL);
  //TCB表中条目的索引号应作为客户端的新套接字ID被这个函数返回
  return k;
}

// 这个函数用于连接服务器. 它以套接字ID, 服务器节点ID和服务器的端口号作为输入参数. 套接字ID用于找到TCB条目.  
// 这个函数设置TCB的服务器节点ID和服务器端口号,  然后使用sip_sendseg()发送一个SYN段给服务器.  
// 在发送了SYN段之后, 一个定时器被启动. 如果在SYNSEG_TIMEOUT时间之内没有收到SYNACK, SYN 段将被重传. 
// 如果收到了, 就返回1. 否则, 如果重传SYN的次数大于SYN_MAX_RETRY, 就将state转换到CLOSED, 并返回-1.
int stcp_client_connect(int sockfd, int nodeID, unsigned int server_port) 
{
	client_tcb_t *tp = gtcb_table[sockfd];
  if(tp == NULL){
    fprintf(stderr, "Invalid sockfd \n");
    return -1;
  }
  switch(tp->stt){
    case CLOSED:
      tp->server_nodeID = nodeID;
      tp->server_portNum = server_port;
      seg_t syn_seg;
      memset(&syn_seg, 0, sizeof(seg_t));
      syn_seg.header.src_port = tp->client_portNum;
      syn_seg.header.dest_port = tp->server_portNum;
      syn_seg.header.seq_num = 0;
      syn_seg.header.ack_num = 0;
      syn_seg.header.length = 0;
      syn_seg.header.type = SYN;
      tp->stt = SYNSENT;
      for(int i = 0; i < SYN_MAX_RETRY; i++){
        sip_sendseg(gsip_sonn, nodeID, &syn_seg);
        printf("===send a SYN to server (%d)===\n", i);
        sleep(1);
        if(tp->stt == CONNECTED){
          return 1;
        }
      }
      tp->stt = CLOSED;
      return -1;
      break;
    case SYNSENT:
      fprintf(stderr, "Invalid state SYNSENT\n");
      return -1;
    case CONNECTED:
      fprintf(stderr, "Invalid state CONNECTED\n");
      return -1;
    case FINWAIT:
      fprintf(stderr, "Invalid state FINWAIT\n");
      return -1;
  }
  return 1;
}

// 发送数据给STCP服务器. 这个函数使用套接字ID找到TCB表中的条目.
// 然后它使用提供的数据创建segBuf, 将它附加到发送缓冲区链表中.
// 如果发送缓冲区在插入数据之前为空, 一个名为sendbuf_timer的线程就会启动.
// 每隔SENDBUF_ROLLING_INTERVAL时间查询发送缓冲区以检查是否有超时事件发生. 
// 这个函数在成功时返回1，否则返回-1. 
// stcp_client_send是一个非阻塞函数调用.
// 因为用户数据被分片为固定大小的STCP段, 所以一次stcp_client_send调用可能会产生多个segBuf
// 被添加到发送缓冲区链表中. 如果调用成功, 数据就被放入TCB发送缓冲区链表中, 根据滑动窗口的情况,
// 数据可能被传输到网络中, 或在队列中等待传输.
void* sendBufhandler(void* clienttcb) {
  client_tcb_t* tp = (client_tcb_t*)clienttcb;
  if(tp == NULL) {
	fprintf(stderr, "socket does not exist\n");
	exit(-1);
  }
  while(1) {
	pthread_mutex_lock(tp->bufMutex);
	if(tp->sendBufHead == NULL) {
	  assert(tp->unAck_segNum == 0);
  	  pthread_mutex_unlock(tp->bufMutex);
	  pthread_exit(NULL);
	} 
  else {
	  segBuf_t* seg_p = tp->sendBufHead;
	  while(seg_p != tp->sendBufunSent) {
		  sip_sendseg(gsip_sonn , tp->server_nodeID, &seg_p->seg);
		  seg_p = seg_p->next;
	  }

	  for (; tp->unAck_segNum < GBN_WINDOW && tp->sendBufunSent != NULL; 
		  tp->unAck_segNum++, tp->sendBufunSent = tp->sendBufunSent->next) {
		  sip_sendseg(gsip_sonn, tp->server_nodeID, &tp->sendBufunSent->seg);
	  }  
	}
	pthread_mutex_unlock(tp->bufMutex);
	sleep(1);
  }
  return NULL;
}
int stcp_client_send(int sockfd, void* data, unsigned int length) 
{
  client_tcb_t *tp = gtcb_table[sockfd];
  if(!tp){
    fprintf(stderr, "sockfd not found");
  }
  if(tp->stt != CONNECTED){
    fprintf(stderr, "socket is not connected when sending data");
  }
  int num_seg = 0;
  num_seg = length / MAX_SEG_LEN;
  if(length % MAX_SEG_LEN)
    num_seg += 1;
  char *data_to_send = (char *)data;
  for (int i = 0; i * MAX_SEG_LEN < length; i++){
    if(tp->stt != CONNECTED){
      fprintf(stderr, "socket is not connected when sending data");
    }
    //构造新的待发送数据段
    int seglen = min(length - i * MAX_SEG_LEN, MAX_SEG_LEN);
    char *start = data_to_send + i * MAX_SEG_LEN;
    segBuf_t* segBuf = (segBuf_t*)malloc(sizeof(segBuf_t));
    memcpy(&segBuf->seg.data, start, seglen);
    segBuf->seg.header.src_port = tp->client_portNum;
    segBuf->seg.header.dest_port = tp->server_portNum;
    segBuf->seg.header.seq_num = tp->next_seqNum;
    tp->next_seqNum += seglen;
    segBuf->seg.header.ack_num = 0;
    segBuf->seg.header.length = seglen;
    segBuf->seg.header.type = DATA;
    segBuf->seg.header.rcv_win = GBN_WINDOW;
    segBuf->seg.header.checksum = 0;
    segBuf->next = NULL;

    if(tp->sendBufHead == NULL){
      tp->sendBufHead = segBuf;
      tp->sendBufTail = segBuf;
      tp->sendBufunSent = segBuf;
    }
    else{
      tp->sendBufTail->next = segBuf;
      tp->sendBufTail = segBuf;
    }
  }
  pthread_t sendBuf;
  int rc = pthread_create(&sendBuf, NULL, sendBufhandler, tp);
  if(rc){
    fprintf(stderr, "ERROR when creating thread\n");
  }
  
  while(1){
    pthread_mutex_lock(tp->bufMutex);
    if(tp->sendBufHead == NULL){
      pthread_mutex_unlock(tp->bufMutex);
      break;
    }
    pthread_mutex_unlock(tp->bufMutex);
	  sleep(1);
  }
  return 1;
}

// 这个函数用于断开到服务器的连接. 它以套接字ID作为输入参数. 套接字ID用于找到TCB表中的条目.  
// 这个函数发送FIN段给服务器. 在发送FIN之后, state将转换到FINWAIT, 并启动一个定时器.
// 如果在最终超时之前state转换到CLOSED, 则表明FINACK已被成功接收. 否则, 如果在经过FIN_MAX_RETRY次尝试之后,
// state仍然为FINWAIT, state将转换到CLOSED, 并返回-1.
int stcp_client_disconnect(int sockfd) 
{
	client_tcb_t *tp = gtcb_table[sockfd];
  if(tp == NULL){
    fprintf(stderr, "Invalid sockfd \n");
    return -1;
  }
  seg_t fin_seg;
  switch(tp->stt){
    case CONNECTED:
      
      memset(&fin_seg, 0, sizeof(seg_t));
      fin_seg.header.src_port = tp->client_portNum;
      fin_seg.header.dest_port = tp->server_portNum;
      fin_seg.header.seq_num = 0;
      fin_seg.header.ack_num = 0;
      fin_seg.header.length = 0;
      fin_seg.header.type = FIN;
      tp->stt = FINWAIT;
      for(int i = 0; i < FIN_MAX_RETRY; i++){
        sip_sendseg(gsip_sonn, tp->server_nodeID, &fin_seg);
        printf("===send a FIN to server (%d)===\n", i);
        sleep(1);
        if(tp->stt == CLOSED)
          return 1;
      }
      tp->stt = CLOSED;
      return -1;
      break;
    case SYNSENT:
      fprintf(stderr, "Invalid state SYNSENT\n");
      return -1;
    case CLOSED:
      fprintf(stderr, "Invalid state CLOSED\n");
      return -1;
    case FINWAIT:
      fprintf(stderr, "Invalid state FINWAIT\n");
      return -1;
  }
  return 1;
}

// 这个函数调用free()释放TCB条目. 它将该条目标记为NULL, 成功时(即位于正确的状态)返回1,
// 失败时(即位于错误的状态)返回-1.
int stcp_client_close(int sockfd) 
{
	client_tcb_t *tp = gtcb_table[sockfd];
  if(tp == NULL){
    fprintf(stderr, "Invalid sockfd\n");
    return -1;
  }
  switch(tp->stt){
    case CLOSED:
      free(tp);
      tp = NULL;
      break;
    case SYNSENT:
      fprintf(stderr, "Invalid state SYNSENT\n");
      return -1;
    case CONNECTED:
      fprintf(stderr, "Invalid state CONNECTED\n");
      return -1;
    case FINWAIT:
      fprintf(stderr, "Invalid state FINWAIT\n");
      return -1;
    }
    return 0;
}

// 这是由stcp_client_init()启动的线程. 它处理所有来自服务器的进入段. 
// seghandler被设计为一个调用sip_recvseg()的无穷循环. 如果sip_recvseg()失败, 则说明到SIP进程的连接已关闭,
// 线程将终止. 根据STCP段到达时连接所处的状态, 可以采取不同的动作. 请查看客户端FSM以了解更多细节.
void* seghandler(void* arg) 
{
	seg_t seg;
  int serverID;
  while (sip_recvseg(gsip_sonn, &serverID, &seg) > 0)
  {
		//根据dest_port和src_port找到对应的TCB
		int k = -1;
		for (int i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++){
			if(gtcb_table[i] != NULL
			&& gtcb_table[i]->server_portNum == seg.header.src_port
      && gtcb_table[i]->client_portNum == seg.header.dest_port){
				k = i;
				break;
			}
		}
		if(k==-1){
			fprintf(stderr, "ERROR: received an invalid segment\n");
			exit(-1);
		}
		client_tcb_t *tp = gtcb_table[k];
		int segtype = seg.header.type;
    if(segtype == SYNACK){
      //FSM
      switch(tp->stt){
        case SYNSENT:
         tp->stt = CONNECTED;
         tp->next_seqNum = seg.header.ack_num;
         break;
        default:
          break;
      }
    }
    else if(segtype == FINACK){
      switch(tp->stt){
        case FINWAIT:
          tp->stt = CLOSED;
          break;
        default:
          break;
      }
    }
    else if(segtype == DATAACK){
      printf("receive DATA ACK\n");
	  switch (tp->stt) {
		case CLOSED: break;
		case SYNSENT: break;
		case CONNECTED: 
		  pthread_mutex_lock(tp->bufMutex);
		  while (tp->sendBufHead != NULL &&
			  	 tp->sendBufHead != tp->sendBufunSent &&
  				 tp->sendBufHead->seg.header.seq_num < seg.header.ack_num) {
			segBuf_t* segbuf_p = tp->sendBufHead;
			tp->sendBufHead = segbuf_p->next;
			free(segbuf_p);
			tp->unAck_segNum--;
		  }

		  if(tp->sendBufHead == NULL) {
			tp->sendBufunSent = tp->sendBufTail = NULL;
			tp->unAck_segNum = 0;
		  }
		  pthread_mutex_unlock(tp->bufMutex);
		  break;
		case FINWAIT: break;
		default: break;
	  }
    }
  }
  return 0;
}


//这个线程持续轮询发送缓冲区以触发超时事件. 如果发送缓冲区非空, 它应一直运行.
//如果(当前时间 - 第一个已发送但未被确认段的发送时间) > DATA_TIMEOUT, 就发生一次超时事件.
//当超时事件发生时, 重新发送所有已发送但未被确认段. 当发送缓冲区为空时, 这个线程将终止.
void* sendBuf_timer(void* clienttcb) 
{
	return 0;
}

