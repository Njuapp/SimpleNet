// 文件名: common/pkt.c
// 创建日期: 2018年

#include "pkt.h"
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
// son_sendpkt()由SIP进程调用, 其作用是要求SON进程将报文发送到重叠网络中. SON进程和SIP进程通过一个本地TCP连接互连.
// 在son_sendpkt()中, 报文及其下一跳的节点ID被封装进数据结构sendpkt_arg_t, 并通过TCP连接发送给SON进程. 
// 参数son_conn是SIP进程和SON进程之间的TCP连接套接字描述符.
// 当通过SIP进程和SON进程之间的TCP连接发送数据结构sendpkt_arg_t时, 使用'!&'和'!#'作为分隔符, 按照'!& sendpkt_arg_t结构 !#'的顺序发送.
// 如果发送成功, 返回1, 否则返回-1.
int son_sendpkt(int nextNodeID, sip_pkt_t* pkt, int son_conn)
{
  sendpkt_arg_t sendpkt;
  sendpkt.nextNodeID = nextNodeID;
  memcpy(&sendpkt.pkt, pkt, sizeof(sip_pkt_t));
  send(son_conn, "!&", 2, 0);
  send(son_conn, &sendpkt, sizeof(sendpkt), 0);
  send(son_conn, "!#", 2, 0);
  return 1;
}
enum recvstate
{
  PKTSTART1,
  PKTSTART2,
  PKTRECV,
  PKTSTOP1
};
// son_recvpkt()函数由SIP进程调用, 其作用是接收来自SON进程的报文.
// 参数son_conn是SIP进程和SON进程之间TCP连接的套接字描述符. 报文通过SIP进程和SON进程之间的TCP连接发送, 使用分隔符!&和!#.
// 为了接收报文, 这个函数使用一个简单的有限状态机FSM
// PKTSTART1 -- 起点
// PKTSTART2 -- 接收到'!', 期待'&'
// PKTRECV -- 接收到'&', 开始接收数据
// PKTSTOP1 -- 接收到'!', 期待'#'以结束数据的接收
// 如果成功接收报文, 返回1, 否则返回-1.
int son_recvpkt(sip_pkt_t* pkt, int son_conn)
{
  enum recvstate state = PKTSTART1;
  char buf[sizeof(sip_pkt_t) + 1];
  int cnt = 0;
  char tmp;
  int n = 0;
  while ((n = recv(son_conn, &tmp, 1, 0)) > 0)
  {
    switch(state){
      case PKTSTART1:
        if(tmp == '!')
          state = PKTSTART2;
        break;
      case PKTSTART2:
        if(tmp == '&')
          state = PKTRECV;
        break;
      case PKTRECV:
        buf[cnt++] = tmp;
        if(tmp == '!')
          state = PKTSTOP1;
        break;
      case PKTSTOP1:
        if(tmp == '#')
          cnt--;
        else if(tmp == '&')
          fprintf(stderr, "ERROR: when receiving packet !\n");
        else{
          buf[cnt++] = tmp;
          state = PKTRECV;
        }
        break;
      }
      if(tmp == '#' && state == PKTSTOP1){
        break;
      }
  }
  if(n < 0)
    return n;
  memcpy(pkt, buf, cnt);
  return cnt;
}

// 这个函数由SON进程调用, 其作用是接收数据结构sendpkt_arg_t.
// 报文和下一跳的节点ID被封装进sendpkt_arg_t结构.
// 参数sip_conn是在SIP进程和SON进程之间的TCP连接的套接字描述符. 
// sendpkt_arg_t结构通过SIP进程和SON进程之间的TCP连接发送, 使用分隔符!&和!#. 
// 为了接收报文, 这个函数使用一个简单的有限状态机FSM
// PKTSTART1 -- 起点 
// PKTSTART2 -- 接收到'!', 期待'&' 
// PKTRECV -- 接收到'&', 开始接收数据
// PKTSTOP1 -- 接收到'!', 期待'#'以结束数据的接收
// 如果成功接收sendpkt_arg_t结构, 返回1, 否则返回-1.
int getpktToSend(sip_pkt_t* pkt, int* nextNode,int sip_conn)
{
  enum recvstate state = PKTSTART1;
  char buf[sizeof(sendpkt_arg_t) + 1];
  int cnt = 0;
  char tmp;
  int n = 0;
  while ((n = recv(sip_conn, &tmp, 1, 0)) > 0)
  {
    switch(state){
      case PKTSTART1:
        if(tmp == '!')
          state = PKTSTART2;
        break;
      case PKTSTART2:
        if(tmp == '&')
          state = PKTRECV;
        break;
      case PKTRECV:
        buf[cnt++] = tmp;
        if(tmp == '!')
          state = PKTSTOP1;
        break;
      case PKTSTOP1:
        if(tmp == '#')
          cnt--;
        else if(tmp == '&')
          fprintf(stderr, "ERROR: when receiving packet !\n");
        else{
          buf[cnt++] = tmp;
          state = PKTRECV;
        }
        break;
      }
      
      if (tmp == '#' && state == PKTSTOP1)
      {
        break;
      }
  }
  if(n < 0){
    printf("getpktTosend failed\n");
    return n;
  }
  sendpkt_arg_t *sendpkt = (sendpkt_arg_t *)buf;
  memcpy(pkt, &sendpkt->pkt, sizeof(sip_pkt_t));
  memcpy(nextNode, &sendpkt->nextNodeID, sizeof(int));
  return cnt;
}

// forwardpktToSIP()函数是在SON进程接收到来自重叠网络中其邻居的报文后被调用的.
// SON进程调用这个函数将报文转发给SIP进程.
// 参数sip_conn是SIP进程和SON进程之间的TCP连接的套接字描述符.
// 报文通过SIP进程和SON进程之间的TCP连接发送, 使用分隔符!&和!#, 按照'!& 报文 !#'的顺序发送.
// 如果报文发送成功, 返回1, 否则返回-1.
int forwardpktToSIP(sip_pkt_t *pkt, int sip_conn)
{
  send(sip_conn, "!&", 2, 0);
  send(sip_conn, pkt, sizeof(sip_pkt_t), 0);
  send(sip_conn, "!#", 2, 0);
  return 1;
}

// sendpkt()函数由SON进程调用, 其作用是将接收自SIP进程的报文发送给下一跳.
// 参数conn是到下一跳节点的TCP连接的套接字描述符.
// 报文通过SON进程和其邻居节点之间的TCP连接发送, 使用分隔符!&和!#, 按照'!& 报文 !#'的顺序发送. 
// 如果报文发送成功, 返回1, 否则返回-1.
int sendpkt(sip_pkt_t* pkt, int conn)
{
  send(conn, "!&", 2, 0);
  send(conn, pkt, sizeof(sip_pkt_t), 0);
  send(conn, "!#", 2, 0);
  return 1;
}

// recvpkt()函数由SON进程调用, 其作用是接收来自重叠网络中其邻居的报文.
// 参数conn是到其邻居的TCP连接的套接字描述符.
// 报文通过SON进程和其邻居之间的TCP连接发送, 使用分隔符!&和!#. 
// 为了接收报文, 这个函数使用一个简单的有限状态机FSM
// PKTSTART1 -- 起点 
// PKTSTART2 -- 接收到'!', 期待'&' 
// PKTRECV -- 接收到'&', 开始接收数据
// PKTSTOP1 -- 接收到'!', 期待'#'以结束数据的接收 
// 如果成功接收报文, 返回1, 否则返回-1.
int recvpkt(sip_pkt_t* pkt, int conn)
{
  enum recvstate state = PKTSTART1;
  char buf[sizeof(sip_pkt_t) + 1];
  int cnt = 0;
  char tmp;
  int n = 0;
  while ((n = recv(conn, &tmp, 1, 0)) > 0)
  {
    switch(state){
      case PKTSTART1:
        if(tmp == '!')
          state = PKTSTART2;
        break;
      case PKTSTART2:
        if(tmp == '&')
          state = PKTRECV;
        break;
      case PKTRECV:
        buf[cnt++] = tmp;
        if(tmp == '!')
          state = PKTSTOP1;
        break;
      case PKTSTOP1:
        if(tmp == '#')
          cnt--;
        else if(tmp == '&')
          fprintf(stderr, "ERROR: when receiving packet !\n");
        else{
          buf[cnt++] = tmp;
          state = PKTRECV;
        }
        break;
      }
      if(tmp == '#' && state == PKTSTOP1){
        break;
      }
  }
  if(n < 0){
    return n;
  }
  memcpy(pkt, buf, cnt);
  return cnt;
}
